#ifndef __RET_H__
#define __RET_H__

#include <route-p.h>

extern enum status ret_init (void);
// extern int ret_add (const struct gw *, int, struct gw *);
extern struct re *ret_add (const struct gw *, int);
extern int ret_ipv6_add (const struct gw_v6 *, int, struct gw_v6 *);
// extern enum status ret_unref (const struct gw *, int);
extern enum status ret_unref (const struct gw *, int);
extern enum status ret_ipv6_unref (const struct gw_v6 *, int);
// extern enum status ret_set_mac_addr (const struct gw *, const GT_ETHERADDR *, vif_id_t);
extern enum status ret_set_mac_addr (const struct gw *, const GT_ETHERADDR *, vif_id_t);
extern enum status ret_unset_mac_addr (const struct gw *);
extern enum status ret_ipv6_set_mac_addr (const struct gw_v6 *, const GT_ETHERADDR *, vif_id_t);
extern int mcre_get (mcg_t, vid_t);
extern int mcre_put (const uint8_t *dst, const uint8_t *src, vid_t);
// extern int mcre_put (mcg_t, vid_t);
extern int mcre_del_node (int, mcg_t, vid_t, vid_t);

extern int mcre_find (const uint8_t *, const uint8_t *, vid_t);
extern int mcre_create (const uint8_t *, const uint8_t *, mcg_t, vid_t, vid_t);
extern int mcre_add_node (int, mcg_t, vid_t);
extern void ret_clear_devs_res(devsbmp_t);
extern void ret_dump(void);
extern void ret_arpc_request_addr(struct gw *gw);

extern bool_t res_pop (IN int count, OUT int *ids);
extern bool_t res_push (IN int count, OUT int *ids);
extern bool_t res_push_count_first(int count, int first);

extern int ret_get_valid(const struct re *re);
extern void ret_set_valid(struct re *re, int valid);
extern struct gw *ret_get_gw(const struct re *re);

extern void ret_set_re_to_idx(const struct re *re, int idx);
extern void ret_set_re_to_idx_gw(const struct gw *gw, int idx);

#endif /* __RET_H__ */
