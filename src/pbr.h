#ifndef __PBR_H__
#define __PBR_H__

#include <control-proto.h>
#include <pcl.h>
#include <lttindex.h>
struct pbr_entry;

extern enum status pbr_route_set(struct row_colum *free_index, ip_addr_t nextHop, vid_t vid, struct pcl_interface interface);
extern enum status pbr_route_unset(struct pcl_interface interface);

extern struct row_colum *pbr_get_ltt_index(struct pbr_entry *pe);
extern void pbr_set_ltt_index(struct pbr_entry *pe, struct row_colum *ltt_index);

extern struct pcl_interface *pbr_get_interface(struct pbr_entry *pe);
extern void pbr_set_interface(struct pbr_entry *pe, struct pcl_interface *interface);

extern int pbr_get_group_id(struct pbr_entry *pe);

#endif /* __PBR_H__ */
