#ifndef __PCL_H__
#define __PCL_H__

#include <control-proto.h>

extern enum status pcl_cpss_lib_init (void);
extern enum status pcl_port_enable (port_id_t, int);
extern void pcl_test (void);

#endif /* __PCL_H__ */
