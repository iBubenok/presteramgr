#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <gtOs/gtOsGen.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/dxCh/dxChxGen/mirror/cpssDxChMirror.h>

#include <vlan.h>
#include <port.h>
#include <monitor.h>
#include <debug.h>

#include <uthash.h>


struct session {
  int num;
  int enabled;
  int nsrcs;
  struct mon_if *src;
  struct mon_if dst;
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


static void
mon_configure_srcs (struct session *s)
{
  struct port *port;
  int i;

  for (i = 0; i < s->nsrcs; i++) {
    switch (s->src[i].type) {
    case MI_SRC_VLAN:
      CRP (cpssDxChBrgVlanIngressMirrorEnable (0, s->src[i].id, GT_TRUE));
      break;
    case MI_SRC_PORT_RX:
      port = port_ptr (s->src[i].id);
      CRP (cpssDxChMirrorRxPortSet (port->ldev, port->lport, GT_TRUE, 0));
      break;
    case MI_SRC_PORT_TX:
      port = port_ptr (s->src[i].id);
      CRP (cpssDxChMirrorTxPortSet (port->ldev, port->lport, GT_TRUE, 0));
      break;
    case MI_SRC_PORT_BOTH:
      port = port_ptr (s->src[i].id);
      CRP (cpssDxChMirrorRxPortSet (port->ldev, port->lport, GT_TRUE, 0));
      CRP (cpssDxChMirrorTxPortSet (port->ldev, port->lport, GT_TRUE, 0));
    }
  }
}

static void
mon_deconfigure_srcs (struct session *s)
{
  struct port *port;
  int i;

  for (i = 0; i < s->nsrcs; i++) {
    switch (s->src[i].type) {
    case MI_SRC_VLAN:
      CRP (cpssDxChBrgVlanIngressMirrorEnable (0, s->src[i].id, GT_FALSE));
      break;
    case MI_SRC_PORT_RX:
      port = port_ptr (s->src[i].id);
      CRP (cpssDxChMirrorRxPortSet (port->ldev, port->lport, GT_FALSE, 0));
      break;
    case MI_SRC_PORT_TX:
      port = port_ptr (s->src[i].id);
      CRP (cpssDxChMirrorTxPortSet (port->ldev, port->lport, GT_FALSE, 0));
      break;
    case MI_SRC_PORT_BOTH:
      port = port_ptr (s->src[i].id);
      CRP (cpssDxChMirrorRxPortSet (port->ldev, port->lport, GT_FALSE, 0));
      CRP (cpssDxChMirrorTxPortSet (port->ldev, port->lport, GT_FALSE, 0));
    }
  }
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

  if (enable && !s->enabled) {
    if ((s->rx && en_rx) || (s->tx && en_tx))
      return ST_BUSY;

    /* TODO: configure destination. */

    if (s->rx)
      en_rx = s;
    if (s->tx)
      en_tx = s;

    mon_configure_srcs (s);
    s->enabled = 1;
  } else if (s->enabled && !enable) {
    /* TODO: deconfigure destination. */
    if (s->rx)
      en_rx = NULL;
    if (s->tx)
      en_tx = NULL;

    mon_deconfigure_srcs (s);
    s->enabled = 0;
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
  int rx = 0, tx = 0, i;

  if (!s)
    return ST_DOES_NOT_EXIST;

  /* Validate config. */
  if (nsrcs) {
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
      mon_deconfigure_srcs (s);
    }
    free (s->src);
    s->src = NULL;
    s->nsrcs = s->rx = s->tx = 0;
  }

  if (nsrcs) {
    int size = nsrcs * sizeof (struct mon_if);

    s->src = malloc (size);
    memcpy (s->src, src, size);
    s->nsrcs = nsrcs;
    s->rx = rx;
    s->tx = tx;
    if (s->enabled) {
      mon_configure_srcs (s);
    }
  }

  return ST_OK;
}

enum status
mon_session_set_dst (mon_session_t num, const struct mon_if *dst)
{
  struct session *s = s_get (num);

  if (!s)
    return ST_DOES_NOT_EXIST;

  switch (dst->type) {
  case MI_DST_VLAN:
    if (!vlan_valid (dst->id))
      return ST_BAD_VALUE;
    break;
  case MI_DST_PORT:
    if (!port_valid (dst->id))
      return ST_BAD_VALUE;
    break;
  default:
    return ST_BAD_VALUE;
  }

  s->dst = *dst;

  return ST_OK;
}
