#ifndef __MAC_H__
#define __MAC_H__

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>

#include <control-proto.h>
#include <sysdeps.h>
#include <tipc.h>
#include <vif.h>

#define FDB_NOTIFY_EP  "inproc://fdb-notify"
#define FDB_CONTROL_EP "inproc://fdb-control"
#define FDB_PUBSUB_EP  "inproc://fbd-pubsub"

struct fdb_entry {
  int valid;
  int secure;
  CPSS_MAC_ENTRY_EXT_STC me;
};
extern struct fdb_entry fdb[];

extern CPSS_MAC_UPDATE_MSG_EXT_STC fdb_addrs[FDB_MAX_ADDRS];
extern GT_U32 fdb_naddrs;

extern void mac_count(uint16_t , uint16_t, uint16_t); //TODO remove

extern enum status mac_op (const struct mac_op_arg *);
extern enum status mac_op_vif (const struct mac_op_arg_vif *);
extern enum status mac_op_own (vid_t, mac_addr_t, int);
extern enum status mac_set_aging_time (aging_time_t);
extern enum status mac_list (void);
extern enum status mac_flush (const struct mac_age_arg *, GT_BOOL);
extern enum status mac_start (void);
extern enum status mac_mc_ip_op (const struct mc_ip_op_arg *);
extern enum status mac_set_master (uint8_t);

extern void fdb_fill_dest_port (struct vif*, CPSS_MAC_ENTRY_EXT_STC *);
extern void fdb_fill_dest_trunk (struct vif*, CPSS_MAC_ENTRY_EXT_STC *);


#endif /* __MAC_H__ */
