#ifndef __PCL_H__
#define __PCL_H__

#include <control-proto.h>

extern enum status pcl_cpss_lib_init (void);
extern enum status pcl_port_setup (port_id_t);
extern enum status pcl_enable_lbd_trap (port_id_t);
extern enum status pcl_enable_mc_drop (port_id_t, int);

#endif /* __PCL_H__ */
