#ifndef __MAC_H__
#define __MAC_H__

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>

#include <control-proto.h>
#include <stackd.h>
#include <route-p.h>
#include <rtbd.h>
#include <arpd.h>
#include <sysdeps.h>
#include <tipc.h>
#include <vif.h>

#define FDB_NOTIFY_EP  "inproc://fdb-notify"
#define FDB_CONTROL_EP "inproc://fdb-control"
#define FDB_ACONTROL_EP "inproc://fdb-acontrol"
#define FDB_PUBSUB_EP  "inproc://fbd-pubsub"

struct fdb_entry {
  int valid;
  int secure;
  uint16_t pc_aging_status;
  CPSS_MAC_ENTRY_EXT_STC me;
};
extern struct fdb_entry fdb[];

extern CPSS_MAC_UPDATE_MSG_EXT_STC fdb_addrs[FDB_MAX_ADDRS];
extern GT_U32 fdb_naddrs;

extern enum status mac_op (const struct mac_op_arg *);
extern enum status mac_op_vif (const struct mac_op_arg_vif *);
extern enum status mac_op_own (vid_t, mac_addr_t, int);
extern enum status mac_op_rt (rtbd_notif_t, void *, int);
extern enum status mac_op_na (struct arpd_ip_addr_msg *);
extern enum status mac_op_opna (const struct gw *, arpd_command_t);
extern enum status mac_op_udt (uint32_t);
extern enum status mac_op_send_stg(void *);
extern enum status mac_op_send_vif_ls(struct vif_link_state_header *);
extern enum status mac_set_aging_time (aging_time_t);
extern enum status mac_list (void);
extern enum status mac_flush (const struct mac_age_arg *, GT_BOOL);
extern enum status mac_flush_vif (const struct mac_age_arg_vif *, GT_BOOL);
extern enum status mac_start (void);
extern enum status mac_mc_ip_op (const struct mc_ip_op_arg *);
extern enum status mac_set_master (uint8_t, serial_t, devsbmp_t);

#endif /* __MAC_H__ */
