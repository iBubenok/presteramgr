#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <route.h>
#include <ip.h>

enum status
ip_cpss_lib_init (void)
{
  return route_cpss_lib_init ();
}
