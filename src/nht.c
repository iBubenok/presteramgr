#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
//#include <cpss/generic/cpssTypes.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>

#include <control-proto.h>
#include <sysdeps.h>
#include <debug.h>
#include <utils.h>

#include <uthash.h>

#define MAX_NH 4096

struct nexthop {
  GT_ETHERADDR addr;
  uint16_t idx;
  int refc;
  UT_hash_handle hh;
};

#define HASH_FIND_ETH(head, findeth, out)                   \
  HASH_FIND (hh, head, findeth, sizeof (GT_ETHERADDR), out)
#define HASH_ADD_ETH(head, ethfield, add)                   \
  HASH_ADD (hh, head, ethfield, sizeof (GT_ETHERADDR), add)

static struct nexthop *nht = NULL;

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

int
nht_add (const GT_ETHERADDR *addr)
{
  struct nexthop *nh;
  int idx, d;
  GT_STATUS rc;

  HASH_FIND_ETH (nht, addr, nh);
  if (nh) {
    ++nh->refc;
    return nh->idx;
  }

  idx = nhs_pop ();
  if (idx < 0)
    return idx;

  for_each_dev(d)
    rc = CRP (cpssDxChIpRouterArpAddrWrite (d, idx, (GT_ETHERADDR *) addr));

  if (rc != ST_OK) {
    nhs_push (idx);
    return -1;
  }

  nh = calloc (1, sizeof (struct nexthop));
  memcpy (&nh->addr, addr, sizeof (*addr));
  nh->idx = idx;
  nh->refc = 1;
  HASH_ADD_ETH (nht, addr, nh);

  return idx;
}

enum status
nht_unref (const GT_ETHERADDR *addr)
{
  struct nexthop *nh;

  HASH_FIND_ETH (nht, addr, nh);
  if (!nh)
    return ST_DOES_NOT_EXIST;

  if (--nh->refc == 0) {
    DEBUG ("last ref to %02x:%02x:%02x:%02x:%02x:%02x dropped, deleting",
           nh->addr.arEther[0], nh->addr.arEther[1], nh->addr.arEther[2],
           nh->addr.arEther[3], nh->addr.arEther[4], nh->addr.arEther[5]);
    nhs_push (nh->idx);
    HASH_DEL (nht, nh);
    free (nh);
  }

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

void
nht_dump (void) {
  struct nexthop *s, *t;
  DEBUG("!!!! NHT DUMP!!!!\n");
  HASH_ITER (hh, nht, s, t) {
    DEBUG(MAC_FMT ",    %08d:%02d\n",
        MAC_ARG(s->addr.arEther), s->idx, s->refc);
  }
  DEBUG("!!!! end NHT DUMP!!!!\n\n");
}
