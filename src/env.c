#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>

#include <log.h>

static int hw_subtype = 0;

void
env_init (void)
{
  char *env;

  if ((env = getenv ("HW_SUBTYPE")))
    hw_subtype = atoi (env);
  else
    DEBUG ("HW_SUBTYPE not defined, assuming default");
}

int
env_hw_subtype (void)
{
  return hw_subtype;
}

