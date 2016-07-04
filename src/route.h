#ifndef __ROUTE_H__
#define __ROUTE_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>

#include <control-proto.h>
#include <route-p.h>

struct route_pfx {
  GT_IPADDR addr;
  int alen;
};

struct route {
  struct route_pfx pfx;
  GT_IPADDR gw;
  vid_t vid;
};

extern enum status route_cpss_lib_init (void);
extern enum status route_start (void);
extern enum status route_add (const struct route *);
extern enum status route_del (const struct route *);
extern enum status route_add_mgmt_ip (ip_addr_t);
extern enum status route_del_mgmt_ip (ip_addr_t);
extern enum status route_set_router_mac_addr (mac_addr_t);
extern void route_update_table (const struct gw *, int);
extern void route_handle_udaddr (uint32_t);
extern void route_handle_udt (const uint8_t *, int);
extern enum status route_mc_add (vid_t, const uint8_t *, const uint8_t *, mcg_t,
                                 vid_t);
extern enum status route_mc_del (vid_t, const uint8_t *, const uint8_t *, mcg_t,
                                 vid_t);
extern void *route_get_udaddrs(void);
extern void route_reset_prefixes4gw(struct gw *);
extern void route_dump(void);

#endif /* __ROUTE_H__ */
