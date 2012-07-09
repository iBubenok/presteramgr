#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>

#include <control-proto.h>
#include <debug.h>

#include <uthash.h>

#define MAX_NH 4096

struct nexthop {
  GT_IPADDR ip;
  GT_ETHERADDR eth;
  int valid;
  uint16_t idx;
  UT_hash_handle hh;
};

#define HASH_FIND_IP(head, findip, out)                 \
  HASH_FIND (hh, head, findip, sizeof (GT_IPADDR), out)
#define HASH_ADD_IP(head, ipfield, add)                 \
  HASH_ADD (hh, head, ipfield, sizeof(GT_IPADDR), add)

static struct nexthop *nht;
static int max_nh = MAX_NH;
static int nh_num = 0;

struct stack {
  int sp;
  uint16_t data[MAX_NH];
};

static struct stack nhs;

static inline int
nhs_pop (void)
{
  if (nhs.sp >= MAX_NH - 1)
    return -1;

  return nhs.data[nhs.sp++];
}

static inline int
nhs_push (uint16_t nh)
{
  if (nhs.sp == 0)
    return -1;

  nhs.data[--nhs.sp] = nh;
  return 0;
}

static enum status
nht_set_mac (const GT_IPADDR *ip, const GT_ETHERADDR *mac)
{
  struct nexthop *nh;
  int idx;
  GT_STATUS rc;

  HASH_FIND_IP (nht, ip, nh);
  if (!nh)
    return ST_DOES_NOT_EXIST;

  idx = nhs_pop ();
  if (idx < 0)
    return ST_HEX;

  rc = CRP (cpssDxChIpRouterArpAddrWrite (0, idx, (GT_ETHERADDR *) mac));
  if (rc == ST_OK) {
    memcpy (&nh->eth, mac, sizeof (GT_ETHERADDR));
    nh->valid = 1;
    nh->idx = idx;
    return ST_OK;
  } else {
    nhs_push (idx);
    switch (rc) {
    default: return ST_HEX;
    }
  }
}

static enum status
__nht_del_mac (struct nexthop *nh)
{
  if (!nh->valid)
    return ST_OK;

  if (nhs_push (nh->idx))
    return ST_HEX;

  nh->valid = 0;

  return ST_OK;
}

enum status
nht_add (const GT_IPADDR *ip)
{
  struct nexthop *nh;

  HASH_FIND_IP (nht, ip, nh);
  if (nh)
    return ST_ALREADY_EXISTS;

  if (nh_num >= max_nh)
    return ST_BAD_VALUE; /* FIXME: add overflow status value. */

  nh = calloc (1, sizeof (struct nexthop));
  nh->ip = *ip;
  HASH_ADD_IP (nht, ip, nh);
  nh_num++;

  return ST_OK;
}

enum status
nht_del (const GT_IPADDR *ip)
{
  struct nexthop *nh;
  enum status st;

  HASH_FIND_IP (nht, ip, nh);
  if (!nh)
    return ST_DOES_NOT_EXIST;

  st = __nht_del_mac (nht);
  if (st != ST_OK)
    return st;

  HASH_DEL (nht, nh);
  free (nh);
  nh_num--;
  assert (nh_num >= 0);

  return ST_OK;
}

enum status
nht_init (void)
{
  uint16_t n;

  DEBUG ("populate NH stack");
  for (n = 0; n < MAX_NH; n++)
    nhs.data[n] = n;
  nhs.sp = 0;

  return ST_OK;
}
