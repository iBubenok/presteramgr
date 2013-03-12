#ifndef __FIB_H__
#define __FIB_H__

#include <control-proto.h>

struct fib_entry;

extern vid_t fib_entry_get_vid (const struct fib_entry *);
extern uint32_t fib_entry_get_gw (const struct fib_entry *);
extern int fib_entry_get_len (const struct fib_entry *);
extern uint32_t fib_entry_get_pfx (const struct fib_entry *);

extern void fib_add (uint32_t, uint8_t, vid_t, uint32_t);
extern void fib_del (uint32_t, uint8_t);
extern const struct fib_entry *fib_route (uint32_t);
extern struct fib_entry *fib_get (uint32_t, uint8_t);

#endif /* __FIB_H__ */
