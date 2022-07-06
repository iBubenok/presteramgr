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
#include <ret_group.h>
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
#include <utlist.h>
#include <time.h>


#define MAX_RE (4096 - FIRST_REGULAR_RE_IDX)

struct stack {
  int sp;
  uint16_t data[MAX_RE];
};

static struct stack res;

struct list_int *ret_indexes_used = NULL;

int ret_list_int_cmp(struct list_int *a, struct list_int *b) {
  if (a->val == b->val) return 0;
  if (a->val > b->val)  return 1;
  return -1;
};

bool_t
res_pop (IN int count, OUT int *ids)
{
  int i;
  int _count = 0;
  int _ids[8];
  struct list_int *out, elt, *add;

  if (!(0 < count && count < 9)) return false;
  if (ids == NULL) return false;

  for(i = FIRST_REGULAR_RE_IDX; i < 4096; i++) {
    elt.val = i;
    DL_SEARCH(ret_indexes_used, out, &elt, ret_list_int_cmp);
    if (!out) {
      _ids[_count] = elt.val;
      _count++;
      if (count == _count) break;
    }
    else {
      _count = 0;
    }
  }

  if (count != _count) return false;

  for (i = 0; i < _count; i++) {
    add = calloc (1, sizeof(struct list_int));
    add->val = _ids[i];
    ids[i] = _ids[i];
    DL_APPEND(ret_indexes_used, add);
  }
  return true;
}

bool_t
res_push (IN int count, OUT int *ids)
{
  int i;
  struct list_int *out_del, elt;
  if (!(0 < count && count < 9)) return false;
  if (ids == NULL) return false;

  for (i = 0; i < count; i++) {
    elt.val = ids[i];
    DL_SEARCH(ret_indexes_used, out_del, &elt, ret_list_int_cmp);
    if (out_del) {
      DL_DELETE(ret_indexes_used, out_del);
      free(out_del);
    }
  }
  return true;
}

bool_t res_push_count_first(int count, int first) {
  if (!(0 < count && count < 9)) return false;
  // if (ids == NULL) return false;
  int _ids[8];
  int i = 0;
  for (i = 0; i < count; i++) {
    _ids[i] = first + i;
  }

  return res_push(count, _ids);
}


struct re {
  struct gw gw;
  int valid;
  int def;
  uint16_t idx;
  GT_ETHERADDR addr;
  uint16_t nh_idx;
  vif_id_t vif_id;
  struct list_int *group_ids;
  int refc;
  UT_hash_handle hh;
};

struct re_ipv6 {
  struct gw_v6 gw;
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
static struct re_ipv6 *ret_ipv6 = NULL;
static int re_cnt = 0;


void ret_add_group_to_list(struct list_int **head, int group_id) {
  struct list_int *out, elt, *add;
  elt.val = group_id;
  DL_SEARCH(*head, out, &elt, ret_list_int_cmp);
  if (!out) {
    add = calloc(1, sizeof(struct list_int));
    add->val = group_id;
    DL_APPEND(*head,add);
  }
}

struct re *
ret_add (const struct gw *gw, int group_id)
{

  struct re *re;

  HASH_FIND_GW (ret, gw, re);
  if (re) {
    ret_add_group_to_list(&re->group_ids, group_id);
    goto out;
  }


  re = calloc (1, sizeof (*re));
  re->gw = *gw;
  re->refc = 1;
  ret_add_group_to_list(&re->group_ids, group_id);
  HASH_ADD_GW (ret, gw, re);

  ++re_cnt;



 out:
  DEBUG ("refc = %d\r\n", re->refc);



  return re;
}

void ret_arpc_request_addr(struct gw *gw){
  arpc_request_addr (gw);
}

int
ret_ipv6_add (const struct gw_v6 *gw, int def, struct gw_v6 *ret_key)
{
  DEBUG("sbelo ret_ipv6_add\n");
  struct re_ipv6 *re;

  HASH_FIND_GW (ret_ipv6, gw, re);
  if (re) {
    if (!ret_key->addr.u32Ip || !ret_key->vid) {
      ++re->refc;
      memcpy(ret_key, gw, sizeof(struct gw));
    }
    if (def) {
      re->def = 1;
      if (re->valid) {
        CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
//        struct port *port = port_ptr (re->pid);
        struct vif *vif = vif_getn (re->vif_id);
        if (!vif)
          return re->idx;
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
        rt.entry.regularEntry.ttlHopLimitDecEnable = GT_TRUE;
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
  HASH_ADD_GW (ret_ipv6, gw, re);
  memcpy(ret_key, gw, sizeof(struct gw_v6));
  ++re_cnt;

  ndpc_request_addr(gw);

 out:
  DEBUG ("refc = %d\r\n", re->refc);

  if (re->valid)
    return re->idx;

  return -1;
}

void ret_remove_group_from_list(struct list_int **head, int group_id) {
  struct list_int *out_del, elt;
  elt.val = group_id;
  DL_SEARCH(*head, out_del, &elt, ret_list_int_cmp);
  if (out_del) {
    DL_DELETE(*head,out_del);
    free(out_del);
  }
}

enum status
ret_unref (const struct gw *gw, int group_id)
{
  struct re *re;

  HASH_FIND_GW (ret, gw, re);
  if (!re)
    return ST_DOES_NOT_EXIST;

  ret_remove_group_from_list(&re->group_ids, group_id);



  if (!re->group_ids) {
    DEBUG ("last ref to " GW_FMT " dropped, deleting\r\n", GW_FMT_ARGS (gw));
    HASH_DEL (ret, re);
    if (re->valid) {
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
ret_ipv6_unref (const struct gw_v6 *gw, int def)
{
  struct re_ipv6 *re;

  HASH_FIND_GW (ret_ipv6, gw, re);
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
    HASH_DEL (ret_ipv6, re);
    if (re->valid) {
      int idx = re->idx;
      res_push (1, &idx);
      nht_unref (&re->addr);
    }
    ndpc_release_addr (gw);
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
  int nh_idx;
  struct list_int *group_id;
  struct vif *vif = vif_getn (vif_id);

  if (!vif) {
    return ST_HEX;
  }
  HASH_FIND_GW (ret, gw, re);
  if (!re) {
    struct re *el, *tmp;
    HASH_ITER(hh,ret,el,tmp) {
      if (!memcmp(&el->gw, gw, sizeof(*gw))) {
        re = el;
      }
      if (el->gw.addr.u32Ip == gw->addr.u32Ip && el->gw.vid == gw->vid) {
        re = el;
      }
    }
  }

  if (!re) {
    return ST_DOES_NOT_EXIST;
  }



  if (re->valid
      && !memcmp (&re->addr, addr, sizeof (*addr))
      && re->vif_id == vif_id)
  {
    DEBUG("no changed\n");
  }
  else {
    // gw changed
    bool_t is_changed_valid = false;

    if (!re->valid) {
      is_changed_valid = true;
    }
    re->valid = 1;
    memcpy (&re->addr, addr, sizeof (*addr));
    re->vif_id = vif_id;

    nh_idx = nht_add (addr);
    if (nh_idx < 0) {
      return ST_HEX;
    }
    re->nh_idx = nh_idx;
    DL_FOREACH(re->group_ids, group_id) {
      ret_group_gw_changed(group_id->val, re, is_changed_valid);
    }
  }
  return ST_OK;
}

enum status
ret_unset_mac_addr (const struct gw *gw) {
  struct re *re;
  struct list_int *group_id;
  HASH_FIND_GW (ret, gw, re);



  if (!re) {

    return ST_DOES_NOT_EXIST;
  }

  if (re->valid) {
    bool_t is_changed_valid = false;
    if (re->valid) {
      is_changed_valid = true;
    }

    re->valid = false;
    nht_unref(&re->addr);
    DL_FOREACH(re->group_ids, group_id) {
      ret_group_gw_changed(group_id->val, re, is_changed_valid);
    }


  }
  else {
    DEBUG("ret_unset_mac_addr\n");
  }


  return ST_OK;
}

enum status
ret_ipv6_set_mac_addr (const struct gw_v6 *gw, const GT_ETHERADDR *addr, vif_id_t vif_id)
{
  struct re_ipv6 *re;
  int idx;
  int nh_idx;
  int d;
  CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
  GT_STATUS rc;
  struct vif *vif = vif_getn (vif_id);

  if (!vif)
    return ST_HEX;

  HASH_FIND_GW (ret_ipv6, gw, re);
  if (!re) {
    return ST_DOES_NOT_EXIST;
  }

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
    rt.entry.regularEntry.ttlHopLimitDecEnable = GT_TRUE;

    DEBUG ("write route entry");
    for_each_dev (d)
      rc = CRP (cpssDxChIpUcRouteEntriesWrite (d, re->idx, &rt, 1));
    if (rc != ST_OK) {
      return ST_HEX;
    }

    re->vif_id = vif_id;

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

  // idx = res_pop ();
  if (!res_pop (1, &idx))
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
  rt.entry.regularEntry.ttlHopLimitDecEnable = GT_TRUE;

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

  route_ipv6_update_table (gw, idx);

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
  int idx, mll_idx, d;
  CPSS_DXCH_IP_MC_ROUTE_ENTRY_STC c;
  GT_STATUS rc;

  DEBUG ("Popping new idx...\n");

  // idx = res_pop ();
  if (!res_pop (1, &idx))
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

  for_each_dev (d)
    rc = CRP (cpssDxChIpMcRouteEntriesWrite (d, idx, &c));
  ON_GT_ERROR (rc)
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
  res_push (1, &idx);
 err:
  return NULL;
}

static void
mcre_del (struct mcre *re)
{
  HASH_DEL (mcret, re);

  DEBUG ("Delete mcre. MLL: %d, RE: %d\n", re->mll_idx, re->idx);

  mll_put (re->mll_idx);
  res_push (1, &re->idx);

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
  int d;

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

    res_push (1, &re->idx);

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

        for_each_dev (d)
          CRP (cpssDxChIpMcRouteEntriesWrite (d, idx, &c));

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
ret_clear_devs_res(devsbmp_t dbmp) {
DEBUG(">>>>ret_clear_devs_res(%x)", dbmp);
  struct re *s, *t;
  struct list_route_pfx_pbr *pfx_pbr = NULL, *pfx_pbr_el = NULL, *pfx_pbr_tmp = NULL;
  struct list_int *el = NULL;
  struct re_group *group = NULL;
  HASH_ITER (hh, ret, s, t) {
    if (in_range (((struct vif_id *)&s->vif_id)->type, VIFT_FE, VIFT_XG)
        && ((1 << ((struct vif_id *)&s->vif_id)->dev) & dbmp)) {
      DEBUG("deleting re:\n");
      DEBUG(IPv4_FMT ":%3d,\t%d, %d, %3d, " MAC_FMT ", %3d, %08x, %03d\n",
          IPv4_ARG(s->gw.addr.arIP), s->gw.vid, s->valid, s->def, s->idx,
          MAC_ARG(s->addr.arEther), s->nh_idx, s->vif_id, s->refc);
      // route_reset_prefixes4gw (&s->gw);
      DL_FOREACH(s->group_ids, el) {
        group = ret_group_get(el->val);
        if (group) {
          ret_group_copy_pfx_pbr(&pfx_pbr, group);
        }
      }
    }
  }


  DL_FOREACH_SAFE(pfx_pbr, pfx_pbr_el, pfx_pbr_tmp) {
    if (pfx_pbr_el->val.type == ROUTE_PFX) {
      route_reset_prefixes4gw(pfx_pbr_el->val.data.pfx.addr.u32Ip, pfx_pbr_el->val.data.pfx.alen);
    }
    DL_DELETE(pfx_pbr, pfx_pbr_el);
    free(pfx_pbr_el);
  }

}

int ret_get_valid(const struct re *re){
  return re->valid;
}

void ret_set_valid(struct re *re, int valid){
  re->valid = valid;
}

struct gw *ret_get_gw(const struct re *re) {
  return (struct gw *)&re->gw;
}


void
ret_dump(void) {
  struct re *s, *t;
  struct re_ipv6 *s_v6, *t_v6;
  DEBUG("!!!! RET DUMP %d  !!!!\n", re_cnt);
  DEBUG("gw IP        :vid,\tvalid, def,idx, MAC                    ,nh_idx, vifid, refc\n");
  HASH_ITER (hh, ret, s, t) {
    DEBUG(IPv4_FMT ":%3d,\t%d, %d, %3d, " MAC_FMT ", %3d, %08x, %03d\n",
        IPv4_ARG(s->gw.addr.arIP), s->gw.vid, s->valid, s->def, s->idx,
        MAC_ARG(s->addr.arEther), s->nh_idx, s->vif_id, s->refc);
  }
  DEBUG("!!!! end RET DUMP!!!!\n\n");

  DEBUG("!!!! RET DUMP IPV6 %d  !!!!\n", re_cnt);
  DEBUG("gw IP        :vid,\tvalid, def,idx, MAC                    ,nh_idx, vifid, refc\n");
  HASH_ITER (hh, ret_ipv6, s_v6, t_v6) {
    DEBUG(IPv6_FMT ":%3d,\t%d, %d, %3d, " MAC_FMT ", %3d, %08x, %03d\n",
        IPv6_ARG(s_v6->gw.addr.arIP), s_v6->gw.vid, s_v6->valid, s_v6->def, s_v6->idx,
        MAC_ARG(s_v6->addr.arEther), s_v6->nh_idx, s_v6->vif_id, s_v6->refc);
  }
  DEBUG("!!!! end RET DUMP IPV6!!!!\n\n");
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

void ret_set_re_to_idx(const struct re *re, int idx) {
  // if (re == NULL) return;
  int d;
  CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
  GT_STATUS rc;

  struct vif *vif = vif_getn (re->vif_id);
  if (!vif)
    return;

  memset (&rt, 0, sizeof (rt));
  rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
  rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_ROUTE_E;
  vif->fill_cpss_if(vif, &rt.entry.regularEntry.nextHopInterface);
  rt.entry.regularEntry.nextHopARPPointer = re->nh_idx;
  rt.entry.regularEntry.nextHopVlanId = re->gw.vid;
  rt.entry.regularEntry.ttlHopLimitDecEnable = GT_TRUE;

  for_each_dev (d)
    rc = CRP (cpssDxChIpUcRouteEntriesWrite (d, idx, &rt, 1));
}

void ret_set_re_to_idx_gw(const struct gw *gw, int idx) {
  struct re *re = NULL;

  HASH_FIND_GW (ret, gw, re);
  if (re) {
    ret_set_re_to_idx(re, idx);
  }
}
