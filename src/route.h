#ifndef __ROUTE_H__
#define __ROUTE_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>

#include <control-proto.h>

struct route_pfx {
  GT_IPADDR addr;
  int alen;
};

struct route {
  struct route_pfx pfx;
  GT_IPADDR gw;
  int ifindex;
};

extern enum status route_cpss_lib_init (void);
extern enum status route_test (void);
extern enum status route_add (const struct route *);
extern enum status route_del (const struct route *);
extern enum status route_add_mgmt_ip (ip_addr_t);
extern enum status route_del_mgmt_ip (ip_addr_t);

#endif /* __ROUTE_H__ */
