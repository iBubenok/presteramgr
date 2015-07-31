#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/config/cpssDxChCfgInit.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>

#include <uthash.h>
#include <mll.h>
#include <debug.h>
#include <utils.h>

static int mll_sp;
static GT_U32 mll_ts;
static int *mll_st;

struct mll_pair {
  int pred;
  int succ;
  vid_t fvid;
  mcg_t fmcg;
  vid_t svid;
  mcg_t smcg;
};

static struct mll_pair *mll_pt; //pair table

enum status
mll_init (void)
{
  int i;

  CRP (cpssDxChCfgTableNumEntriesGet
       (0, CPSS_DXCH_CFG_TABLE_MLL_PAIR_E, &mll_ts));
  DEBUG ("MLL table size: %u\n", mll_ts);

  mll_st = malloc (sizeof (*mll_st) * mll_ts);
  for (i = 0; i < mll_ts; i++)
    mll_st[i] = i;
  mll_sp = 0;

  struct mll_pair nullnode = {-1, -1, 0, 0, 0, 0}; // -1 в кач-ве предка опасен

  mll_pt = malloc (sizeof (struct mll_pair) * mll_ts);
  for (i = 0; i < mll_ts; i++)
    mll_pt[i] = nullnode;

  return ST_OK;
}

int
mll_get (void)
{
  if (mll_sp >= mll_ts - 1)
    return -1;

  return mll_st[mll_sp++];
}

int
mll_put (int ix)
{
  if (mll_sp == 0)
    return -1;

  mll_st[--mll_sp] = ix;
  return 0;
}

static int create_pair (int pred, mcg_t mcg, vid_t vid)
{
  CPSS_DXCH_IP_MLL_PAIR_STC p;
  int idx;

  memset (&p, 0, sizeof (p));

  p.firstMllNode.mllRPFFailCommand = CPSS_PACKET_CMD_DROP_SOFT_E;
  p.firstMllNode.isTunnelStart = GT_FALSE;
  p.firstMllNode.nextHopInterface.type = CPSS_INTERFACE_VIDX_E;
  p.firstMllNode.nextHopInterface.vidx = mcg;
  p.firstMllNode.nextHopVlanId = vid;
  p.firstMllNode.ttlHopLimitThreshold = 0;
  p.firstMllNode.excludeSrcVlan = GT_FALSE;
  p.firstMllNode.last = GT_TRUE;
  /* Just in case; shouldn't be really necessary. */
  memcpy (&p.secondMllNode, &p.firstMllNode, sizeof (p.secondMllNode));

  DEBUG ("Getting new mll.\n");

  idx = mll_get ();
  if (idx == -1)
    return -1;

  DEBUG ("Mll = %d\n", idx);

  ON_GT_ERROR
    (CRP (cpssDxChIpMLLPairWrite
          (0, idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p))) {
    mll_put (idx);
    return -1;
  }

  struct mll_pair pair = {pred, -1, vid, mcg, 0, 0};
  mll_pt [idx] = pair;

  DEBUG ("Pair created.\n");
  return idx;
}

static int find_last_pair (int head)
{
  int cur = head;
  while (1) {
    if (mll_pt[cur].succ == -1)
      return cur;
    else
      cur = mll_pt[cur].succ;
  }
}

static int pair_set_next_pair (int pred, int succ)
{
  CPSS_DXCH_IP_MLL_PAIR_STC p;

  memset (&p, 0, sizeof (p));

  CRP (cpssDxChIpMLLPairRead
          (0, pred, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p));

  p.secondMllNode.last = GT_FALSE;
  p.nextPointer = succ;

  mll_pt[pred].succ = succ;

  CRP (cpssDxChIpMLLPairWrite
          (0, pred, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p));


  return 0;
}

static int insert_second_node (int pair, mcg_t mcg, vid_t vid)
{
  CPSS_DXCH_IP_MLL_PAIR_STC p;

  memset (&p, 0, sizeof (p));

  CRP (cpssDxChIpMLLPairRead
        (0, pair, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p));

  memcpy (&p.secondMllNode, &p.firstMllNode, sizeof (p.secondMllNode));

  p.firstMllNode.last = GT_FALSE;
  p.secondMllNode.nextHopInterface.vidx = mcg;
  p.secondMllNode.nextHopVlanId = vid;
  p.secondMllNode.last = GT_TRUE;

  CRP (cpssDxChIpMLLPairWrite
        (0, pair, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p));

  mll_pt[pair].svid = vid;
  mll_pt[pair].smcg = mcg;

  return 0;
}

static int append_node (int pred, mcg_t mcg, vid_t vid)
{
  int new_pair = -1;
  int last_pair = find_last_pair (pred);


  if (mll_pt[last_pair].svid == 0)
    insert_second_node (last_pair, mcg, vid);
  else {
    new_pair = create_pair (pred, mcg, vid);
    pair_set_next_pair (last_pair, new_pair);
  }

  return 0;

}

int add_node (int pred, mcg_t mcg, vid_t vid)
{
  DEBUG ("Add node. Predecessor = %d.\n", pred);
  if (pred < 0) {
    DEBUG ("Creating first pair. Mcg = %d, vid = %d\n", mcg, vid);
    return create_pair (pred, mcg, vid);
  }

  else {
    DEBUG ("Appending node Mcg = %d, vid = %d, to chain %d\n", mcg, vid, pred);
    append_node (pred, mcg, vid);
  }

  return pred;
}

//------------------------------------------------------------------------------

static int find_pair (int pred, mcg_t mcg, vid_t vid)
{
  int cur = pred;

  while (((mll_pt[cur].fmcg != mcg) ||
          (mll_pt[cur].fvid != vid))
         &&
         ((mll_pt[cur].smcg != mcg) ||
          (mll_pt[cur].svid != vid))) {
    if (mll_pt[cur].succ == -1)
      return -1;
    cur = mll_pt[cur].succ;
  }

  return cur;
}

#define KEEP_FLAG 0
#define SET_FLAG 1
#define RESET_FLAG 2

#define CP_SECD_NODE_TO_FIRST_NODE 0
#define KEEP_SECD_NODE 1

#define CP_CUR_FRST_TO_LAST_SECD 0
#define CP_CUR_SECD_TO_LAST_SECD 1
#define SET_LAST_NEXT_TO_CUR 2

#define SKIP -1

static int
pair_modify_chain (int idx,
                   int flag1,
                   int flag2,
                   int cp_secd,
                   int next,
                   int last_idx,
                   int last_idx_cmd)
{
  CPSS_DXCH_IP_MLL_PAIR_STC p, last_p;

  memset (&p, 0, sizeof (p));

  CRP (cpssDxChIpMLLPairRead
        (0, idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p));

  if (cp_secd == CP_SECD_NODE_TO_FIRST_NODE)
    memcpy (&p.firstMllNode, &p.secondMllNode, sizeof (p.firstMllNode));

  if (flag1 == SET_FLAG)
    p.firstMllNode.last = GT_TRUE;
  else if (flag1 == RESET_FLAG)
    p.firstMllNode.last = GT_FALSE;

  if (flag2 == SET_FLAG)
    p.secondMllNode.last = GT_TRUE;
  else if (flag2 == RESET_FLAG)
    p.secondMllNode.last = GT_FALSE;

  if (next != SKIP)
    p.nextPointer = next;

  if (last_idx_cmd == CP_CUR_FRST_TO_LAST_SECD) {
    CRP (cpssDxChIpMLLPairRead
        (0, last_idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &last_p));

    memcpy (&last_p.secondMllNode, &p.firstMllNode, sizeof (p.firstMllNode));

    last_p.firstMllNode.last = GT_FALSE;
    last_p.secondMllNode.last = GT_TRUE;

    CRP (cpssDxChIpMLLPairWrite
          (0, last_idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &last_p));

    return 0;

  } else if (last_idx_cmd == CP_CUR_SECD_TO_LAST_SECD) {
    CRP (cpssDxChIpMLLPairRead
        (0, last_idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &last_p));

    memcpy (&last_p.secondMllNode, &p.secondMllNode, sizeof (p.secondMllNode));

    last_p.firstMllNode.last = GT_FALSE;
    last_p.secondMllNode.last = GT_TRUE;

    CRP (cpssDxChIpMLLPairWrite
          (0, last_idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &last_p));

    return 0;

  } else if (last_idx_cmd == SET_LAST_NEXT_TO_CUR) {
    CRP (cpssDxChIpMLLPairRead
        (0, last_idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &last_p));

    last_p.nextPointer = idx;
    last_p.secondMllNode.last = GT_FALSE;

    CRP (cpssDxChIpMLLPairWrite
          (0, idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p));

    CRP (cpssDxChIpMLLPairWrite
          (0, last_idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &last_p));

    return 0;
  }

  CRP (cpssDxChIpMLLPairWrite
          (0, idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p));

  return 0;
}

static int do_del_node (int idx, mcg_t mcg, vid_t vid, int head_idx)
{
  int single_node, firstnode, head, tail, complete_tail;

  // Check whether there is only one (the first) node in the record
  if (mll_pt[idx].svid == 0)
    single_node = 1;
  else
    single_node = 0;

  // Check if we delete first node in the struct
  if (mll_pt[idx].fvid == vid)
    firstnode = 1;
  else
    firstnode = 0;

  // Check whether we delete the head
  if (mll_pt[idx].pred < 0)
    head = 1;
  else
    head = 0;

  // Check whether we delete the tail
  if (mll_pt[idx].succ < 0)
    tail = 1;
  else
    tail = 0;

  int last_idx = find_last_pair (idx);

  // Check whether tail has two nodes
  if (mll_pt[last_idx].svid == 0)
    complete_tail = 0;
  else
    complete_tail = 1;

  //--------------------------------------------------------------------

  int w_idx, new_head_idx, succ_idx;
  struct mll_pair nullnode = {-1, -1, 0, 0, 0, 0};

  if (firstnode) {
    if (single_node) {
      if (head) { // (1) firstnode && single_node && head

        mll_pt[idx] = nullnode;
        mll_put (idx);

        // There is no more chain
        return -2;

      } else {    // (2) firstnode && single_node && !(head)

        //find predecessor
        w_idx = mll_pt[idx].pred;

        //make predecessor tail
        pair_modify_chain (w_idx, SKIP, SET_FLAG, SKIP, SKIP, SKIP, SKIP);

        mll_pt[idx] = nullnode;
        mll_put (idx);

        mll_pt[w_idx].succ = -1;

        // Chain keep head
        return head_idx;

      }
    } else {
      if (head) { // firstnode && !(single_node) && head
        if (tail) {  // (3) firstnode && !(single_node) && head && tail

          // copy second node to first node
          pair_modify_chain (idx, SET_FLAG, SKIP, CP_SECD_NODE_TO_FIRST_NODE,
                             SKIP, SKIP, SKIP);

          mll_pt[idx].fvid = mll_pt[idx].svid;
          mll_pt[idx].fmcg = mll_pt[idx].smcg;

          mll_pt[idx].svid = 0;
          mll_pt[idx].smcg = 0;

          // Chain keep head
          return head_idx;

        } else {
          if (complete_tail) { // (4) firstnode && !(single_node) &&
                                    // head && !(tail) && complete_tail

            pair_modify_chain (idx, SET_FLAG, SKIP, CP_SECD_NODE_TO_FIRST_NODE,
                               SKIP, last_idx, SET_LAST_NEXT_TO_CUR);

            mll_pt[idx].fvid = mll_pt[idx].svid;
            mll_pt[idx].fmcg = mll_pt[idx].smcg;

            mll_pt[idx].svid = 0;
            mll_pt[idx].smcg = 0;

            succ_idx = mll_pt[idx].succ;

            mll_pt[succ_idx].pred = mll_pt[idx].pred;

            new_head_idx = mll_pt[idx].succ;

            mll_pt[idx].succ = -1;

            mll_pt[last_idx].succ = idx;

            // Know the second record become the head
            return new_head_idx;

          } else { // (5) firstnode && !(single_node) &&
                 // head && !(tail) && !(complete_tail)

            pair_modify_chain (idx, SKIP, SET_FLAG, SKIP,
                               SKIP, last_idx, CP_CUR_SECD_TO_LAST_SECD);

            mll_pt[last_idx].svid = mll_pt[idx].svid;
            mll_pt[last_idx].smcg = mll_pt[idx].smcg;

            succ_idx = mll_pt[idx].succ;

            mll_pt[succ_idx].pred = mll_pt[idx].pred;

            new_head_idx = mll_pt[idx].succ;

            mll_pt[idx] = nullnode;
            mll_put (idx);

            // Know the second record become the head
            return new_head_idx;
          }
        }
      } else {
        if (tail) { // (6) firstnode && !(single_node) && !(head) && tail

          pair_modify_chain (idx, SET_FLAG, SKIP, CP_SECD_NODE_TO_FIRST_NODE,
                             SKIP, SKIP, SKIP);

          mll_pt[idx].fvid = mll_pt[idx].svid;
          mll_pt[idx].fmcg = mll_pt[idx].smcg;

          mll_pt[idx].svid = 0;
          mll_pt[idx].smcg = 0;

          // Chain keep head
          return head_idx;

        } else { // firstnode && !(single_node) && !(head) && !(tail)

          if (complete_tail) { // (7) firstnode && !(single_node) &&
                               // !(head) && !(tail) && complete_tail

            w_idx = mll_pt[idx].pred;
            succ_idx = mll_pt[idx].succ;

            pair_modify_chain (w_idx, SKIP, SKIP, SKIP, succ_idx, SKIP, SKIP);

            mll_pt[w_idx].succ = succ_idx;

            pair_modify_chain (idx, SET_FLAG, SKIP, CP_SECD_NODE_TO_FIRST_NODE,
                               SKIP, SKIP, SKIP);

            mll_pt[idx].fvid = mll_pt[idx].svid;
            mll_pt[idx].fmcg = mll_pt[idx].smcg;

            mll_pt[idx].svid = 0;
            mll_pt[idx].smcg = 0;

            mll_pt[idx].succ = -1;

            pair_modify_chain (last_idx, SKIP, RESET_FLAG, SKIP, idx, SKIP, SKIP);

            mll_pt[last_idx].succ = idx;

            // Chain keep head
            return head_idx;

          } else { // (8) firstnode && !(single_node) &&
                   // !(head) && !(tail) && !(complete_tail)

            w_idx = mll_pt[idx].pred;
            succ_idx = mll_pt[idx].succ;

            pair_modify_chain (w_idx, SKIP, SKIP, SKIP, succ_idx, SKIP, SKIP);

            mll_pt[w_idx].succ = succ_idx;

            pair_modify_chain (idx, SKIP, SET_FLAG, SKIP,
                               SKIP, last_idx, CP_CUR_SECD_TO_LAST_SECD);

            mll_pt[last_idx].svid = mll_pt[idx].svid;
            mll_pt[last_idx].smcg = mll_pt[idx].smcg;

            mll_pt[idx] = nullnode;
            mll_put (idx);

            // Chain keep head
            return head_idx;

          }
        }
      }
    }
  } else { // !(firstnode)
    if (tail) { // (9) !(firstnode) && tail

      pair_modify_chain (idx, SET_FLAG, SKIP, SKIP, SKIP, SKIP, SKIP);

      mll_pt[idx].svid = 0;
      mll_pt[idx].smcg = 0;

      // Chain keep head
      return head_idx;
    } else { // !(firstnode) && !(tail)
      if (head) {
        if (complete_tail) { // (10) !(firstnode) && !(tail) &&
                             // head && complete_tail
          new_head_idx = mll_pt[idx].succ;
          pair_modify_chain (new_head_idx, SKIP, SKIP, SKIP, idx, SKIP, SKIP);
          pair_modify_chain (last_idx, SKIP, RESET_FLAG, SKIP, idx, SKIP, SKIP);
          mll_pt[last_idx].succ = idx;

          pair_modify_chain (idx, SET_FLAG, SKIP, SKIP, SKIP, SKIP, SKIP);

          mll_pt[idx].svid = 0;
          mll_pt[idx].smcg = 0;
          mll_pt[idx].succ = -1;

          // Know the second record become the head
          return new_head_idx;

        } else { // (11) !(firstnode) && !(tail) &&
                 // head && !(complete_tail)

          pair_modify_chain (idx, SKIP, SKIP, SKIP,
                             SKIP, last_idx, CP_CUR_FRST_TO_LAST_SECD);

          mll_pt[last_idx].svid = mll_pt[idx].fvid;
          mll_pt[last_idx].smcg = mll_pt[idx].fmcg;

          succ_idx = mll_pt[idx].succ;

          mll_pt[succ_idx].pred = mll_pt[idx].pred;

          new_head_idx = mll_pt[idx].succ;

          mll_pt[idx] = nullnode;
          mll_put (idx);

          // Know the second record become the head
          return new_head_idx;
        }
      } else { // !(firstnode) && !(tail) && !(head)
        if (complete_tail) { // (12 !(firstnode) && !(tail) &&
                             // !(head) && complete_tail
          w_idx = mll_pt[idx].pred;
          succ_idx = mll_pt[idx].succ;

          pair_modify_chain (w_idx, SKIP, SKIP, SKIP, succ_idx, SKIP, SKIP);

          mll_pt[w_idx].succ = succ_idx;


          pair_modify_chain (idx, SET_FLAG, SKIP, SKIP, SKIP, SKIP, SKIP);

          mll_pt[idx].svid = 0;
          mll_pt[idx].smcg = 0;
          mll_pt[idx].succ = -1;

          pair_modify_chain (last_idx, SKIP, RESET_FLAG, SKIP, idx, SKIP, SKIP);

          mll_pt[last_idx].succ = idx;

          // Chain keep head
          return head_idx;

        } else { // (13 !(firstnode) && !(tail) &&
                 // !(head) && complete_tail

          pair_modify_chain (idx, SKIP, SKIP, SKIP,
                             SKIP, last_idx, CP_CUR_FRST_TO_LAST_SECD);

          mll_pt[last_idx].svid = mll_pt[idx].fvid;
          mll_pt[last_idx].smcg = mll_pt[idx].fmcg;

          mll_pt[idx] = nullnode;
          mll_put (idx);

          return head_idx;
        }
      }
    }
  }
  // We must not get here!!!
  return -3;
}

int del_node (int head_idx, mcg_t mcg, vid_t vid)
{
  int idx = find_pair (head_idx, mcg, vid);

  if (idx == -1) {
    return -1; //there is no such node
  }
  else {
    return do_del_node (idx, mcg, vid, head_idx);
  }
}
