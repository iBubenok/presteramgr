#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <zmq.h>
#include <czmq.h>
#include <assert.h>
#include <control.h>
#include <control-proto.h>
#include <port.h>
#include <data.h>


static zctx_t *context;
static void *pub_sock;

int
control_init (void)
{
  int rc;

  context = zctx_new ();
  assert (context);

  pub_sock = zsocket_new (context, ZMQ_PUB);
  assert (pub_sock);
  rc = zsocket_bind (pub_sock, PUB_SOCK_EP);

  return 0;
}

static zmsg_t *
make_notify_message (enum control_notification type)
{
  zmsg_t *msg = zmsg_new ();
  assert (msg);

  uint16_t tmp = type;
  zmsg_addmem (msg, &tmp, sizeof (tmp));

  return msg;
}

static inline void
notify_send (zmsg_t **msg)
{
  zmsg_send (msg, pub_sock);
}

static void
put_port_state (zmsg_t *msg, int port, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  struct port_link_state state;

  data_encode_port_state (&state, port, attrs);
  zmsg_addmem (msg, &state, sizeof (state));
}

void
control_notify_port_state (int port, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{

  zmsg_t *msg = make_notify_message (CN_PORT_LINK_STATE);
  put_port_state (msg, port, attrs);
  notify_send (&msg);
}
