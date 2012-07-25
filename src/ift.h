#ifndef __IFT_H__
#define __IFT_H__

#include <control-proto.h>
#include <rtnl.h>

#include <linux/rtnetlink.h>

extern enum status ift_add (const struct ifinfomsg *, const char *);
extern enum status ift_del (const struct ifinfomsg *, const char *);
extern enum status ift_add_addr (int, ip_addr_t);
extern enum status ift_del_addr (int, ip_addr_t);

#endif /* __IFT_H__ */
