#ifndef __MCG_H__
#define __MCG_H__

#include <control-proto.h>

extern enum status mcg_create (mcg_t);
extern enum status mcg_delete (mcg_t);
extern enum status mcg_add_port (mcg_t, port_id_t);
extern enum status mcg_del_port (mcg_t, port_id_t);

#endif /* __MCG_H__ */
