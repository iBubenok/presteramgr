#ifndef __FIB_H__
#define __FIB_H__

#include <control-proto.h>

extern void fib_add (uint32_t, uint8_t);
extern void fib_del (uint32_t, uint8_t);
extern uint32_t fib_route (uint32_t);

#endif /* __FIB_H__ */
