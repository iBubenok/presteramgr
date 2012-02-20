#ifndef __MAC_H__
#define __MAC_H__

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>

#include <control-proto.h>

#define FDB_MAX_ADDRS 16384
extern CPSS_MAC_UPDATE_MSG_EXT_STC fdb_addrs[FDB_MAX_ADDRS];
extern GT_U32 fdb_naddrs;

extern enum status mac_op (const struct mac_op_arg *);
extern enum status mac_set_aging_time (aging_time_t);
extern enum status mac_list (void);
extern enum status mac_flush (const struct mac_age_arg *, GT_BOOL);
extern enum status mac_start (void);
enum status mac_mc_ip_op (const struct mc_ip_op_arg *);

#endif /* __MAC_H__ */
