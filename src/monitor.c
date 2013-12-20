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
#include <utils.h>

#include <uthash.h>

#define RX_DST_IX 0
#define TX_DST_IX 1

struct session {
  int num;
  int enabled;
  int nsrcs;
  struct mon_if *src;
  port_id_t dst_pid;
  vid_t dst_vid;
  int rx;
  int tx;
  UT_hash_handle hh;
};

static struct session *sessions = NULL;
static struct session *en_rx = NULL, *en_tx = NULL;
static int n_en_s = 0;

static int dst_compat (const struct session *, port_id_t, vid_t);
static struct session *other_active_session (const struct session *);

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

static inline void
s_del (struct session *s)
{
  HASH_DEL (sessions, s);
  if (s->src)
    free (s->src);
  free (s);
}

static void
mon_configure_dst (struct session *s)
{
  struct port *port = port_ptr (s->dst_pid);
  CPSS_DXCH_MIRROR_ANALYZER_INTERFACE_STC iface;
  int tag = vlan_valid (s->dst_vid);
  CPSS_DXCH_MIRROR_ANALYZER_VLAN_TAG_CFG_STC cfg;

  if (!port)
    return;

  iface.interface.type = CPSS_INTERFACE_PORT_E;
  iface.interface.devPort.devNum = port->ldev;
  iface.interface.devPort.portNum = port->lport;

  if (tag) {
    cfg.etherType = 0x8100;
    cfg.vpt = 0;
    cfg.cfi = 0;
    cfg.vid = s->dst_vid;
    CRP (cpssDxChMirrorAnalyzerVlanTagEnable
         (port->ldev, port->lport, GT_TRUE));
  };

  if (s->rx) {
    if (tag)
      CRP (cpssDxChMirrorRxAnalyzerVlanTagConfig (0, &cfg));
    CRP (cpssDxChMirrorAnalyzerInterfaceSet (0, RX_DST_IX, &iface));
    CRP (cpssDxChMirrorRxGlobalAnalyzerInterfaceIndexSet
         (0, GT_TRUE, RX_DST_IX));
  }

  if (s->tx) {
    if (tag)
      CRP (cpssDxChMirrorTxAnalyzerVlanTagConfig (0, &cfg));
    CRP (cpssDxChMirrorAnalyzerInterfaceSet (0, TX_DST_IX, &iface));
    CRP (cpssDxChMirrorTxGlobalAnalyzerInterfaceIndexSet
         (0, GT_TRUE, TX_DST_IX));
  }
}

static void
mon_deconfigure_dst (struct session *s)
{
  struct port *port = port_ptr (s->dst_pid);

  if (!port)
    /* Destination is not configured. */
    return;

  if (s->rx)
    CRP (cpssDxChMirrorRxGlobalAnalyzerInterfaceIndexSet
         (0, GT_FALSE, RX_DST_IX));

  if (s->tx)
    CRP (cpssDxChMirrorTxGlobalAnalyzerInterfaceIndexSet
         (0, GT_FALSE, TX_DST_IX));

  if (vlan_valid (s->dst_vid)) {
    struct session *o = other_active_session (s);
    if (!o || (o->dst_pid != s->dst_pid))
      CRP (cpssDxChMirrorAnalyzerVlanTagEnable
           (port->ldev, port->lport, GT_FALSE));
  }
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

  if (s->rx)
    en_rx = s;
  if (s->tx)
    en_tx = s;
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

  if (s->rx)
    en_rx = NULL;
  if (s->tx)
    en_tx = NULL;
}

enum status
mon_session_add (mon_session_t num)
{
  return s_add (num) ? ST_OK : ST_ALREADY_EXISTS;
}

static enum status
__mon_session_enable (struct session *s, int enable)
{
  if (s->enabled == enable)
    return ST_OK;

  if (enable) {
    if ((n_en_s > 1) ||
        (s->rx && en_rx) ||
        (s->tx && en_tx) ||
        !dst_compat (other_active_session (s), s->dst_pid, s->dst_vid))
      return ST_BUSY;

    mon_configure_dst (s);
    mon_configure_srcs (s);
    n_en_s++;
  } else {
    mon_deconfigure_srcs (s);
    mon_deconfigure_dst (s);
    n_en_s--;
  }
  s->enabled = enable;

  return ST_OK;
}

enum status
mon_session_enable (mon_session_t num, int enable)
{
  struct session *s = s_get (num);

  if (!s)
    return ST_DOES_NOT_EXIST;

  return __mon_session_enable (s, !!enable);
}

enum status
mon_session_del (mon_session_t num)
{
  struct session *s = s_get (num);

  if (!s)
    return ST_DOES_NOT_EXIST;

  __mon_session_enable (s, 0);
  s_del (s);

  return ST_OK;
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
      if ((rx && en_rx && (en_rx != s)) ||
          (tx && en_tx && (en_tx != s)) ||
          !dst_compat (other_active_session (s), s->dst_pid, s->dst_vid))
        return ST_BUSY;
    }
  }

  if (s->src) {
    if (s->enabled) {
      mon_deconfigure_dst (s);
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
      mon_configure_dst (s);
    }
  }

  return ST_OK;
}

static int
dst_compat (const struct session *s, port_id_t dst_pid, vid_t dst_vid)
{
  if (!s)
    return 1;

  if (!port_valid (s->dst_pid) || !port_valid (dst_pid))
    return 1;

  /* If two sessions share the destination port, both of them
     must use tagged or untagged output. */
  if (s->dst_pid == dst_pid)
    return ((vlan_valid (s->dst_vid) && vlan_valid (dst_vid)) ||
            (!vlan_valid (s->dst_vid) && !vlan_valid (dst_vid)));

  return 1;
}

struct session *
other_active_session (const struct session *session)
{
  if (session == en_rx)
    return (session == en_tx) ? NULL : en_tx;
  else
    return en_rx;
}

enum status
mon_session_set_dst (mon_session_t num, port_id_t dst_pid, vid_t dst_vid)
{
  struct session *s = s_get (num);

  if (!s)
    return ST_DOES_NOT_EXIST;

  if (!((dst_pid == 0 || port_valid (dst_pid)) &&
        (dst_vid == 0 || vlan_valid (dst_vid))))
    return ST_BAD_VALUE;

  if (s->enabled) {
    if (!dst_compat (other_active_session (s), dst_pid, dst_vid))
      return ST_BUSY;

    mon_deconfigure_dst (s);
    s->dst_pid = dst_pid;
    s->dst_vid = dst_vid;
    mon_configure_dst (s);
  } else {
    s->dst_pid = dst_pid;
    s->dst_vid = dst_vid;
  }

  return ST_OK;
}

void
mon_cpss_lib_init (int d)
{
  CRP (cpssDxChMirrorToAnalyzerForwardingModeSet
       (d, CPSS_DXCH_MIRROR_TO_ANALYZER_FORWARDING_HOP_BY_HOP_E));
}
