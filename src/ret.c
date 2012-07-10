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
#include <debug.h>

#include <uthash.h>


#define MAX_RE 4094

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
  uint16_t idx;
  GT_ETHERADDR addr;
  uint16_t nh_idx;
  int refc;
  UT_hash_handle hh;
};
static struct re *ret;
static int re_cnt = 0;

#define HASH_FIND_GW(head, findgw, out)                 \
  HASH_FIND (hh, head, findgw, sizeof (struct gw), out)
#define HASH_ADD_GW(head, gwfield, add)                 \
  HASH_ADD (hh, head, gwfield, sizeof (struct gw), add)


enum status
ret_add (const struct gw *gw)
{
  struct re *re;

  HASH_FIND_GW (ret, gw, re);
  if (re) {
    ++re->refc;
    return ST_OK;
  }

  if (re_cnt >= MAX_RE)
    return ST_BAD_VALUE; /* FIXME: add overflow status value. */

  re = calloc (1, sizeof (*re));
  re->gw = *gw;
  re->refc = 1;
  HASH_ADD_GW (ret, gw, re);
  ++re_cnt;

  return ST_OK;
}

enum status
ret_unref (const struct gw *gw)
{
  struct re *re;

  HASH_FIND_GW (ret, gw, re);
  if (!re)
    return ST_DOES_NOT_EXIST;

  if (--re->refc == 0) {
    HASH_DEL (ret, re);
    --re_cnt;
  }

  return ST_OK;
}

enum status
ret_set_mac_addr (const struct gw *gw, const GT_ETHERADDR *addr)
{
  struct re *re;
  int idx, nh_idx;
  CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
  GT_STATUS rc;

  HASH_FIND_GW (ret, gw, re);
  if (!re)
    return ST_DOES_NOT_EXIST;

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
  /* TODO: set real port info. */
  rt.entry.regularEntry.nextHopInterface.devPort.devNum = 0;
  rt.entry.regularEntry.nextHopInterface.devPort.portNum = 15;
  rt.entry.regularEntry.nextHopARPPointer = nh_idx;
  rt.entry.regularEntry.nextHopVlanId = gw->vid;
  DEBUG ("write route entry");
  rc = CRP (cpssDxChIpUcRouteEntriesWrite (0, idx, &rt, 1));
  if (rc != ST_OK) {
    nht_unref (addr);
    res_push (idx);
    return ST_HEX;
  }

  re->idx = idx;
  memcpy (&re->addr, addr, sizeof (*addr));
  re->nh_idx = nh_idx;
  re->valid = 1;

  /* TODO: notify routing code of the new route entry. */
  return ST_OK;
}

enum status
ret_init (void)
{
  uint16_t n;

  nht_init ();

  DEBUG ("populate RE stack");
  for (n = 0; n < MAX_RE; n++)
    res.data[n] = n;
  res.sp = 0;

  return ST_OK;
}
