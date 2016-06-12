#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIpTypes.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>

#include <control-proto.h>
#include <nht.h>
#include <ret.h>
#include <arpc.h>
#include <port.h>
#include <vif.h>
#include <route.h>
#include <debug.h>
#include <route-p.h>
#include <dev.h>
#include <sysdeps.h>
#include <mll.h>
#include <utils.h>

#include <uthash.h>


#define MAX_RE (4096 - FIRST_REGULAR_RE_IDX)

struct stack {
  int sp;
  uint16_t data[MAX_RE];
};

static struct stack res;

static inline int
res_pop (void)
{
  if (res.sp >= MAX_RE - 1)
    return -1;

  return res.data[res.sp++];
}

static inline int
res_push (uint16_t re)
{
  if (res.sp == 0)
    return -1;

  res.data[--res.sp] = re;
  return 0;
}


struct re {
  struct gw gw;
  int valid;
  int def;
  uint16_t idx;
  GT_ETHERADDR addr;
  uint16_t nh_idx;
  vif_id_t vif_id;
  int refc;
  UT_hash_handle hh;
};
static struct re *ret = NULL;
static int re_cnt = 0;

int
ret_add (const struct gw *gw, int def)
{
  struct re *re;

  HASH_FIND_GW (ret, gw, re);
  if (re) {
    ++re->refc;
    if (def) {
      re->def = 1;
      if (re->valid) {
        CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
//        struct port *port = port_ptr (re->pid);
        struct vif *vif = vif_getn (re->vif_id);
        int d;

        memset (&rt, 0, sizeof (rt));
        rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
        rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_ROUTE_E;
        vif->fill_cpss_if(vif, &rt.entry.regularEntry.nextHopInterface);
//        rt.entry.regularEntry.nextHopInterface.type = CPSS_INTERFACE_PORT_E;
//        rt.entry.regularEntry.nextHopInterface.devPort.devNum = phys_dev (port->ldev);
//        rt.entry.regularEntry.nextHopInterface.devPort.portNum = port->lport;
        rt.entry.regularEntry.nextHopARPPointer = re->nh_idx;
        rt.entry.regularEntry.nextHopVlanId = gw->vid;
        DEBUG ("write default route entry\r\n");
        for_each_dev (d)
          CRP (cpssDxChIpUcRouteEntriesWrite (d, DEFAULT_UC_RE_IDX, &rt, 1));
      }
    }
    goto out;
  }

  if (re_cnt >= MAX_RE)
    return ST_BAD_VALUE; /* FIXME: add overflow status value. */

  re = calloc (1, sizeof (*re));
  re->gw = *gw;
  re->refc = 1;
  re->def = def;
  HASH_ADD_GW (ret, gw, re);
  ++re_cnt;

  arpc_request_addr (gw);

 out:
  DEBUG ("refc = %d\r\n", re->refc);

  if (re->valid)
    return re->idx;

  return -1;
}

enum status
ret_unref (const struct gw *gw, int def)
{
  struct re *re;

  HASH_FIND_GW (ret, gw, re);
  if (!re)
    return ST_DOES_NOT_EXIST;

  if (def) {
    re->def = 0;
    if (re->valid) {
      CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
      int d;

      DEBUG ("reset default route entry");
      memset (&rt, 0, sizeof (rt));
      rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
      rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_DROP_HARD_E;
      for_each_dev (d)
        CRP (cpssDxChIpUcRouteEntriesWrite (d, DEFAULT_UC_RE_IDX, &rt, 1));
    }
  }

  if (--re->refc == 0) {
    DEBUG ("last ref to " GW_FMT " dropped, deleting\r\n", GW_FMT_ARGS (gw));
    HASH_DEL (ret, re);
    if (re->valid) {
      res_push (re->idx);
      nht_unref (&re->addr);
    }
    arpc_release_addr (gw);
    free (re);
    --re_cnt;
  }
  DEBUG ("refc = %d\r\n", re->refc);

  return ST_OK;
}

enum status
ret_set_mac_addr (const struct gw *gw, const GT_ETHERADDR *addr, vif_id_t vif_id)
{
  struct re *re;
  int idx, nh_idx, d;
  CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
  GT_STATUS rc;
  struct vif *vif = vif_getn (vif_id);

  if (!vif)
    return ST_HEX;

  HASH_FIND_GW (ret, gw, re);
  if (!re)
    return ST_DOES_NOT_EXIST;

  if (re->valid && !memcmp (&re->addr, addr, sizeof (*addr))) {
    memset (&rt, 0, sizeof (rt));
    rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_ROUTE_E;
    vif->fill_cpss_if(vif, &rt.entry.regularEntry.nextHopInterface);
//    rt.entry.regularEntry.nextHopInterface.type = CPSS_INTERFACE_PORT_E;
//    rt.entry.regularEntry.nextHopInterface.devPort.devNum = phys_dev (port->ldev);
//    rt.entry.regularEntry.nextHopInterface.devPort.portNum = port->lport;
    rt.entry.regularEntry.nextHopARPPointer = re->nh_idx;
    rt.entry.regularEntry.nextHopVlanId = gw->vid;
    DEBUG ("write route entry");
    for_each_dev (d)
      rc = CRP (cpssDxChIpUcRouteEntriesWrite (d, re->idx, &rt, 1));
    if (rc != ST_OK)
      return ST_HEX;

    if (re->def) {
      DEBUG ("write default route entry");
      for_each_dev (d)
        CRP (cpssDxChIpUcRouteEntriesWrite (d, DEFAULT_UC_RE_IDX, &rt, 1));
    }

    return ST_OK;
  }

  memcpy (&re->addr, addr, sizeof (*addr));
  re->vif_id = vif_id;

  nh_idx = nht_add (addr);
  if (nh_idx < 0)
    return ST_HEX;

  idx = res_pop ();
  if (idx < 0)
    return ST_BAD_VALUE; /* FIXME: add overflow status value. */

  memset (&rt, 0, sizeof (rt));
  rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
  rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_ROUTE_E;
  vif->fill_cpss_if(vif, &rt.entry.regularEntry.nextHopInterface);
//  rt.entry.regularEntry.nextHopInterface.type = CPSS_INTERFACE_PORT_E;
//  rt.entry.regularEntry.nextHopInterface.devPort.devNum = phys_dev (port->ldev);
//  rt.entry.regularEntry.nextHopInterface.devPort.portNum = port->lport;
  rt.entry.regularEntry.nextHopARPPointer = nh_idx;
  rt.entry.regularEntry.nextHopVlanId = gw->vid;
  DEBUG ("write route entry at %d\r\n", idx);
  for_each_dev (d)
    rc = CRP (cpssDxChIpUcRouteEntriesWrite (d, idx, &rt, 1));

  if (re->def) {
    DEBUG ("write default route entry\r\n");
    for_each_dev (d)
      CRP (cpssDxChIpUcRouteEntriesWrite (d, DEFAULT_UC_RE_IDX, &rt, 1));
  }

  re->idx = idx;
  memcpy (&re->addr, addr, sizeof (*addr));
  re->nh_idx = nh_idx;
  re->valid = 1;

  route_update_table (gw, idx);

  return ST_OK;
}

enum status
ret_init (void)
{
  uint16_t n;

  nht_init ();

  DEBUG ("populate RE stack");
  for (n = 0; n < MAX_RE; n++)
    res.data[n] = n + FIRST_REGULAR_RE_IDX;
  res.sp = 0;

  return ST_OK;
}

/*
 * Multicast.
 */

struct mcre_key {
  GT_U8 dst[4];
  GT_U8 src[4];
  vid_t src_vid;
};

struct mcre {
  struct mcre_key key;
  int refc;
  int idx;
  int mll_idx;
  UT_hash_handle hh;
};

static struct mcre_key mcrek, mcre_key_bp[4096];

static inline int
mcre_key (struct mcre *re , const uint8_t *dst, const uint8_t *src,
          vid_t src_vid)
{
  memcpy (&(re->key.dst), dst, sizeof (re->key.dst));
  memcpy (&(re->key.src), src, sizeof (re->key.src));
  memcpy (&(re->key.src_vid), &src_vid, sizeof (re->key.src_vid));
  return 0;
}

static struct mcre *mcret;

static struct mcre *
mcre_new (const uint8_t *dst, const uint8_t *src, mcg_t mcg, vid_t vid,
          vid_t src_vid)
{
  struct mcre *re;
  int idx, mll_idx;
  CPSS_DXCH_IP_MC_ROUTE_ENTRY_STC c;

  DEBUG ("Popping new idx...\n");

  idx = res_pop ();
  if (idx == -1)
    goto err;

  DEBUG ("New idx = %d. Adding first node\n", idx);

  mll_idx = add_node (-idx, mcg, vid);
  if (mll_idx == -1)
    goto err_res;

  DEBUG ("New mll chain is %d. Src for it is %d\n", mll_idx, src_vid),

  memset (&c, 0, sizeof (c));
  c.cmd = CPSS_PACKET_CMD_ROUTE_E;
  c.cpuCodeIdx = CPSS_DXCH_IP_CPU_CODE_IDX_0_E;
  c.appSpecificCpuCodeEnable = GT_FALSE;
  c.ttlHopLimitDecEnable = GT_FALSE;
  c.ttlHopLimDecOptionsExtChkByPass = GT_TRUE;
  c.ingressMirror = GT_FALSE;
  c.qosProfileMarkingEnable = GT_FALSE;
  c.qosProfileIndex = 0;
  c.qosPrecedence = CPSS_PACKET_ATTRIBUTE_ASSIGN_PRECEDENCE_SOFT_E;
  c.modifyUp = CPSS_PACKET_ATTRIBUTE_MODIFY_KEEP_PREVIOUS_E;
  c.modifyDscp = CPSS_PACKET_ATTRIBUTE_MODIFY_KEEP_PREVIOUS_E;
  c.countSet = CPSS_IP_CNT_SET3_E;
  c.multicastRPFCheckEnable = GT_TRUE;
  c.multicastRPFVlan = src_vid;
  c.multicastRPFFailCommandMode =
    CPSS_DXCH_IP_MULTICAST_ROUTE_ENTRY_RPF_FAIL_COMMAND_MODE_E;
  c.RPFFailCommand = CPSS_PACKET_CMD_DROP_SOFT_E;
  c.scopeCheckingEnable = GT_FALSE;
  c.siteId = CPSS_IP_SITE_ID_INTERNAL_E;
  c.mtuProfileIndex = 0;
  c.internalMLLPointer = mll_idx;
  c.externalMLLPointer = 0;

  ON_GT_ERROR (CRP (cpssDxChIpMcRouteEntriesWrite (0, idx, &c)))
    goto err_mll;

  re = calloc (1, sizeof (*re));
  re->idx = idx;
  re->mll_idx = mll_idx;

  memcpy (&(mcre_key_bp[idx].dst), dst, sizeof (mcre_key_bp[idx].dst));
  memcpy (&(mcre_key_bp[idx].src), src, sizeof (mcre_key_bp[idx].src));
  memcpy (&(mcre_key_bp[idx].src_vid), &src_vid,
          sizeof (mcre_key_bp[idx].src_vid));

  DEBUG ("Re created. RE: %d, MLL: %d\n", idx, mll_idx);

  return re;

 err_mll:
  mll_put (mll_idx);
 err_res:
  res_push (idx);
 err:
  return NULL;
}

static void
mcre_del (struct mcre *re)
{
  HASH_DEL (mcret, re);

  DEBUG ("Delete mcre. MLL: %d, RE: %d\n", re->mll_idx, re->idx);

  mll_put (re->mll_idx);
  res_push (re->idx);

  free (re);
}

int
mcre_find (const uint8_t *dst, const uint8_t *src, vid_t src_vid)
{
  struct mcre re_key, *re;
  mcre_key (&re_key, dst, src, src_vid);

  HASH_FIND (hh, mcret, &re_key.key, sizeof (struct mcre_key), re);
  if (!re) {
      return -1;
  }

  return re->idx;
}

int
mcre_create (const uint8_t *dst, const uint8_t *src, mcg_t mcg, vid_t vid,
             vid_t src_vid)
{
  struct mcre re_key, *re;
  mcre_key (&re_key, dst, src, src_vid);


  re = mcre_new (dst, src, mcg, vid, src_vid);
  if (!re)
    return -1;

  re->key = re_key.key;
  re->refc = 1;
  HASH_ADD (hh, mcret, key, sizeof (struct mcre_key), re);

  DEBUG ("New Route entry idx = %d was added ho hash\n", re->idx);

  return re->idx;
}

int
mcre_add_node (int idx, mcg_t mcg, vid_t vid)
{
  memcpy (&(mcrek.dst), &mcre_key_bp[idx].dst, sizeof (mcrek.dst));
  memcpy (&(mcrek.src), &mcre_key_bp[idx].src, sizeof (mcrek.src));
  memcpy (&(mcrek.src_vid), &mcre_key_bp[idx].src_vid, sizeof (mcrek.src_vid));

  struct mcre *re, *upd_re;

  upd_re = calloc (1, sizeof (*upd_re));

  DEBUG ("Looking for re of group %d.%d.%d.%d vlan %d\n",
         mcrek.dst[0], mcrek.dst[1], mcrek.dst[2], mcrek.dst[3],
         mcrek.src_vid);

  HASH_FIND (hh, mcret, &mcrek, sizeof (struct mcre_key), re);

  *upd_re = *re;

  int res, mll_head = re->mll_idx;

  DEBUG ("Re found. Chain head is %d.\n", mll_head),

  res = add_node (mll_head, mcg, vid);

  upd_re->refc++;

  DEBUG ("Chain %d now has %d nodes.\n", mll_head, upd_re->refc);

  HASH_DEL (mcret, re);
  HASH_ADD (hh, mcret, key, sizeof (struct mcre_key), upd_re);

  free (re);

  return 0;
}

int
mcre_put (const uint8_t *dst, const uint8_t *src, vid_t src_vid)
{
  struct mcre re_key, *re;
  mcre_key (&re_key, dst, src, src_vid);

  HASH_FIND (hh, mcret, &re_key.key, sizeof (struct mcre_key), re);
  if (!re)
    return -1;

  re->refc--;
  if (!re->refc) {
    mcre_del (re);
    return 0;
  }

  return re->refc;
}

int
mcre_del_node (int idx, mcg_t via, vid_t vid, vid_t src_vid)
{
  struct mcre *re, *upd_re;

  memcpy (&(mcrek.dst), &mcre_key_bp[idx].dst, sizeof (mcrek.dst));
  memcpy (&(mcrek.src), &mcre_key_bp[idx].src, sizeof (mcrek.dst));
  memcpy (&(mcrek.src_vid), &mcre_key_bp[idx].src_vid, sizeof (mcrek.src_vid));

  DEBUG ("Finding idx of group %d.%d.%d.%d of src_vid %d\n",
         mcrek.dst[0], mcrek.dst[1], mcrek.dst[2], mcrek.dst[3],
         src_vid);

  HASH_FIND (hh, mcret, &mcrek, sizeof (struct mcre_key), re);
  if (!re)
    return -1;

  int head, new_head;

  head = re->mll_idx;

  DEBUG ("Idx found. Chain head is %d. Deleting node...\n", head);

  new_head = del_node (head, via, vid);

  DEBUG ("New head is %d\n", new_head);

  if (new_head == -2) {// There is no more chain

    DEBUG ("There is no more chain\n");

    HASH_DEL (mcret, re);

    res_push (re->idx);

    free (re);

    return 0;
  } else {
    if (new_head == -3) { //No such node
      DEBUG ("Node was not found!\n");
      return re->refc;
    } else {
      if (new_head != head) {

        CPSS_DXCH_IP_MC_ROUTE_ENTRY_STC c;

        CRP (cpssDxChIpMcRouteEntriesRead (0, idx, &c));

        c.internalMLLPointer = new_head;

        CRP (cpssDxChIpMcRouteEntriesWrite (0, idx, &c));

          upd_re = calloc (1, sizeof (*upd_re));
          *upd_re = *re;
          upd_re->refc--;
          upd_re->mll_idx = new_head;

          HASH_DEL (mcret, re);
          HASH_ADD (hh, mcret, key, sizeof (struct mcre_key), upd_re);

          free (re);

        DEBUG ("Change head from %d to %d! Nodes left: %d\n",
               head, new_head, upd_re->refc);

        return upd_re->refc;

      } else {// new_head == head

        upd_re = calloc (1, sizeof (*upd_re));
        *upd_re = *re;
        upd_re->refc--;

        HASH_DEL (mcret, re);
        HASH_ADD (hh, mcret, key, sizeof (struct mcre_key), upd_re);

        free (re);

        DEBUG ("Head remain the same. Nodes left: %d\n", upd_re->refc);

        return upd_re->refc;
      }
    }
  }

  // We should not get here
  return -2;
}

void
ret_dump(void) {
  struct re *s, *t;
  DEBUG("!!!! RET DUMP %d  !!!!\n", re_cnt);
  DEBUG("gw IP                 :vid,\tvalid, def,idx, MAC                    ,nh_idx, vifid, refc\n");
  HASH_ITER (hh, ret, s, t) {
    DEBUG(IPv4_FMT ":%3d,\t%d, %d, %3d, " MAC_FMT ", %3d, %08x, %03d\n",
        IPv4_ARG(s->gw.addr.arIP), s->gw.vid, s->valid, s->def, s->idx,
        MAC_ARG(s->addr.arEther), s->nh_idx, s->vif_id, s->refc);
  }
  DEBUG("!!!! end RET DUMP!!!!\n\n");
/*
struct re {
  struct gw gw;
  int valid;
  int def;
  uint16_t idx;
  GT_ETHERADDR addr;
  uint16_t nh_idx;
  vif_id_t vif_id;
  int refc;
  UT_hash_handle hh;
};
*/
}
