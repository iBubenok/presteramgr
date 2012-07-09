#ifndef __ROUTE_P_H__
#define __ROUTE_P_H__

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>

#include <control-proto.h>

struct gw {
  GT_IPADDR addr;
  vid_t vid;
};

static inline void
route_fill_gw (struct gw *gw, const GT_IPADDR *addr, vid_t vid)
{
  memset (gw, 0, sizeof (*gw));
  gw->addr.u32Ip = addr->u32Ip;
  gw->vid = vid;
}

#endif /* __ROUTE_P_H__ */
