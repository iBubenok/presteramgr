#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <rtbd.h>
#include <fib.h>
#include <route.h>
#include <route-p.h>
#include <uthash.h>
#include <utlist.h>
#include <assert.h>
#include <log.h>


struct fib_entry {
  uint32_t addr; /* To use HASH_*_INT(). */
  uint32_t pfx;
  struct list_uint32 *gw;
  struct list_vid *vid;
  int len;
  struct fib_entry *children;
  int group_id;
  struct list_gw *ret_key;
  UT_hash_handle hh;
};

struct list_vid *
fib_entry_get_vid (const struct fib_entry *e)
{
  return e->vid;
}

struct list_uint32 *
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

void fib_entry_set_group_id (struct fib_entry *e, int group_id)
{
  e->group_id = group_id;
}

int
fib_entry_get_group_id (struct fib_entry *e)
{
  return e->group_id;
}

struct list_gw *
fib_entry_get_retkey_ptr (struct fib_entry *e) {
  return e->ret_key;
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


int route_list_uint32_cmp (struct list_uint32 *a, struct list_uint32 *b) {
  if (a->val > b->val)
    return 1;

  if (a->val < b->val)
    return -1;

  return 0;
}


void
fib_add (uint32_t addr, uint8_t len, uint8_t gw_count, const struct gw *gws)
{
  assert (len < 33);

  if (len == 0) {
    if (fib.e[0] == NULL)
      fib.e[0] = fib_entry_new ();
      fib.e[0]->addr = 0;
      fib.e[0]->pfx  = 0;
      fib.e[0]->len  = len;
      int i = 0;
      for (i = 0; i < gw_count; i++) {
        struct list_uint32 tmp;
        struct list_uint32 *gw = NULL;
        struct list_vid *vid = NULL;
        struct list_gw *ret_key = NULL;

        tmp.val = ntohl(gws[i].addr.u32Ip);

        DL_SEARCH(fib.e[0]->gw, gw, &tmp, route_list_uint32_cmp);
        if (gw) {
          gw = NULL;
          continue;
        }

        gw = calloc(1, sizeof(struct list_uint32));
        vid = calloc(1, sizeof(struct list_vid));
        ret_key = calloc(1, sizeof(struct list_gw));

        gw->val = ntohl(gws[i].addr.u32Ip);
        vid->val = gws[i].vid;
        DL_APPEND(fib.e[0]->gw, gw);
        DL_APPEND(fib.e[0]->vid, vid);
        DL_APPEND(fib.e[0]->ret_key, ret_key);
      }
  } else {
    struct fib_entry *e;

    uint32_t pfx = addr;
    addr &= mkmask (len);
    HASH_FIND_INT (fib.e[len], &addr, e);
    if (!e) {
      e = fib_entry_new ();
      e->addr = addr;
      e->pfx  = pfx;
      e->len  = len;
      // e->vid  = vid;
      // e->gw   = gw;
      int i = 0;
      for (i = 0; i < gw_count; i++) {
        struct list_uint32 tmp;
        struct list_uint32 *gw = NULL;
        struct list_vid *vid = NULL;
        struct list_gw *ret_key = NULL;
        tmp.val = ntohl(gws[i].addr.u32Ip);
        DL_SEARCH(e->gw, gw, &tmp, route_list_uint32_cmp);
        if (gw) {
          gw = NULL;
          continue;
        }
        gw = calloc(1, sizeof(struct list_uint32));
        vid = calloc(1, sizeof(struct list_vid));
        ret_key = calloc(1, sizeof(struct list_gw));
        gw->val = ntohl(gws[i].addr.u32Ip);
        vid->val = gws[i].vid;
        DL_APPEND(e->gw, gw);
        DL_APPEND(e->vid, vid);
        DL_APPEND(e->ret_key, ret_key);
      }
      HASH_ADD_INT (fib.e[len], addr, e);
    }
  }
}

extern void route_del_fib_entry (struct fib_entry *, bool_t);

static void
fib_del_entry_with_children (struct fib_entry *e)
{
  struct fib_entry *c, *tmp;

  HASH_ITER (hh, e->children, c, tmp) {
    route_del_fib_entry (c, true);
    HASH_DEL (e->children, c);
    free (c);
  }
  route_del_fib_entry (e, false);
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

  if (len == 0) {
    return fib.e[0];
  }
  else {
    struct fib_entry *e;

    addr &= mkmask (len);
    HASH_FIND_INT (fib.e[len], &addr, e);
    return e;
  }
}

struct fib_entry *
fib_unhash_child (uint32_t addr, uint8_t len) { /* you should free child yourself */
  assert (len == 32);

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
        if (c) {
          HASH_DEL(e->children, c);
          return c;
        }
      }
    }
    pfx &= ~(1 << (32 - i));
  }

  return NULL;
}

struct fib_entry *
fib_route (uint32_t addr)
{
  int i;
  uint32_t pfx = addr;
  for (i = 32; i > 0; i--) {
    struct fib_entry *e;

    HASH_FIND_INT (fib.e[i], &pfx, e);
    if (e) {
      int count = 0;
      struct list_uint32 *el = NULL, *last_el = NULL;
      DL_FOREACH(e->gw, el) {
        count++;
        last_el = el;
      }

      // if (e->gw == 0) {
      if (count == 1 && last_el->val == 0) {
        /* Connected route. */
        struct fib_entry *c;
        HASH_FIND_INT (e->children, &addr, c);
        if (!c) {
          c = fib_entry_new ();
          c->addr = addr;
          c->pfx  = addr;
          c->vid  = e->vid;
          // c->gw   = addr;
          struct list_uint32 *add;
          add = calloc(1, sizeof(struct list_uint32));
          add->val = addr;
          DL_APPEND(c->gw, add);
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

void fib_to_route(struct fib_entry *fib, struct route *rt) {
  if (rt == NULL || fib == NULL) return;

  struct list_uint32 *gw;
  struct list_vid *vid;
  rt->pfx.addr.u32Ip = htonl(fib->pfx);
  rt->pfx.alen = fib->len;
  rt->gw_count = 0;
  DL_FOREACH(fib->gw, gw) {
    rt->gw[rt->gw_count].addr.u32Ip = htonl(gw->val);
    rt->gw_count++;
  }
  rt->gw_count = 0;
  DL_FOREACH(fib->vid, vid) {
    rt->gw[rt->gw_count].vid = vid->val;
    rt->gw_count++;
  }
}

void
fib_clear_routing(void) {

  struct fib_entry *s, *t;
  int i;
  for (i = 0; i < 32; i++) {
    HASH_ITER (hh, fib.e[i], s, t) {
      struct route route;
      fib_to_route(s, &route);
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

// void *
// fib_get_routes(void) {
//   // fib_dump();
//   int i;
//   uint32_t n = 0;
//   struct fib_entry *s, *t;

//   for (i = 0; i < 32; i++) {
// //    n += HASH_COUNT(fib.e[i]);
//     HASH_ITER (hh, fib.e[i], s, t)
//       n++;
//   }

//   void *r = malloc(sizeof(n) + (sizeof(struct rtbd_route_msg) + sizeof(struct rtbd_hexthop_data)) * n);
//   if (!r)
//     return NULL;

//   *(uint32_t*)r = n;
//   struct rtbd_route_msg *r1 = (struct rtbd_route_msg *)((uint32_t*)r + 1);
//   for (i = 0; i < 32; i++) {
//     HASH_ITER (hh, fib.e[i], s, t) {
//       r1->op = RRTO_ADD;
//       // r1->vid = s->vid;
//       r1->dst.v4 = htonl(s->pfx);
//       // r1->gw.v4 = htonl(s->gw);
//       r1->dst_len = s->len;
//       r1->gw[0].gw.v4 = htonl(s->gw);
//       r1->gw[0].vid = s->vid;

//       r1++;
//     }
//   }
//   return r;
// }

void *
fib_get_routes(void) {
  // fib_dump();
  int i, j;
  uint32_t fib_n = 0;
  uint32_t gw_n = 0;

  struct fib_entry *s, *t;
  struct list_uint32 *gw;
  struct list_vid *vid;
  
  for (i = 0; i < 32; i++) {
//    n += HASH_COUNT(fib.e[i]);
    HASH_ITER (hh, fib.e[i], s, t) {
      fib_n++;
      DL_FOREACH(s->gw, gw) {
        gw_n++;
      }
    }
  }

  void *r = malloc(sizeof(fib_n) + (sizeof(struct rtbd_route_msg) * fib_n) + (sizeof(struct rtbd_hexthop_data) * gw_n));
  if (!r)
    return NULL;

  *(uint32_t*)r = fib_n;
  struct rtbd_route_msg *r1 = (struct rtbd_route_msg *)((uint32_t*)r + 1);
  for (i = 0; i < 32; i++) {
    HASH_ITER (hh, fib.e[i], s, t) {
      r1->op = RRTO_ADD;
      // r1->vid = s->vid;
      r1->dst.v4 = htonl(s->pfx);
      // r1->gw.v4 = htonl(s->gw);
      r1->dst_len = s->len;

      j = 0;
      DL_FOREACH(s->gw, gw) {
        r1->gw[j].gw.v4 = htonl(gw->val);
        j++;
      }
      j = 0;
      DL_FOREACH(s->vid, vid) {
        r1->gw[j].vid = vid->val;
        j++;
      }
      r1->gw_count = j;
      // r1->gw[0].gw.v4 = htonl(s->gw);
      // r1->gw[0].vid = s->vid;

      r1 = (struct rtbd_route_msg *)&r1->gw[j];
    }
  }

  return r;
}

// void
// fib_dump(void){
//   struct fib_entry *s, *t;
//   int i;
//   DEBUG("!!!! FIB DUMP!!!!\n");
//   for (i = 0; i<32;i++) {
//     int k = 1;
//     HASH_ITER (hh, fib.e[i], s, t) {
//       if (k)
//         DEBUG("==== %d\n", i);
//       k = 0;
//       DEBUG("%16p,   %08X:%08X - %08X:%02d, %02d,  %16p, %d:%x\n",
//           s, s->addr, s->pfx, s->gw, s->vid, s->len, s->children, s->ret_key.vid, s->ret_key.addr.u32Ip);
//       if (s->children) {
//         struct fib_entry *s1, *t1;
//         HASH_ITER (hh, s->children, s1, t1) {
//           DEBUG("c       %16p,   %08X:%08X - %08X:%02d, %02d,  %16p, %d:%x\n",
//               s->children, s1->addr, s1->pfx, s1->gw, s1->vid, s1->len, s1->children, s1->ret_key.vid, s1->ret_key.addr.u32Ip);
//         }
//       }
//     }
//     if (!k)
//       DEBUG("<<<< %d\n", i);
//   }
//   DEBUG("!!!! endFIB DUMP!!!!\n\n");
// }


