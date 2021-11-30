#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <rtbd.h>
#include <fib_ipv6.h>
#include <route.h>
#include <uthash.h>
#include <assert.h>
#include <log.h>
#include <utils.h>

struct fib_entry_ipv6 {
  GT_IPV6ADDR addr; /* To use HASH_*_INT(). */
  GT_IPV6ADDR pfx;
  GT_IPV6ADDR gw;
  vid_t vid;
  int len;
  struct fib_entry_ipv6 *children;
  struct gw_v6 ret_key;
  UT_hash_handle hh;
};

vid_t
fib_entry_ipv6_get_vid (const struct fib_entry_ipv6 *e)
{
  return e->vid;
}

GT_IPV6ADDR
fib_entry_ipv6_get_gw (const struct fib_entry_ipv6 *e)
{
  return e->gw;
}

int
fib_entry_ipv6_get_len (const struct fib_entry_ipv6 *e)
{
  return e->len;
}

GT_IPV6ADDR
fib_entry_ipv6_get_pfx (const struct fib_entry_ipv6 *e)
{
  return e->pfx;
}

struct gw_v6*
fib_entry_ipv6_get_retkey_ptr (struct fib_entry_ipv6 *e) {
  return &e->ret_key;
}

static struct fib_entry_ipv6 *
fib_entry_ipv6_new (void)
{
  return calloc (1, sizeof (struct fib_entry_ipv6));
}

static struct fib_ipv6 {
  struct fib_entry_ipv6 *e[129];
} fib;

void mkmask (GT_IPV6ADDR *addr, uint8_t n)
{
    uint32_t k = -(n - 128);
    addr->u32Ip[0] &=  96 <= k && k < 128 ? ntohl((0-1) << (k-96)) : k <= 96 ? 0-1 : 0 ;
    addr->u32Ip[1] &=  64 <= k && k < 96 ? ntohl((0-1) << (k-64)) : k <= 64 ? 0-1 : 0 ;
    addr->u32Ip[2] &=  32 <= k && k < 64 ? ntohl((0-1) << (k-32)) : k <= 32 ? 0-1 : 0 ;
    addr->u32Ip[3] &= k < 32 ? ntohl((0-1) << k) : 0;
}


void
fib_ipv6_add (GT_IPV6ADDR addr, uint8_t len, vid_t vid, GT_IPV6ADDR gw)
{
  assert (len < 129);

  if (len == 0) {
    if (fib.e[0] == NULL)
      fib.e[0] = fib_entry_ipv6_new ();
    // fib.e[0]->addr = 0;
    memset(&fib.e[0]->addr, 0, sizeof(fib.e[0]->addr));
    // fib.e[0]->pfx  = 0;
    memset(&fib.e[0]->pfx, 0, sizeof(fib.e[0]->pfx));
    fib.e[0]->vid  = vid;
    fib.e[0]->gw   = gw;
    fib.e[0]->len  = len;
  } else {
    struct fib_entry_ipv6 *e;

    GT_IPV6ADDR pfx = addr;

    mkmask(&addr, len);

    HASH_FIND_INT (fib.e[len], &addr, e);
    if (!e) {
      e = fib_entry_ipv6_new ();
      e->addr = addr;
      e->pfx  = pfx;
      e->vid  = vid;
      e->gw   = gw;
      e->len  = len;
      HASH_ADD_INT (fib.e[len], addr, e);
    }
  }
  fib_ipv6_dump();
}

extern void route_del_fib_ipv6_entry (struct fib_entry_ipv6 *);

static void
fib_ipv6_del_entry_with_children (struct fib_entry_ipv6 *e)
{
  struct fib_entry_ipv6 *c, *tmp;

  route_del_fib_ipv6_entry (e);
  HASH_ITER (hh, e->children, c, tmp) {
    route_del_fib_ipv6_entry (c);
    HASH_DEL (e->children, c);
    free (c);
  }
  free (e);
}

int
fib_ipv6_del (GT_IPV6ADDR addr, uint8_t len)
{
  assert (len < 129);

  if (len == 0) {
    if (fib.e[0]) {
      fib_ipv6_del_entry_with_children (fib.e[0]);
      fib.e[0] = NULL;
      return 1;
    }
  } else {
    struct fib_entry_ipv6 *e;

    mkmask(&addr, len);

    HASH_FIND_INT (fib.e[len], &addr, e);
    if (e) {
      HASH_DEL (fib.e[len], e);
      fib_ipv6_del_entry_with_children (e);
      return 1;
    }
  }
  fib_ipv6_dump();

  return 0;
}

struct fib_entry_ipv6 *
fib_ipv6_get (GT_IPV6ADDR addr, uint8_t len)
{
  assert (len < 129);

  if (len == 0)
    return fib.e[0];
  else {
    struct fib_entry_ipv6 *e;

    mkmask(&addr, len);

    HASH_FIND_INT (fib.e[len], &addr, e);
    return e;
  }
}

struct fib_entry_ipv6 *
fib_ipv6_unhash_child (GT_IPV6ADDR addr, uint8_t len) { /* you should free child yourself */
  GT_IPV6ADDR ip0;
  memset(&ip0, 0, sizeof(ip0));

  assert (len == 32);

  int i;
  GT_IPV6ADDR pfx = addr;

  for (i = 32; i > 0; i--) {
    struct fib_entry_ipv6 *e;
    HASH_FIND_INT (fib.e[i], &pfx, e);
    if (e) {
      // if (e->gw == 0) {
      if (memcmp(&e->gw, &ip0, 16) == 0) {
        /* Connected route. */
        struct fib_entry_ipv6 *c;

        HASH_FIND_INT (e->children, &addr, c);
        if (c) {
          HASH_DEL(e->children, c);
          return c;
        }
      }
    }

    mkmask(&pfx, i-1);
  }

  return NULL;
}

struct fib_entry_ipv6 *
fib_ipv6_route (GT_IPV6ADDR addr)
{
  GT_IPV6ADDR ip0;
  memset(&ip0, 0, sizeof(ip0));

  int i;
  GT_IPV6ADDR pfx = addr;

  for (i = 128; i > 0; i--) {
    struct fib_entry_ipv6 *e;

    HASH_FIND_INT (fib.e[i], &pfx, e);
    if (e) {
      if (memcmp(&e->gw, &ip0, 16) == 0) {
        /* Connected route. */
        struct fib_entry_ipv6 *c;

        HASH_FIND_INT (e->children, &addr, c);
        if (!c) {
          c = fib_entry_ipv6_new ();
          c->addr = addr;
          c->pfx  = addr;
          c->vid  = e->vid;
          c->gw   = addr;
          c->len  = 32;
          HASH_ADD_INT (e->children, addr, c);
        }
        return c;
      }

      return e;
    }

    mkmask(&pfx, i-1);
  }

  return fib.e[0] ? : 0;
}

void
fib_ipv6_clear_routing(void) {

  struct fib_entry_ipv6 *s, *t;
  int i;
  for (i = 0; i < 32; i++) {
    HASH_ITER (hh, fib.e[i], s, t) {
      struct route route;
      route.pfx.addrv6 = s->pfx;
      route.pfx.alen = s->len;
      route.gw_v6 = s->gw;
      route.vid = s->vid;
      route_del_v6(&route);
    }
  }
}

void *
fib_ipv6_get_routes(void) {
  fib_ipv6_dump();
  int i;
  uint32_t n = 0;
  struct fib_entry_ipv6 *s, *t;

  for (i = 0; i < 32; i++) {
//    n += HASH_COUNT(fib.e[i]);
    HASH_ITER (hh, fib.e[i], s, t)
      n++;
  }

  void *r = malloc(sizeof(n) + sizeof(struct rtbd_route_msg) * n);
  if (!r)
    return NULL;

  *(uint32_t*)r = n;
  struct rtbd_route_msg *r1 = (struct rtbd_route_msg *)((uint32_t*)r + 1);
  for (i = 0; i < 32; i++) {
    HASH_ITER (hh, fib.e[i], s, t) {
      r1->op = RRTO_ADD;
      r1->vid = s->vid;
      memcpy(&r1->dst_v6, &s->pfx.arIP, 16);
      memcpy(&r1->dst_v6, &s->gw.arIP, 16);
      r1->dst_len = s->len;
      r1++;
    }
  }
  return r;
}

void
fib_ipv6_dump(void){
  struct fib_entry_ipv6 *s, *t;
  int i;
  DEBUG("!!!! FIB_IPV6 DUMP!!!!\n");
  for (i = 0; i<128;i++) {
    int k = 1;
    HASH_ITER (hh, fib.e[i], s, t) {
      if (k)
        DEBUG("==== %d\n", i);
      k = 0;
      DEBUG("%16p,   "IPv6_FMT":"IPv6_FMT" - "IPv6_FMT":%02d, %02d,  %16p, %d:"IPv6_FMT"\n",
          s, 
          IPv6_ARG(s->addr.arIP), 
          IPv6_ARG(s->pfx.arIP), 
          IPv6_ARG(s->gw.arIP), 
          s->vid, 
          s->len, 
          s->children, 
          s->ret_key.vid, 
          IPv6_ARG(s->ret_key.addr.arIP));

      if (s->children) {
        struct fib_entry_ipv6 *s1, *t1;
        HASH_ITER (hh, s->children, s1, t1) {
          DEBUG("c       %16p,   "IPv6_FMT":"IPv6_FMT" - "IPv6_FMT":%02d, %02d,  %16p, %d:"IPv6_FMT"\n",
              s->children, 
              IPv6_ARG(s1->addr.arIP), 
              IPv6_ARG(s1->pfx.arIP), 
              IPv6_ARG(s1->gw.arIP), 
              s1->vid, 
              s1->len, 
              s1->children, 
              s1->ret_key.vid, 
              IPv6_ARG(s1->ret_key.addr.arIP));
        }
      }
    }
    if (!k)
      DEBUG("<<<< %d\n", i);
  }
  DEBUG("!!!! endFIB_IPV6 DUMP!!!!\n\n");
}
