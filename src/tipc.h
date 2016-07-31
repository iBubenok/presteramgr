#ifndef __TIPC_H__
#define __TIPC_H__

#include <cpssdefs.h>
#include <cpss/generic/port/cpssPortCtrl.h>

#include <control-proto.h>

//#define TIPC_CONTROL_EP "inproc://tipc-control"
#define TIPC_POST_EP "inproc://tipc-post"

#define TIPC_MSG_MAX_LEN (11900)

extern struct sockaddr_tipc fdb_dst;

extern void tipc_start (zctx_t *);
extern void tipc_notify_bpdu (port_id_t, size_t, void *);
extern void tipc_notify_link (port_id_t, const CPSS_PORT_ATTRIBUTES_STC *);
extern void tipc_bc_link_state (void);
extern int tipc_fdbcomm_connect (void);

#endif /* __TIPC_H__ */
