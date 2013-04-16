#ifndef __STACK_H__
#define __STACK_H__

extern int stack_id;

static inline int
stack_active (void)
{
  return !!stack_id;
}

#endif /* __STACK_H__ */
