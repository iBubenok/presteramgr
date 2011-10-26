#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <zmq.h>
#include <czmq.h>
#include <assert.h>
#include <control.h>
#include <control-proto.h>
#include <port.h>


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

void
control_notify_port_state (int port, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  static enum control_port_speed psm[] = {
    [CPSS_PORT_SPEED_10_E]    = PORT_SPEED_10,
    [CPSS_PORT_SPEED_100_E]   = PORT_SPEED_100,
    [CPSS_PORT_SPEED_1000_E]  = PORT_SPEED_1000,
    [CPSS_PORT_SPEED_10000_E] = PORT_SPEED_10000,
    [CPSS_PORT_SPEED_12000_E] = PORT_SPEED_12000,
    [CPSS_PORT_SPEED_2500_E]  = PORT_SPEED_2500,
    [CPSS_PORT_SPEED_5000_E]  = PORT_SPEED_5000,
    [CPSS_PORT_SPEED_13600_E] = PORT_SPEED_13600,
    [CPSS_PORT_SPEED_20000_E] = PORT_SPEED_20000,
    [CPSS_PORT_SPEED_40000_E] = PORT_SPEED_40000,
    [CPSS_PORT_SPEED_16000_E] = PORT_SPEED_16000,
    [CPSS_PORT_SPEED_NA_E]    = PORT_SPEED_NA
  };
  struct control_port_state state;

  state.port = port;
  state.link = attrs->portLinkUp == GT_TRUE;
  state.duplex = attrs->portDuplexity == CPSS_PORT_FULL_DUPLEX_E;
  assert (attrs->portSpeed >= 0 && attrs->portSpeed <= CPSS_PORT_SPEED_NA_E);
  state.speed = psm[attrs->portSpeed];

  zmsg_t *msg = make_notify_message (CN_PORT_STATE);
  zmsg_addmem (msg, &state, sizeof (state));

  zmsg_send (&msg, pub_sock);
}
