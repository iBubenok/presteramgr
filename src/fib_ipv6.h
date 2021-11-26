#ifndef __FIB_IPV6_H__
#define __FIB_IPV6_H__

#include <control-proto.h>
#include <cpss/generic/cpssTypes.h>

struct fib_entry_ipv6;

extern vid_t fib_entry_ipv6_get_vid (const struct fib_entry_ipv6 *);
extern GT_IPV6ADDR fib_entry_ipv6_get_gw (const struct fib_entry_ipv6 *);
extern int fib_entry_ipv6_get_len (const struct fib_entry_ipv6 *);
extern GT_IPV6ADDR fib_entry_ipv6_get_pfx (const struct fib_entry_ipv6 *);
extern struct gw_v6 *fib_entry_ipv6_get_retkey_ptr (struct fib_entry_ipv6 *);

extern void fib_ipv6_add (GT_IPV6ADDR, uint8_t, vid_t, GT_IPV6ADDR);
extern int fib_ipv6_del (GT_IPV6ADDR, uint8_t);
extern struct fib_entry_ipv6 *fib_ipv6_route (GT_IPV6ADDR);
extern struct fib_entry_ipv6 *fib_ipv6_get (GT_IPV6ADDR, uint8_t);
extern struct fib_entry_ipv6 *fib_ipv6_unhash_child (GT_IPV6ADDR, uint8_t);
extern void fib_ipv6_clear_routing(void);
extern void *fib_ipv6_get_routes(void);
extern void fib_ipv6_dump(void);

#endif /* __FIB_IPV6_H__ */
