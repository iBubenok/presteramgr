#ifndef __MCG_H__
#define __MCG_H__

#include <control-proto.h>

extern enum status mcg_create (mcg_t);
extern enum status mcg_delete (mcg_t);
extern int mcg_exists (mcg_t);
extern enum status mcg_add_port (mcg_t, port_id_t);
extern enum status mcg_del_port (mcg_t, port_id_t);
extern void mcg_dgasp_setup (void);
extern enum status mcg_dgasp_port_op (port_id_t, int);
extern void mcg_stack_setup (void);

#define LAST_USER_MCG 4091
#define DROP_ALL_MCG 4092
#define DGASP_MCG 4093
#define STACK_MCG 4094

static inline int
mcg_valid (mcg_t mcg)
{
  return mcg <= LAST_USER_MCG;
}

#endif /* __MCG_H__ */
