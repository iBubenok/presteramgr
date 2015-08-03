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

static int debug_pair (int idx) {
  DEBUG ("Pred = %d, Succ = %d, 1Vlan = %d, 1Mcg = %d, 2Vlan = %d, 2Mcg = %d\n",
         mll_pt[idx].pred, mll_pt[idx].succ, mll_pt[idx].fvid,
         mll_pt[idx].fmcg, mll_pt[idx].svid, mll_pt[idx].smcg);
  return 0;
}

static int create_pair (int pred, mcg_t mcg, vid_t vid)
{
  CPSS_DXCH_IP_MLL_PAIR_STC p;
  int idx;

  DEBUG ("Creating new pair.\n");

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

  DEBUG ("Pair created. Pair is\n");
  debug_pair (idx);
  return idx;
}

static int find_last_pair (int head)
{
  DEBUG ("Finding last pair in chain %d", head);
  int cur = head;
  while (1) {
    debug_pair (cur);
    if (mll_pt[cur].succ == -1) {
      DEBUG ("Last pair found! It is %d\n", cur);
      return cur;
    }
    else
      cur = mll_pt[cur].succ;
  }
}

static int pair_set_next_pair (int pred, int succ)
{
  CPSS_DXCH_IP_MLL_PAIR_STC p;

  DEBUG ("Linking pair %d to %d\n", pred, succ);

  memset (&p, 0, sizeof (p));

  CRP (cpssDxChIpMLLPairRead
          (0, pred, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p));

  p.secondMllNode.last = GT_FALSE;
  p.nextPointer = succ;

  mll_pt[pred].succ = succ;

  CRP (cpssDxChIpMLLPairWrite
          (0, pred, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p));

  DEBUG ("New chain is \n");
  debug_pair (pred);
  debug_pair (succ);

  return 0;
}

static int insert_second_node (int pair, mcg_t mcg, vid_t vid)
{
  CPSS_DXCH_IP_MLL_PAIR_STC p;

  DEBUG ("Inserting second node to pair %d\n", pair);
  debug_pair (pair);

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

  DEBUG ("Updated pair %d is \n", pair);
  debug_pair (pair);

  return 0;
}

static int append_node (int pred, mcg_t mcg, vid_t vid)
{
  int new_pair = -1;
  int last_pair = find_last_pair (pred);


  if (mll_pt[last_pair].svid == 0)
    insert_second_node (last_pair, mcg, vid);
  else {
    new_pair = create_pair (last_pair, mcg, vid);
    pair_set_next_pair (last_pair, new_pair);
  }

  return 0;

}

int add_node (int pred, mcg_t mcg, vid_t vid)
{
  DEBUG ("Add node. Predecessor = %d.\n", pred);
  if (pred < 0) {
    DEBUG ("Creating first pair of chain. Mcg = %d, vid = %d\n", mcg, vid);
    return create_pair (pred, mcg, vid);
  }

  else {
    DEBUG ("Appending node. Mcg = %d, vid = %d, to chain %d\n", mcg, vid, pred);
    append_node (pred, mcg, vid);
  }

  return pred;
}

//------------------------------------------------------------------------------

static int find_pair (int pred, mcg_t mcg, vid_t vid)
{
  int cur = pred;

  DEBUG ("Finding pair vlan %d mcg %d in chain %d", vid, mcg, pred);

  while (((mll_pt[cur].fmcg != mcg) ||
          (mll_pt[cur].fvid != vid))
         &&
         ((mll_pt[cur].smcg != mcg) ||
          (mll_pt[cur].svid != vid))) {
    if (mll_pt[cur].succ == -1)
      return -1;
    debug_pair (cur);
    cur = mll_pt[cur].succ;
  }

  DEBUG ("Pair found! It is %d\n", cur);

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

  DEBUG ("Do del node vid %d mcg %d of pair %d in chain %d",
         vid, mcg, idx, head_idx);

  // Check whether there is only one (the first) node in the record
  if (mll_pt[idx].svid == 0)
    single_node = 1;
  else
    single_node = 0;

  // Check if we delete first node in the struct
  if ((mll_pt[idx].fvid == vid) && (mll_pt[idx].fmcg == mcg))
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

  DEBUG ("Flags are: sgnode - %d, fnode - %d, "
         "head %d, tail - %d, comp_tl - %d\n",
         single_node, firstnode, head, tail, complete_tail);

  //--------------------------------------------------------------------

  int w_idx, new_head_idx, succ_idx;
  struct mll_pair nullnode = {-1, -1, 0, 0, 0, 0};

  if (firstnode) {
    if (single_node) {
      if (head) { // (1) firstnode && single_node && head

        DEBUG ("CASE 1. Delete the only node in the head-tail.\n");

        mll_pt[idx] = nullnode;
        debug_pair (idx);
        mll_put (idx);

        DEBUG ("MLL pair %d is free.\n", idx);

        // There is no more chain
        return -2;

      } else {    // (2) firstnode && single_node && !(head)

        DEBUG ("CASE 2. Delete the first node in the incomplete tail\n");

        //find predecessor
        w_idx = mll_pt[idx].pred;

        DEBUG ("Last two pairs were:\n");
        debug_pair (w_idx);
        debug_pair (idx);

        //make predecessor tail
        pair_modify_chain (w_idx, SKIP, SET_FLAG, SKIP, SKIP, SKIP, SKIP);

        mll_pt[idx] = nullnode;
        mll_put (idx);

        mll_pt[w_idx].succ = -1;

        DEBUG ("Last two pairs are:\n");
        debug_pair (w_idx);
        debug_pair (idx);

        // Chain keep head
        return head_idx;

      }
    } else {
      if (head) { // firstnode && !(single_node) && head
        if (tail) {  // (3) firstnode && !(single_node) && head && tail

          DEBUG ("CASE 3. Delete the first node in the complete head-tail\n");

          // copy second node to first node
          pair_modify_chain (idx, SET_FLAG, SKIP, CP_SECD_NODE_TO_FIRST_NODE,
                             SKIP, SKIP, SKIP);

          DEBUG ("Pair was:\n");
          debug_pair (idx);


          mll_pt[idx].fvid = mll_pt[idx].svid;
          mll_pt[idx].fmcg = mll_pt[idx].smcg;

          mll_pt[idx].svid = 0;
          mll_pt[idx].smcg = 0;

          DEBUG ("Pair is:\n");
          debug_pair (idx);

          // Chain keep head
          return head_idx;

        } else {
          if (complete_tail) { // (4) firstnode && !(single_node) &&
                                    // head && !(tail) && complete_tail

            DEBUG ("CASE 4. Delete the first node in the head when "
                   "tail is complete\n");

            DEBUG ("Head, second and tail pairs were:\n");
            debug_pair (idx);
            debug_pair (mll_pt[idx].succ);
            debug_pair (last_idx);

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
            mll_pt[idx].pred = last_idx;

            DEBUG ("OldHead, tail and OldSecond-NewHead pairs are:\n");
            debug_pair (idx);
            debug_pair (last_idx);
            debug_pair (new_head_idx);

            // Know the second record become the head
            return new_head_idx;

          } else { // (5) firstnode && !(single_node) &&
                 // head && !(tail) && !(complete_tail)

            DEBUG ("CASE 5. Delete the first node in the head when "
                   "tail is incomplete\n");

            DEBUG ("Head, second and tail pairs were:\n");
            debug_pair (idx);
            debug_pair (mll_pt[idx].succ);
            debug_pair (last_idx);

            pair_modify_chain (idx, SKIP, SET_FLAG, SKIP,
                               SKIP, last_idx, CP_CUR_SECD_TO_LAST_SECD);

            mll_pt[last_idx].svid = mll_pt[idx].svid;
            mll_pt[last_idx].smcg = mll_pt[idx].smcg;

            succ_idx = mll_pt[idx].succ;

            mll_pt[succ_idx].pred = mll_pt[idx].pred;

            new_head_idx = mll_pt[idx].succ;

            mll_pt[idx] = nullnode;
            mll_put (idx);

            DEBUG ("MLL pair %d is free.\n", idx);

            DEBUG ("OldHead, tail and OldSecond-NewHead pairs are:\n");
            debug_pair (idx);
            debug_pair (last_idx);
            debug_pair (new_head_idx);

            // Know the second record become the head
            return new_head_idx;
          }
        }
      } else {
        if (tail) { // (6) firstnode && !(single_node) && !(head) && tail

          DEBUG ("CASE 6. Delete the first node in the complete tail\n");

          pair_modify_chain (idx, SET_FLAG, SKIP, CP_SECD_NODE_TO_FIRST_NODE,
                             SKIP, SKIP, SKIP);

          DEBUG ("Pair was:\n");
          debug_pair (idx);

          mll_pt[idx].fvid = mll_pt[idx].svid;
          mll_pt[idx].fmcg = mll_pt[idx].smcg;

          mll_pt[idx].svid = 0;
          mll_pt[idx].smcg = 0;

          DEBUG ("Pair is:\n");
          debug_pair (idx);

          // Chain keep head
          return head_idx;

        } else { // firstnode && !(single_node) && !(head) && !(tail)

          if (complete_tail) { // (7) firstnode && !(single_node) &&
                               // !(head) && !(tail) && complete_tail

            DEBUG ("CASE 7. Delete the first node in regular pair, when "
                   "tail is complete\n");

            w_idx = mll_pt[idx].pred;
            succ_idx = mll_pt[idx].succ;

            DEBUG ("Predecessor, pair, successor and tail were:\n");
            debug_pair (w_idx);
            debug_pair (idx);
            debug_pair (succ_idx);
            debug_pair (last_idx);


            pair_modify_chain (w_idx, SKIP, SKIP, SKIP, succ_idx, SKIP, SKIP);

            mll_pt[w_idx].succ = succ_idx;
            mll_pt[succ_idx].pred = w_idx;

            pair_modify_chain (idx, SET_FLAG, SKIP, CP_SECD_NODE_TO_FIRST_NODE,
                               SKIP, SKIP, SKIP);

            mll_pt[idx].fvid = mll_pt[idx].svid;
            mll_pt[idx].fmcg = mll_pt[idx].smcg;

            mll_pt[idx].svid = 0;
            mll_pt[idx].smcg = 0;

            mll_pt[idx].succ = -1;

            pair_modify_chain (last_idx, SKIP, RESET_FLAG, SKIP, idx, SKIP, SKIP);

            mll_pt[last_idx].succ = idx;
            mll_pt[idx].pred = last_idx;

            DEBUG ("Predecessor, pair, successor and tail is:\n");
            debug_pair (w_idx);
            debug_pair (idx);
            debug_pair (succ_idx);
            debug_pair (last_idx);

            // Chain keep head
            return head_idx;

          } else { // (8) firstnode && !(single_node) &&
                   // !(head) && !(tail) && !(complete_tail)

            DEBUG ("CASE 8. Delete the first node in regular pair, when "
                   "tail is incomplete\n");

            w_idx = mll_pt[idx].pred;
            succ_idx = mll_pt[idx].succ;

            DEBUG ("Predecessor, pair, successor and tail were:\n");
            debug_pair (w_idx);
            debug_pair (idx);
            debug_pair (succ_idx);
            debug_pair (last_idx);

            pair_modify_chain (w_idx, SKIP, SKIP, SKIP, succ_idx, SKIP, SKIP);

            mll_pt[w_idx].succ = succ_idx;

            pair_modify_chain (idx, SKIP, SET_FLAG, SKIP,
                               SKIP, last_idx, CP_CUR_SECD_TO_LAST_SECD);

            mll_pt[last_idx].svid = mll_pt[idx].svid;
            mll_pt[last_idx].smcg = mll_pt[idx].smcg;

            mll_pt[idx] = nullnode;
            mll_put (idx);

            DEBUG ("Predecessor, pair, successor and tail is:\n");
            debug_pair (w_idx);
            debug_pair (idx);
            debug_pair (succ_idx);
            debug_pair (last_idx);

            DEBUG ("MLL pair %d is free.\n", idx);

            // Chain keep head
            return head_idx;

          }
        }
      }
    }
  } else { // !(firstnode)
    if (tail) { // (9) !(firstnode) && tail

      DEBUG ("CASE 9. Delete the second node in the complete tail\n");

      pair_modify_chain (idx, SET_FLAG, SKIP, SKIP, SKIP, SKIP, SKIP);

      DEBUG ("Pair was:\n");
      debug_pair (idx);

      mll_pt[idx].svid = 0;
      mll_pt[idx].smcg = 0;

      DEBUG ("Pair is:\n");
      debug_pair (idx);

      // Chain keep head
      return head_idx;
    } else { // !(firstnode) && !(tail)
      if (head) {
        if (complete_tail) { // (10) !(firstnode) && !(tail) &&
                             // head && complete_tail

          DEBUG ("CASE 10. Delete the second node in the head when "
                 "tail is complete\n");

          DEBUG ("Head and tail were:\n");
          debug_pair (idx);
          debug_pair (last_idx);

          new_head_idx = mll_pt[idx].succ;
          mll_pt[new_head_idx].pred = mll_pt[idx].pred;

          pair_modify_chain (new_head_idx, SKIP, SKIP, SKIP, idx, SKIP, SKIP);
          pair_modify_chain (idx, SET_FLAG, SKIP, SKIP, SKIP, SKIP, SKIP);
          pair_modify_chain (last_idx, SKIP, RESET_FLAG, SKIP, idx, SKIP, SKIP);

          mll_pt[last_idx].succ = idx;
          mll_pt[idx].pred = last_idx;

          mll_pt[idx].svid = 0;
          mll_pt[idx].smcg = 0;
          mll_pt[idx].succ = -1;


          DEBUG ("NewHead, OldTail and OldHead are:\n");
          debug_pair (new_head_idx);
          debug_pair (last_idx);
          debug_pair (idx);

          // Know the second record become the head
          return new_head_idx;

        } else { // (11) !(firstnode) && !(tail) &&
                 // head && !(complete_tail)

          DEBUG ("CASE 11. Delete the second node in the head when "
                 "tail is incomplete\n");

          DEBUG ("Head and tail were:\n");
          debug_pair (idx);
          debug_pair (last_idx);

          pair_modify_chain (idx, SET_FLAG, SKIP, SKIP,
                             SKIP, last_idx, CP_CUR_FRST_TO_LAST_SECD);

          mll_pt[last_idx].svid = mll_pt[idx].fvid;
          mll_pt[last_idx].smcg = mll_pt[idx].fmcg;

          succ_idx = mll_pt[idx].succ;

          mll_pt[succ_idx].pred = mll_pt[idx].pred;

          new_head_idx = mll_pt[idx].succ;

          mll_pt[idx] = nullnode;
          mll_put (idx);

          DEBUG ("NewHead, OldTail and OldHead are:\n");
          debug_pair (new_head_idx);
          debug_pair (last_idx);
          debug_pair (idx);

          DEBUG ("MLL pair %d is free.\n", idx);

          // Know the second record become the head
          return new_head_idx;
        }
      } else { // !(firstnode) && !(tail) && !(head)
        if (complete_tail) { // (12 !(firstnode) && !(tail) &&
                             // !(head) && complete_tail

          DEBUG ("CASE 12. Delete the second node in regular pair, when "
                   "tail is complete\n");

          w_idx = mll_pt[idx].pred;
          succ_idx = mll_pt[idx].succ;

          DEBUG ("Predecessor, pair, successor and tail were:\n");
          debug_pair (w_idx);
          debug_pair (idx);
          debug_pair (succ_idx);
          debug_pair (last_idx);

          pair_modify_chain (w_idx, SKIP, SKIP, SKIP, succ_idx, SKIP, SKIP);

          mll_pt[w_idx].succ = succ_idx;
          mll_pt[succ_idx].pred = w_idx;


          pair_modify_chain (idx, SET_FLAG, SKIP, SKIP, SKIP, SKIP, SKIP);

          mll_pt[idx].svid = 0;
          mll_pt[idx].smcg = 0;
          mll_pt[idx].succ = -1;

          pair_modify_chain (last_idx, SKIP, RESET_FLAG, SKIP, idx, SKIP, SKIP);

          mll_pt[last_idx].succ = idx;
          mll_pt[idx].pred = last_idx;

          DEBUG ("Predecessor, pair, successor and tail is:\n");
          debug_pair (w_idx);
          debug_pair (idx);
          debug_pair (succ_idx);
          debug_pair (last_idx);

          // Chain keep head
          return head_idx;

        } else { // (13 !(firstnode) && !(tail) &&
                 // !(head) && complete_tail

          DEBUG ("CASE 13. Delete the second node in regular pair, when "
                 "tail is incomplete\n");

          w_idx = mll_pt[idx].pred;
          succ_idx = mll_pt[idx].succ;

          DEBUG ("Predecessor, pair, successor and tail were:\n");
          debug_pair (w_idx);
          debug_pair (idx);
          debug_pair (succ_idx);
          debug_pair (last_idx);

          pair_modify_chain (w_idx, SKIP, SKIP, SKIP, succ_idx, SKIP, SKIP);

          mll_pt[w_idx].succ = succ_idx;
          mll_pt[succ_idx].pred = w_idx;

          pair_modify_chain (idx, SKIP, SKIP, SKIP,
                             SKIP, last_idx, CP_CUR_FRST_TO_LAST_SECD);

          mll_pt[last_idx].svid = mll_pt[idx].fvid;
          mll_pt[last_idx].smcg = mll_pt[idx].fmcg;

          mll_pt[idx] = nullnode;
          mll_put (idx);

          DEBUG ("Predecessor, pair, successor and tail is:\n");
          debug_pair (w_idx);
          debug_pair (idx);
          debug_pair (succ_idx);
          debug_pair (last_idx);

          DEBUG ("MLL pair %d is free.\n", idx);

          // Chain keep head
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
  DEBUG ("Deleting node vlan %d mcg %d from chain %d...", vid, mcg, head_idx);
  int idx = find_pair (head_idx, mcg, vid);

  if (idx == -1) {
    DEBUG ("Such node does not found.\n");
    return -3; //there is no such node
  }
  else {
    return do_del_node (idx, mcg, vid, head_idx);
  }
}
