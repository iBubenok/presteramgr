#ifndef __TIPC_H__
#define __TIPC_H__

#include <control-proto.h>
#include <vif.h>

#define TIPC_POST_EP "inproc://tipc-post"

#define TIPC_MSG_MAX_LEN (11900)

extern struct sockaddr_tipc fdb_dst;

extern void tipc_start ();
extern void tipc_notify_bpdu (vif_id_t, port_id_t, vid_t, bool_t, size_t, void *);
extern void tipc_notify_link (vif_id_t vif, port_id_t, const struct port_link_state *);
extern void tipc_bc_link_state (void);
extern int tipc_fdbcomm_connect (void);

#endif /* __TIPC_H__ */
