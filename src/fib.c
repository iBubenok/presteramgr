#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <rtbd.h>
#include <fib.h>
#include <route.h>
#include <uthash.h>
#include <assert.h>
#include <log.h>


struct fib_entry {
  uint32_t addr; /* To use HASH_*_INT(). */
  uint32_t pfx;
  uint32_t gw;
  vid_t vid;
  int len;
  struct fib_entry *children;
  UT_hash_handle hh;
};

vid_t
fib_entry_get_vid (const struct fib_entry *e)
{
  return e->vid;
}

uint32_t
fib_entry_get_gw (const struct fib_entry *e)
{
  return e->gw;
}

int
fib_entry_get_len (const struct fib_entry *e)
{
  return e->len;
}

uint32_t
fib_entry_get_pfx (const struct fib_entry *e)
{
  return e->pfx;
}

static struct fib_entry *
fib_entry_new (void)
{
  return calloc (1, sizeof (struct fib_entry));
}

static struct fib {
  struct fib_entry *e[33];
} fib;

static inline uint32_t __attribute__ ((pure))
mkmask (uint8_t n)
{
  return n ? ~((1 << (32 - n)) - 1) : 0;
}


void
fib_add (uint32_t addr, uint8_t len, vid_t vid, uint32_t gw)
{
  assert (len < 33);

  if (len == 0) {
    if (fib.e[0] == NULL)
      fib.e[0] = fib_entry_new ();
    fib.e[0]->addr = 0;
    fib.e[0]->pfx  = 0;
    fib.e[0]->vid  = vid;
    fib.e[0]->gw   = gw;
    fib.e[0]->len  = len;
  } else {
    struct fib_entry *e;

    uint32_t pfx = addr;
    addr &= mkmask (len);
    HASH_FIND_INT (fib.e[len], &addr, e);
    if (!e) {
      e = fib_entry_new ();
      e->addr = addr;
      e->pfx  = pfx;
      e->vid  = vid;
      e->gw   = gw;
      e->len  = len;
      HASH_ADD_INT (fib.e[len], addr, e);
    }
  }
}

extern void route_del_fib_entry (struct fib_entry *);

static void
fib_del_entry_with_children (struct fib_entry *e)
{
  struct fib_entry *c, *tmp;

  route_del_fib_entry (e);
  HASH_ITER (hh, e->children, c, tmp) {
    route_del_fib_entry (c);
    HASH_DEL (e->children, c);
    free (c);
  }
  free (e);
}

int
fib_del (uint32_t addr, uint8_t len)
{
  assert (len < 33);

  if (len == 0) {
    if (fib.e[0]) {
      fib_del_entry_with_children (fib.e[0]);
      fib.e[0] = NULL;
      return 1;
    }
  } else {
    struct fib_entry *e;

    addr &= mkmask (len);
    HASH_FIND_INT (fib.e[len], &addr, e);
    if (e) {
      HASH_DEL (fib.e[len], e);
      fib_del_entry_with_children (e);
      return 1;
    }
  }

  return 0;
}

struct fib_entry *
fib_get (uint32_t addr, uint8_t len)
{
  assert (len < 33);

  if (len == 0)
    return fib.e[0];
  else {
    struct fib_entry *e;

    addr &= mkmask (len);
    HASH_FIND_INT (fib.e[len], &addr, e);
    return e;
  }
}

const struct fib_entry *
fib_route (uint32_t addr)
{
  int i;
  uint32_t pfx = addr;

  for (i = 32; i > 0; i--) {
    struct fib_entry *e;

    HASH_FIND_INT (fib.e[i], &pfx, e);
    if (e) {
      if (e->gw == 0) {
        /* Connected route. */
        struct fib_entry *c;

        HASH_FIND_INT (e->children, &addr, c);
        if (!c) {
          c = fib_entry_new ();
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

    pfx &= ~(1 << (32 - i));
  }

  return fib.e[0] ? : 0;
}

void
fib_clear_routing(void) {

  struct fib_entry *s, *t;
  int i;
  for (i = 0; i < 32; i++) {
    HASH_ITER (hh, fib.e[i], s, t) {
      struct route route;
      route.pfx.addr.u32Ip = htonl(s->pfx);
      route.pfx.alen = s->len;
      route.gw.u32Ip = htonl(s->gw);
      route.vid = s->vid;
      route_del(&route);
    }
  }
/*
struct fib_entry {
  uint32_t addr;
  uint32_t pfx;
  uint32_t gw;
  vid_t vid;
  int len;
  struct fib_entry *children;
  UT_hash_handle hh;
};*/
}

void *
fib_get_routes(void) {
  fib_dump();
  int i;
  uint32_t n = 0;
  struct fib_entry *s, *t;

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
      r1->dst = htonl(s->pfx);
      r1->gw = htonl(s->gw);
      r1->dst_len = s->len;
      r1++;
    }
  }
  return r;
}

void
fib_dump(void){
  struct fib_entry *s, *t;
  int i;
  DEBUG("!!!! FIB DUMP!!!!\n");
  for (i = 0; i<32;i++) {
    int k = 1;
    HASH_ITER (hh, fib.e[i], s, t) {
      if (k)
        DEBUG("==== %d\n", i);
      k = 0;
      DEBUG("%16p,   %08X:%08X - %08X:%02d, %02d,  %16p\n",
          s, s->addr, s->pfx, s->gw, s->vid, s->len, s->children);
      if (s->children) {
        struct fib_entry *s1, *t1;
        HASH_ITER (hh, s->children, s1, t1) {
          DEBUG("c       %16p,   %08X:%08X - %08X:%02d, %02d,  %16p\n",
              s->children, s1->addr, s1->pfx, s1->gw, s1->vid, s1->len, s1->children);
        }
      }
    }
    if (!k)
      DEBUG("<<<< %d\n", i);
  }
  DEBUG("!!!! endFIB DUMP!!!!\n\n");
}
