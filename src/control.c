#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <zmq.h>
#include <czmq.h>
#include <assert.h>


static zctx_t *context;


int
control_init (void)
{
  context = zctx_new ();

  return 0;
}
