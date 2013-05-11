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


#endif /* __STACK_H__ */
