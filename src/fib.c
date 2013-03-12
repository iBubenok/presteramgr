#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <fib.h>
#include <uthash.h>
#include <assert.h>


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

void
fib_del (uint32_t addr, uint8_t len)
{
  assert (len < 33);

  if (len == 0) {
    if (fib.e[0]) {
      free (fib.e[0]);
      fib.e[0] = NULL;
    }
  } else {
    struct fib_entry *e;

    addr &= mkmask (len);
    HASH_FIND_INT (fib.e[len], &addr, e);
    if (e) {
      /* TODO: delete all children. */
      HASH_DEL (fib.e[len], e);
      free (e);
    }
  }
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
