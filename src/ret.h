#ifndef __RET_H__
#define __RET_H__

#include <route-p.h>

extern enum status ret_init (void);
extern int ret_add (const struct gw *, int);
extern enum status ret_unref (const struct gw *, int);
extern enum status ret_set_mac_addr (const struct gw *, const GT_ETHERADDR *, port_id_t);

#endif /* __RET_H__ */
