#ifndef __FIB_H__
#define __FIB_H__

#include <control-proto.h>
#include <route-p.h>
#include <route.h>

struct fib_entry;

extern struct list_vid *fib_entry_get_vid (const struct fib_entry *);
extern struct list_uint32 *fib_entry_get_gw (const struct fib_entry *);
extern int fib_entry_get_len (const struct fib_entry *);
extern uint32_t fib_entry_get_pfx (const struct fib_entry *);
extern struct list_gw *fib_entry_get_retkey_ptr (struct fib_entry *);
extern int fib_entry_get_group_id (struct fib_entry *);
extern void fib_entry_set_group_id(struct fib_entry *, int);

extern void fib_add (uint32_t addr, uint8_t len, uint8_t gw_count, const struct gw *gws);
extern int fib_del (uint32_t, uint8_t);
extern struct fib_entry *fib_route (uint32_t);
extern struct fib_entry *fib_get (uint32_t, uint8_t);
extern struct fib_entry *fib_unhash_child (uint32_t, uint8_t);
extern void fib_clear_routing(void);
extern void fib_to_route(struct fib_entry *, struct route*);
extern void *fib_get_routes(void);
// extern void fib_dump(void);

#endif /* __FIB_H__ */
