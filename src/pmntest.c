#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <zmq.h>
#include <czmq.h>
#include <control-proto.h>

static int
notify_handler (zloop_t *loop, zmq_pollitem_t *pi, void *sock)
{
  zmsg_t *msg = zmsg_recv (sock);
  zframe_t *frame;
  uint16_t type;
  struct control_port_state state;

  frame = zmsg_pop (msg);
  assert (zframe_size (frame) == sizeof (type));
  memcpy (&type, zframe_data (frame), sizeof (type));
  zframe_destroy (&frame);
  printf ("got message of type %d\n", type);

  frame = zmsg_pop (msg);
  assert (zframe_size (frame) == sizeof (state));
  memcpy (&state, zframe_data (frame), sizeof (state));
  zframe_destroy (&frame);

  zmsg_destroy (&msg);

  printf ("port %d link %s at speed %d, %s duplex\n",
          state.port, state.link ? "up" : "down",
          state.speed, state.duplex ? "full" : "half");

  return 0;
}

int
main (int argc, char **argv)
{
  zctx_t *context = zctx_new ();
  void *sub_sock = zsocket_new (context, ZMQ_SUB);
  zmq_pollitem_t pi = { sub_sock, 0, ZMQ_POLLIN };
  zloop_t *loop = zloop_new ();
  uint16_t sub = CN_PORT_STATE;

  zmq_setsockopt (sub_sock, ZMQ_UNSUBSCRIBE, "", 0);
  zmq_setsockopt (sub_sock, ZMQ_SUBSCRIBE, &sub, sizeof (sub));
  zsocket_connect (sub_sock, PUB_SOCK_EP);
  zloop_poller (loop, &pi, notify_handler, sub_sock);
  zloop_start (loop);

  return 0;
}
