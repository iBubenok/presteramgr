#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <zmq.h>
#include <czmq.h>
#include <control-proto.h>

static uint16_t data = 0;

static int
notify_handler (zloop_t *loop, zmq_pollitem_t *pi, void *sock)
{
  zmsg_t *msg = zmsg_recv (sock);
  zframe_t *frame;
  uint16_t type;
  port_num_t port;
  struct port_link_state state;

  frame = zmsg_pop (msg);
  assert (zframe_size (frame) == sizeof (type));
  memcpy (&type, zframe_data (frame), sizeof (type));
  zframe_destroy (&frame);

  frame = zmsg_pop (msg);
  assert (zframe_size (frame) == sizeof (port));
  memcpy (&port, zframe_data (frame), sizeof (port));
  zframe_destroy (&frame);

  frame = zmsg_pop (msg);
  assert (zframe_size (frame) == sizeof (state));
  memcpy (&state, zframe_data (frame), sizeof (state));
  zframe_destroy (&frame);
  if (state.link)
    printf ("port %d link up at speed %d, %s duplex\n",
            port, state.speed, state.duplex ? "full" : "half");
  else
    printf ("port %d link down\n", port);

  zmsg_destroy (&msg);
  return 0;
}

static int
req_timer (zloop_t *loop, zmq_pollitem_t *dummy, void *sock)
{
  zmsg_t *msg = zmsg_new ();

  data++;
  zmsg_addmem (msg, &data, sizeof (data));
  zmsg_send (&msg, sock);

  fprintf (stderr, "sent request: %d\n", data);

  return 0;
}

static int
reply_handler (zloop_t *loop, zmq_pollitem_t *pi, void *sock)
{
  zmsg_t *msg = zmsg_recv (sock);
  zframe_t *frame;
  uint16_t data;

  frame = zmsg_pop (msg);
  assert (zframe_size (frame) == sizeof (data));
  memcpy (&data, zframe_data (frame), sizeof (data));
  zframe_destroy (&frame);
  zmsg_destroy (&msg);

  fprintf (stderr, "got reply: %d\n", data);

  zloop_timer (loop, 1000, 1, req_timer, sock);

  return 0;
}

int
main (int argc, char **argv)
{
  zctx_t *context = zctx_new ();
  void *sub_sock = zsocket_new (context, ZMQ_SUB);
  zmq_pollitem_t pi = { sub_sock, 0, ZMQ_POLLIN };
  void *cmd_sock = zsocket_new (context, ZMQ_REQ);
  zmq_pollitem_t pi2 = { cmd_sock, 0, ZMQ_POLLIN };
  zloop_t *loop = zloop_new ();

  zmq_setsockopt (sub_sock, ZMQ_UNSUBSCRIBE, "", 0);
  control_notification_subscribe (sub_sock, CN_PORT_LINK_STATE);
  control_notification_connect (sub_sock);

  zsocket_connect (cmd_sock, CMD_SOCK_EP);

  zloop_poller (loop, &pi, notify_handler, sub_sock);
  zloop_poller (loop, &pi2, reply_handler, cmd_sock);
  zloop_timer (loop, 1000, 1, req_timer, cmd_sock);

  zloop_start (loop);

  return 0;
}
