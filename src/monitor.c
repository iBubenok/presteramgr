#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <monitor.h>

#include <uthash.h>


struct session {
  int num;
  int enabled;
  int nsrcs;
  struct mon_if *src;
  int rx;
  int tx;
  UT_hash_handle hh;
};

static struct session *sessions = NULL;
static struct session *en_rx = NULL, *en_tx = NULL;

static inline int
s_add (int num)
{
  struct session *s;

  HASH_FIND_INT (sessions, &num, s);
  if (s)
    return 0;

  s = calloc (1, sizeof (*s));
  s->num = num;
  HASH_ADD_INT (sessions, num, s);

  return 1;
}

static inline struct session *
s_get (int num)
{
  struct session *s;

  HASH_FIND_INT (sessions, &num, s);
  return s;
}

static inline int
s_del (int num)
{
  struct session *s;

  HASH_FIND_INT (sessions, &num, s);
  if (s) {
    HASH_DEL (sessions, s);
    return 1;
  } else
    return 0;
}

enum status
mon_session_add (mon_session_t num)
{
  return s_add (num) ? ST_OK : ST_ALREADY_EXISTS;
}

enum status
mon_session_enable (mon_session_t num, int enable)
{
  struct session *s = s_get (num);

  if (!s)
    return ST_DOES_NOT_EXIST;

  enable = !!enable;
  if (s->enabled != enable) {
    /* TODO: really enable/disable everything. */
    s->enabled = enable;
  }

  return ST_OK;
}

enum status
mon_session_del (mon_session_t num)
{
  return s_del (num) ? ST_OK : ST_DOES_NOT_EXIST;
}

enum status
mon_session_set_src (mon_session_t num, int nsrcs, const struct mon_if *src)
{
  struct session *s = s_get (num);
  int rx = 0, tx = 0;

  if (!s)
    return ST_DOES_NOT_EXIST;

  /* Validate config. */
  if (nsrcs) {
    int i;

    for (i = 0; i < nsrcs; i++) {
      switch (src[i].type) {
      case MI_SRC_VLAN:
        if (!vlan_valid (src[i].id))
          return ST_BAD_VALUE;
        rx = 1;
        break;
      case MI_SRC_PORT_RX:
        if (!port_valid (src[i].id))
          return ST_BAD_VALUE;
        rx = 1;
        break;
      case MI_SRC_PORT_TX:
        if (!port_valid (src[i].id))
          return ST_BAD_VALUE;
        tx = 1;
        break;
      case MI_SRC_PORT_BOTH:
        if (!port_valid (src[i].id))
          return ST_BAD_VALUE;
        rx = tx = 1;
        break;
      default:
        return ST_BAD_VALUE;
      }
    }

    if (s->enabled) {
      if ((rx && en_rx && en_rx != s) ||
          (tx && en_tx && en_tx != s))
        return ST_BUSY;
    }
  }

  if (s->src) {
    if (s->enabled) {
      /* TODO: deconfigure sources. */
    }
    free (s->src);
    s->src = NULL;
    s->nsrcs = s->rx = s->tx = 0;
  }

  if (nsrcs) {
    int i;
    int size = nsrcs * sizeof (struct mon_if), i;

    s->src = malloc (size);
    memcpy (s->src, src, size);
    s->nsrcs = nsrcs;
    if (enabled) {
      /* TODO: configure sources. */
    }
  }

  return ST_OK;
}
