#ifndef __RET_GROUP_H__
#define __RET_GROUP_H__

#include <route-p.h>
#include <route.h>
#include <ret.h>
// #include <route.h>


enum route_pfx_pbr_enum {
  ROUTE_PFX,
  ROUTE_PBR
};

union route_pfx_pbr_union {
  struct route_pfx pfx;
  struct route_pbr pbr;
};

struct route_pfx_pbr{
  enum route_pfx_pbr_enum type;
  union route_pfx_pbr_union data;  
};

struct list_route_pfx_pbr {
    struct route_pfx_pbr val;
    struct list_route_pfx_pbr *prev; /* needed for a doubly-linked list only */
    struct list_route_pfx_pbr *next; /* needed for singly- or doubly-linked lists */
};

struct pbr_entry;

extern int ret_group_add(int len, struct gw *gw, uint32_t ip, int alen);
extern int ret_group_add_pbr(int gw_count, struct gw *gw, struct pbr_entry *pe);
extern void ret_group_del(int group_id, uint32_t, int, bool_t);
extern void ret_group_del_pbr(struct pbr_entry *pe);
extern void ret_group_gw_changed(int group_id, const struct re *re, bool_t is_changed_valid);
extern struct re_group *ret_group_get(int group_id);
extern void ret_group_copy_pfx_pbr(struct list_route_pfx_pbr **pfx_pbr, struct re_group *group);

#endif /* __RET_GROUP_H__ */
