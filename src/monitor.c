#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <gtOs/gtOsGen.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/dxCh/dxChxGen/mirror/cpssDxChMirror.h>

#include <vif.h>
#include <stack.h>
#include <vlan.h>
#include <port.h>
#include <monitor.h>
#include <debug.h>
#include <utils.h>
#include <sysdeps.h>
#include <dev.h>

#include <uthash.h>

#define RX_DST_IX 0
#define TX_DST_IX 1

struct session {
  int num;
  int enabled;
  int nsrcs;
  struct mon_if *src;
  struct mon_if *dst;
  vid_t dst_vid;
  int rx;
  int tx;
  UT_hash_handle hh;
};

static struct session *sessions = NULL;
static struct session *en_rx = NULL, *en_tx = NULL;
static int n_en_s = 0;

static int dst_compat (const struct session *, struct mon_if *, vid_t);
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
  if (s->dst)
    free (s->dst);
  free (s);
}

static void
mon_configure_dst (struct session *s)
{
  if(!s->dst)
    return;
  struct vif *vif = vif_get(s->dst->num, 0, s->dst->dummy);
  CPSS_DXCH_MIRROR_ANALYZER_INTERFACE_STC iface;
  int tag = vlan_valid (s->dst_vid);
  CPSS_DXCH_MIRROR_ANALYZER_VLAN_TAG_CFG_STC cfg;
  int d;
  if (!vif)
    return;

  iface.interface.type = CPSS_INTERFACE_PORT_E;
  iface.interface.devPort.devNum = (s->dst->dev == stack_id? phys_dev(vif->local->ldev): s->dst->dev);
  iface.interface.devPort.portNum = vif->local->lport;
  if (tag) {
    cfg.etherType = 0x8100;
    cfg.vpt = 0;
    cfg.cfi = 0;
    cfg.vid = s->dst_vid;
    CRP (cpssDxChMirrorAnalyzerVlanTagEnable
         (vif->local->ldev, vif->local->lport, GT_TRUE));
  };

  if (s->rx) {
    for_each_dev (d) {
      if (tag)
        CRP (cpssDxChMirrorRxAnalyzerVlanTagConfig (d, &cfg));
      CRP (cpssDxChMirrorAnalyzerInterfaceSet (d, RX_DST_IX, &iface));
      CRP (cpssDxChMirrorRxGlobalAnalyzerInterfaceIndexSet
           (d, GT_TRUE, RX_DST_IX));
    }
  }

  if (s->tx) {
    for_each_dev (d) {
      if (tag)
        CRP (cpssDxChMirrorTxAnalyzerVlanTagConfig (d, &cfg));
      CRP (cpssDxChMirrorAnalyzerInterfaceSet (d, TX_DST_IX, &iface));
      CRP (cpssDxChMirrorTxGlobalAnalyzerInterfaceIndexSet
           (d, GT_TRUE, TX_DST_IX));
    }
  }
}

static void
mon_deconfigure_dst (struct session *s)
{
  if(!s->dst)
    return;
  struct vif *vif = vif_get(s->dst->num, 0, s->dst->dummy);
  int d;

  if (!vif)
    /* Destination is not configured. */
    return;
  int dev = (s->dst->dev == stack_id? vif->local->ldev: s->dst->dev);

  if (s->rx)
    for_each_dev (d)
      CRP (cpssDxChMirrorRxGlobalAnalyzerInterfaceIndexSet
           (d, GT_FALSE, RX_DST_IX));

  if (s->tx)
    for_each_dev (d)
      CRP (cpssDxChMirrorTxGlobalAnalyzerInterfaceIndexSet
           (d, GT_FALSE, TX_DST_IX));

  if (vlan_valid (s->dst_vid)) {
    struct session *o = other_active_session (s);
    if (!o || (o->dst != s->dst))
      CRP (cpssDxChMirrorAnalyzerVlanTagEnable
           (dev, vif->local->lport, GT_FALSE));
  }
}

static void
mon_configure_srcs (struct session *s)
{
  struct vif *vif;
  int i, d;
  for (i = 0; i < s->nsrcs; i++) {
    switch (s->src[i].type) {
    case MI_SRC_VLAN:
      for_each_dev (d)
        CRP (cpssDxChBrgVlanIngressMirrorEnable (d, s->src[i].num*32 + s->src[i].dev, GT_TRUE));
      break;
    case MI_SRC_PORT_RX:
      vif = vif_get(s->src[i].num, 0, s->src[i].dummy);
      if(!vif || (s->src[i].dev != stack_id))
        continue;
      CRP (cpssDxChMirrorRxPortSet (vif->local->ldev, vif->local->lport, GT_TRUE, 0));
      break;
    case MI_SRC_PORT_TX:
      vif = vif_get(s->src[i].num, 0, s->src[i].dummy);
      if(!vif || (s->src[i].dev != stack_id))
        continue;
      CRP (cpssDxChMirrorTxPortSet (vif->local->ldev, vif->local->lport, GT_TRUE, 0));
      break;
    case MI_SRC_PORT_BOTH:
      vif = vif_get(s->src[i].num, 0, s->src[i].dummy);
      if(!vif || (s->src[i].dev != stack_id))
        continue;
      CRP (cpssDxChMirrorRxPortSet (vif->local->ldev, vif->local->lport, GT_TRUE, 0));
      CRP (cpssDxChMirrorTxPortSet (vif->local->ldev, vif->local->lport, GT_TRUE, 0));
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
  int i, d;
  struct vif *vif;
  for (i = 0; i < s->nsrcs; i++) {
    switch (s->src[i].type) {
    case MI_SRC_VLAN:
      for_each_dev (d)
        CRP (cpssDxChBrgVlanIngressMirrorEnable (d, s->src[i].num*32 + s->src[i].dev, GT_FALSE));
      break;
    case MI_SRC_PORT_RX:
      vif = vif_get(s->src[i].num, 0, s->src[i].dummy);
      if(!(vif && s->dst) || (s->src[i].dev != stack_id))
        continue;
      CRP (cpssDxChMirrorRxPortSet (vif->local->ldev, vif->local->lport, GT_FALSE, 0));
      break;
    case MI_SRC_PORT_TX:
      vif = vif_get(s->src[i].num, 0, s->src[i].dummy);
      if(!(vif && s->dst) || (s->src[i].dev != stack_id))
        continue;
      CRP (cpssDxChMirrorTxPortSet (vif->local->ldev, vif->local->lport, GT_FALSE, 0));
      break;
    case MI_SRC_PORT_BOTH:
      vif = vif_get(s->src[i].num, 0, s->src[i].dummy);
      if(!(vif && s->dst) || (s->src[i].dev != stack_id))
        continue;
      CRP (cpssDxChMirrorRxPortSet (vif->local->ldev, vif->local->lport, GT_FALSE, 0));
      CRP (cpssDxChMirrorTxPortSet (vif->local->ldev, vif->local->lport, GT_FALSE, 0));
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
        !dst_compat (other_active_session (s), s->dst, s->dst_vid))
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
        if (!vlan_valid (src[i].num*32 + src[i].dev))
          return ST_BAD_VALUE;
        rx = 1;
        break;
      case MI_SRC_PORT_RX:
        rx = 1;
        break;
      case MI_SRC_PORT_TX:
        tx = 1;
        break;
      case MI_SRC_PORT_BOTH:
        rx = tx = 1;
        break;
      default:
        return ST_BAD_VALUE;
      }
    }

    if (s->enabled) {
      if ((rx && en_rx && (en_rx != s)) ||
          (tx && en_tx && (en_tx != s)) ||
          !dst_compat (other_active_session (s), s->dst, s->dst_vid))
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
dst_compat (const struct session *s, struct mon_if* dst, vid_t dst_vid)
{
  if (!s)
    return 1;

  if(!vif_get(dst->num, 0, dst->dummy) || !vif_get(s->dst->num, 0, s->dst->dummy))
    return 1;
  /* If two sessions share the destination port, both of them
     must use tagged or untagged output. */
  if (s->dst == dst)
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
mon_session_set_dst (mon_session_t num, struct mon_if* dst, vid_t dst_vid)
{
  struct session *s = s_get (num);

  if (!s)
    return ST_DOES_NOT_EXIST;

  if (!(dst && (dst_vid == 0 || vlan_valid (dst_vid))))
    return ST_BAD_VALUE;

  if (s->enabled) {
    if (!dst_compat (other_active_session (s), dst, dst_vid))
      return ST_BUSY;
    mon_deconfigure_dst (s);

    if(s->dst){
      free (s->dst);
      s->dst = NULL;
    }

    int size = sizeof (struct mon_if);
    s->dst = malloc (size);
    memcpy (s->dst, dst, size);
    s->dst_vid = dst_vid;
    if(s->dst)
      mon_configure_dst (s);

  } else {
    if(s->dst){
      free (s->dst);
      s->dst = NULL;
    }
    int size = sizeof (struct mon_if);
    s->dst = malloc (size);
    memcpy (s->dst, dst, size);
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
