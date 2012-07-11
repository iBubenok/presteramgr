#ifndef __ROUTE_P_H__
#define __ROUTE_P_H__

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>

#include <control-proto.h>

struct gw {
  GT_IPADDR addr;
  vid_t vid;
};

enum {
  DEFAULT_UC_RE_IDX = 0,
  DEFAULT_MC_RE_IDX,
  FIRST_REGULAR_RE_IDX
};

#define GW_FMT "%d.%d.%d.%d:%d"
#define GW_FMT_ARGS(gw) \
  gw->addr.arIP[0],gw->addr.arIP[1],gw->addr.arIP[2],gw->addr.arIP[3],gw->vid

static inline void
route_fill_gw (struct gw *gw, const GT_IPADDR *addr, vid_t vid)
{
  memset (gw, 0, sizeof (*gw));
  gw->addr.u32Ip = addr->u32Ip;
  gw->vid = vid;
}

#endif /* __ROUTE_P_H__ */
