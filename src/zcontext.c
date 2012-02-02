#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <zcontext.h>
#include <log.h>

zctx_t *zcontext = NULL;

int
zcontext_init (void)
{
  zcontext = zctx_new ();
  if (!zcontext) {
    ERR ("failed to create ZMQ context\n");
    return -1;
  }

  return 0;
}
