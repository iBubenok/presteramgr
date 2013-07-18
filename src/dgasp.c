#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <dgasp.h>
#include <mcg.h>

void
dgasp_init (void)
{
  mcg_dgasp_setup ();
}

