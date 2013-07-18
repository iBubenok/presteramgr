#ifndef __MCG_H__
#define __MCG_H__

#include <control-proto.h>

extern enum status mcg_create (mcg_t);
extern enum status mcg_delete (mcg_t);
extern enum status mcg_add_port (mcg_t, port_id_t);
extern enum status mcg_del_port (mcg_t, port_id_t);
extern void mcg_dgasp_setup (void);
extern enum status mcg_dgasp_port_op (port_id_t, int);

#define DGASP_MCG     4093
#define LAST_USER_MCG 4092

static inline int
mcg_valid (mcg_t mcg)
{
  return mcg <= LAST_USER_MCG;
}

#endif /* __MCG_H__ */
