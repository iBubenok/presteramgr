#ifndef __TIPC_H__
#define __TIPC_H__

#include <control-proto.h>

extern void tipc_start (void);
extern void tipc_notify_bpdu (port_id_t, size_t, void *);

#endif /* __TIPC_H__ */
