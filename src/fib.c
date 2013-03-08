#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <fib.h>
#include <uthash.h>
#include <assert.h>


struct fib_entry {
  uint32_t addr; /* To use HASH_*_INT(). */
  UT_hash_handle hh;
};

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
fib_add (uint32_t addr, uint8_t len)
{
  assert (len < 33);

  if (len == 0) {
    if (fib.e[0] == NULL)
      fib.e[0] = fib_entry_new ();
    fib.e[0]->addr = addr;
  } else {
    struct fib_entry *e;

    addr &= mkmask (len);
    HASH_FIND_INT (fib.e[len], &addr, e);
    if (!e) {
      e = fib_entry_new ();
      e->addr = addr;
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
      HASH_DEL (fib.e[len], e);
      free (e);
    }
  }
}

uint32_t
fib_route (uint32_t addr)
{
  int i;

  for (i = 32; i > 0; i--) {
    struct fib_entry *e;

    HASH_FIND_INT (fib.e[i], &addr, e);
    if (e)
      return e->addr;

    addr &= ~(1 << (32 - i));
  }

  return fib.e[0] ? fib.e[0]->addr : 0;
}
