#ifndef __PCL_H__
#define __PCL_H__

#include <control-proto.h>

extern enum status pcl_cpss_lib_init (int);
extern enum status pcl_port_setup (port_id_t);
extern enum status pcl_enable_port (port_id_t, int);
extern enum status pcl_enable_lbd_trap (port_id_t, int);
extern enum status pcl_enable_dhcp_trap (int);
extern enum status pcl_setup_vt (port_id_t, vid_t, vid_t, int, int);
extern enum status pcl_remove_vt (port_id_t, vid_t, int);
extern void pcl_port_enable_vt (port_id_t, int);
extern void pcl_port_clear_vt (port_id_t);
extern enum status pcl_enable_mc_drop (port_id_t, int);

extern void pcl_source_guard_drop_enable (port_id_t);
extern void pcl_source_guard_drop_disable (port_id_t);
extern void pcl_source_guard_rule_set (port_id_t, mac_addr_t, vid_t, ip_addr_t, uint16_t);
extern void pcl_source_guard_rule_unset (port_id_t, uint16_t);

#endif /* __PCL_H__ */
