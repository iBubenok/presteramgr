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
  port_id_t pid;
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
        struct port *port = port_ptr (re->pid);
        int d;

        memset (&rt, 0, sizeof (rt));
        rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
        rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_ROUTE_E;
        rt.entry.regularEntry.nextHopInterface.type = CPSS_INTERFACE_PORT_E;
        rt.entry.regularEntry.nextHopInterface.devPort.devNum = phys_dev (port->ldev);
        rt.entry.regularEntry.nextHopInterface.devPort.portNum = port->lport;
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
ret_set_mac_addr (const struct gw *gw, const GT_ETHERADDR *addr, port_id_t pid)
{
  struct re *re;
  int idx, nh_idx, d;
  CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
  GT_STATUS rc;
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_HEX;

  HASH_FIND_GW (ret, gw, re);
  if (!re)
    return ST_DOES_NOT_EXIST;

  if (re->valid && !memcmp (&re->addr, addr, sizeof (*addr))) {
    memset (&rt, 0, sizeof (rt));
    rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_ROUTE_E;
    rt.entry.regularEntry.nextHopInterface.type = CPSS_INTERFACE_PORT_E;
    rt.entry.regularEntry.nextHopInterface.devPort.devNum = phys_dev (port->ldev);
    rt.entry.regularEntry.nextHopInterface.devPort.portNum = port->lport;
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
  re->pid = pid;

  nh_idx = nht_add (addr);
  if (nh_idx < 0)
    return ST_HEX;

  idx = res_pop ();
  if (idx < 0)
    return ST_BAD_VALUE; /* FIXME: add overflow status value. */

  memset (&rt, 0, sizeof (rt));
  rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
  rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_ROUTE_E;
  rt.entry.regularEntry.nextHopInterface.type = CPSS_INTERFACE_PORT_E;
  rt.entry.regularEntry.nextHopInterface.devPort.devNum = phys_dev (port->ldev);
  rt.entry.regularEntry.nextHopInterface.devPort.portNum = port->lport;
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

struct mcre {
  int key;
  int refc;
  int idx;
  int mll_idx;
  UT_hash_handle hh;
};

static inline int
mcre_key (mcg_t mcg, vid_t vid)
{
  int m = mcg & 0xFFFF, v = vid & 0xFFFF;
  return (m << 16) | v;
}

static struct mcre *mcret;

struct mcre_bp {
  mcg_t mcg;
  vid_t vid;
};

static struct mcre_bp mcre_bp[4096];

static int
mcre_mll_get (mcg_t mcg, vid_t vid)
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

  idx = mll_get ();
  if (idx == -1)
    return -1;

  ON_GT_ERROR
    (CRP (cpssDxChIpMLLPairWrite
          (0, idx, CPSS_DXCH_IP_MLL_PAIR_READ_WRITE_WHOLE_E, &p))) {
    mll_put (idx);
    return -1;
  }

  return idx;
}

static struct mcre *
mcre_new (mcg_t mcg, vid_t vid)
{
  struct mcre *re;
  int idx, mll_idx;
  CPSS_DXCH_IP_MC_ROUTE_ENTRY_STC c;

  idx = res_pop ();
  if (idx == -1)
    goto err;

  mll_idx = mcre_mll_get (mcg, vid);
  if (mll_idx == -1)
    goto err_res;

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
  c.multicastRPFCheckEnable = GT_FALSE;
  c.multicastRPFVlan = 0;
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

  mcre_bp[idx].mcg = mcg;
  mcre_bp[idx].vid = vid;

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

  mll_put (re->mll_idx);
  res_push (re->idx);

  free (re);
}

int
mcre_get (mcg_t mcg, vid_t vid)
{
  struct mcre *re;
  int key = mcre_key (mcg, vid);

  HASH_FIND_INT (mcret, &key, re);
  if (!re) {
    re = mcre_new (mcg, vid);
    if (!re)
      return -1;

    re->key = key;
    re->refc = 1;
    HASH_ADD_INT (mcret, key, re);
  }

  return re->idx;
}

int
mcre_put (mcg_t mcg, vid_t vid)
{
  struct mcre *re;
  int key = mcre_key (mcg, vid);

  HASH_FIND_INT (mcret, &key, re);
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
mcre_put_idx (int idx)
{
  return mcre_put (mcre_bp[idx].mcg, mcre_bp[idx].vid);
}
