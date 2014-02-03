#ifndef __TIPC_H__
#define __TIPC_H__

#include <cpssdefs.h>
#include <cpss/generic/port/cpssPortCtrl.h>

#include <control-proto.h>

extern void tipc_start (void);
extern void tipc_notify_bpdu (port_id_t, size_t, void *);
extern void tipc_notify_link (port_id_t, const CPSS_PORT_ATTRIBUTES_STC *);
extern void tipc_bc_link_state (void);

#endif /* __TIPC_H__ */
