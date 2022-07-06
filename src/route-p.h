#ifndef __ROUTE_P_H__
#define __ROUTE_P_H__

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>

#include <control-proto.h>

#include <uthash.h>

struct gw {
  GT_IPADDR addr;
  vid_t vid;
}__attribute__ ((packed)) ;

struct gw_v6 {
  GT_IPV6ADDR addr;
  vid_t vid;
};

struct list_uint32 {
    uint32_t val;
    struct list_uint32 *prev; /* needed for a doubly-linked list only */
    struct list_uint32 *next; /* needed for singly- or doubly-linked lists */
};

struct list_uint16 {
    uint16_t val;
    struct list_uint32 *prev; /* needed for a doubly-linked list only */
    struct list_uint32 *next; /* needed for singly- or doubly-linked lists */
};

struct list_int {
    int val;
    struct list_int *prev; /* needed for a doubly-linked list only */
    struct list_int *next; /* needed for singly- or doubly-linked lists */
};

struct list_vid {
    vid_t val;
    struct list_vid *prev; /* needed for a doubly-linked list only */
    struct list_vid *next; /* needed for singly- or doubly-linked lists */
};

struct list_gw {
    struct gw val;
    struct list_gw *prev; /* needed for a doubly-linked list only */
    struct list_gw *next; /* needed for singly- or doubly-linked lists */
};

#define HASH_FIND_GW(head, findgw, out)                 \
  HASH_FIND (hh, head, findgw, sizeof (struct gw), out)
#define HASH_ADD_GW(head, gwfield, add)                 \
  HASH_ADD (hh, head, gwfield, sizeof (struct gw), add)

enum {
  DEFAULT_UC_RE_IDX = 0,
  DEFAULT_MC_RE_IDX,
  TRAP_RE_IDX,
  DROP_RE_IDX,
  MGMT_IP_RE_IDX,
  FIRST_REGULAR_RE_IDX
};

#define DROP_MC_RE_IDX DEFAULT_MC_RE_IDX

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

static inline void
route_ipv6_fill_gw (struct gw_v6 *gw, const GT_IPV6ADDR *addr, vid_t vid)
{
  memset (gw, 0, sizeof (*gw));
  gw->addr = *addr;
  gw->vid = vid;
}

#endif /* __ROUTE_P_H__ */
