#ifndef __STACK_H__
#define __STACK_H__

#include <control-proto.h>

extern int stack_id;

extern void stack_start (void);

static inline int
stack_active (void)
{
  return !!stack_id;
}

struct port;
extern struct port *stack_pri_port, *stack_sec_port;

extern enum status stack_mail (enum port_stack_role, void *, size_t);
extern void stack_handle_mail (port_id_t, uint8_t *, size_t);
extern uint8_t stack_port_get_state (enum port_stack_role);
extern enum status stack_set_dev_map (uint8_t, const uint8_t *);


#endif /* __STACK_H__ */
