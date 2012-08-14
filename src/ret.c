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
#include <port.h>
#include <arp.h>
#include <route.h>
#include <debug.h>
#include <route-p.h>

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

        memset (&rt, 0, sizeof (rt));
        rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
        rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_ROUTE_E;
        rt.entry.regularEntry.nextHopInterface.type = CPSS_INTERFACE_PORT_E;
        rt.entry.regularEntry.nextHopInterface.devPort.devNum = port->ldev;
        rt.entry.regularEntry.nextHopInterface.devPort.portNum = port->lport;
        rt.entry.regularEntry.nextHopARPPointer = re->nh_idx;
        rt.entry.regularEntry.nextHopVlanId = gw->vid;
        DEBUG ("write default route entry");
        CRP (cpssDxChIpUcRouteEntriesWrite (0, DEFAULT_UC_RE_IDX, &rt, 1));
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

 out:
  if (re->valid)
    return (def ? 0 : re->idx);

  DEBUG ("sending ARP requests");
  arp_add_ip (arpc_sock, gw->vid, gw->addr.arIP);
  int i;
  for (i = 0; i < 3; i++)
    arp_send_req (gw->vid, gw->addr.arIP);
  DEBUG ("done sending ARP requests");
  return 0;
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

      DEBUG ("reset default route entry");
      memset (&rt, 0, sizeof (rt));
      rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
      rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_DROP_HARD_E;
      CRP (cpssDxChIpUcRouteEntriesWrite (0, DEFAULT_UC_RE_IDX, &rt, 1));
    }
  }

  if (--re->refc == 0) {
    DEBUG ("last ref to " GW_FMT " dropped, deleting", GW_FMT_ARGS (gw));
    HASH_DEL (ret, re);
    if (re->valid) {
      res_push (re->idx);
      nht_unref (&re->addr);
    }
    free (re);
    --re_cnt;
  }

  return ST_OK;
}

enum status
ret_set_mac_addr (const struct gw *gw, const GT_ETHERADDR *addr, port_id_t pid)
{
  struct re *re;
  int idx, nh_idx;
  CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
  GT_STATUS rc;
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_HEX;

  HASH_FIND_GW (ret, gw, re);
  if (!re)
    return ST_DOES_NOT_EXIST;

  if (re->valid && !memcmp (&re->addr, addr, sizeof (*addr))) {
    DEBUG ("MAC addr is already known");
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
  rt.entry.regularEntry.nextHopInterface.devPort.devNum = port->ldev;
  rt.entry.regularEntry.nextHopInterface.devPort.portNum = port->lport;
  rt.entry.regularEntry.nextHopARPPointer = nh_idx;
  rt.entry.regularEntry.nextHopVlanId = gw->vid;
  DEBUG ("write route entry");
  rc = CRP (cpssDxChIpUcRouteEntriesWrite (0, idx, &rt, 1));
  if (rc != ST_OK) {
    nht_unref (addr);
    res_push (idx);
    return ST_HEX;
  }
  if (re->def) {
    DEBUG ("write default route entry");
    CRP (cpssDxChIpUcRouteEntriesWrite (0, DEFAULT_UC_RE_IDX, &rt, 1));
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
