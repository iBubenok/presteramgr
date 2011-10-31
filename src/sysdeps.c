#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <pthread.h>

size_t sysdeps_default_stack_size;

static void __attribute__ ((constructor))
get_system_params (void)
{
  pthread_attr_t attr;

  pthread_attr_init (&attr);
  pthread_attr_getstacksize (&attr, &sysdeps_default_stack_size);
}
