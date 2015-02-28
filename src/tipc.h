#ifndef __TIPC_H__
#define __TIPC_H__

#include <cpssdefs.h>
#include <cpss/generic/port/cpssPortCtrl.h>

#include <control-proto.h>

#define TIPC_CONTROL_EP "inproc://tipc-control"

enum {
  PTI_CMD_FDB_SEND = 0
} pti_cmd;

enum {
  PTI_FDB_OP_ADD,
  PTI_FDB_OP_DEL
} pti_fdb_op;

enum {
  IFTYPE_PORT = 0,
  IFTYPE_TRUNK,
  IFTYPE_VIDX,
  IFTYPE_VLANID,
  IFTYPE_DEVICE,
  IFTYPE_FABRIC_VIDX,
  IFTYPE_INDEX
} iface_type;

struct pti_fdbr {
  uint8_t operation;
  uint8_t type; ///< record type of enum iface_type
  union {
    struct {
      uint8_t hwdev;
      uint8_t hwport;
    } __attribute__ ((packed)) port;
    struct {
      uint8_t trunkId;
    } __attribute__ ((packed)) trunk;
  } __attribute__ ((packed)) ;
  uint16_t vid;
  uint8_t mac[6];
} __attribute__ ((packed));

extern void tipc_start (zctx_t *);
extern void tipc_notify_bpdu (port_id_t, size_t, void *);
extern void tipc_notify_link (port_id_t, const CPSS_PORT_ATTRIBUTES_STC *);
extern void tipc_bc_link_state (void);
extern enum status tipc_fdb_ctl(unsigned, const struct pti_fdbr *);

#endif /* __TIPC_H__ */
