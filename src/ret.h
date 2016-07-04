#ifndef __RET_H__
#define __RET_H__

#include <route-p.h>

extern enum status ret_init (void);
extern int ret_add (const struct gw *, int);
extern enum status ret_unref (const struct gw *, int);
extern enum status ret_set_mac_addr (const struct gw *, const GT_ETHERADDR *, vif_id_t);
extern int mcre_get (mcg_t, vid_t);
extern int mcre_put (const uint8_t *dst, const uint8_t *src, vid_t);
// extern int mcre_put (mcg_t, vid_t);
extern int mcre_del_node (int, mcg_t, vid_t, vid_t);

extern int mcre_find (const uint8_t *, const uint8_t *, vid_t);
extern int mcre_create (const uint8_t *, const uint8_t *, mcg_t, vid_t, vid_t);
extern int mcre_add_node (int, mcg_t, vid_t);
extern void ret_clear_devs_res(devsbmp_t);
extern void ret_dump(void);

#endif /* __RET_H__ */
