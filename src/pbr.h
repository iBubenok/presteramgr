#ifndef __PBR_H__
#define __PBR_H__

#include <control-proto.h>
#include <pcl.h>
#include <lttindex.h>

extern enum status pbr_route_set(struct row_colum *free_index, ip_addr_t nextHop, vid_t vid, struct pcl_interface interface);
extern enum status pbr_route_unset(struct pcl_interface interface);

#endif /* __PBR_H__ */
