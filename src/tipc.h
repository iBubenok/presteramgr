#ifndef __TIPC_H__
#define __TIPC_H__

#include <cpssdefs.h>
#include <cpss/generic/port/cpssPortCtrl.h>

#include <control-proto.h>

//#define TIPC_CONTROL_EP "inproc://tipc-control"
#define TIPC_POST_EP "inproc://tipc-post"

#define TIPC_MSG_MAX_LEN (11900)

enum pti_cmd {
  PTI_CMD_FDB_SEND = 0
};

enum pti_fdb_op {
  PTI_FDB_OP_ADD = 0,
  PTI_FDB_OP_DEL = 1
};

enum iface_type {
  IFTYPE_PORT = 0,
  IFTYPE_TRUNK,
  IFTYPE_VIDX,
  IFTYPE_VLANID,
  IFTYPE_DEVICE,
  IFTYPE_FABRIC_VIDX,
  IFTYPE_INDEX
};

extern struct sockaddr_tipc fdb_dst;

extern void tipc_start (zctx_t *);
extern void tipc_notify_bpdu (port_id_t, size_t, void *);
extern void tipc_notify_link (port_id_t, const CPSS_PORT_ATTRIBUTES_STC *);
extern void tipc_bc_link_state (void);
extern int tipc_fdbcomm_connect (void);
//extern enum status tipc_fdb_ctl(unsigned, const struct pti_fdbr *);

#endif /* __TIPC_H__ */
