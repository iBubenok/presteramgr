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
  notification_t type;
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
    printf ("port %2d link up at speed %d, %s duplex\n",
            port, state.speed, state.duplex ? "full" : "half");
  else
    printf ("port %2d link down\n", port);

  zmsg_destroy (&msg);
  return 0;
}

int
main (int argc, char **argv)
{
  zctx_t *context = zctx_new ();
  void *sub_sock = zsocket_new (context, ZMQ_SUB);
  zmq_pollitem_t pi = { sub_sock, 0, ZMQ_POLLIN };
  void *cmd_sock = zsocket_new (context, ZMQ_REQ);
  zloop_t *loop = zloop_new ();
  port_num_t port;

  zmq_setsockopt (sub_sock, ZMQ_UNSUBSCRIBE, "", 0);
  control_notification_subscribe (sub_sock, CN_PORT_LINK_STATE);
  control_notification_connect (sub_sock);

  zsocket_connect (cmd_sock, CMD_SOCK_EP);

  for (port = 0; port < 30; port++) {
    zmsg_t *msg = zmsg_new ();
    command_t command = CC_PORT_GET_STATE;
    zmsg_addmem (msg, &command, sizeof (command));
    zmsg_addmem (msg, &port, sizeof (port));
    zmsg_send (&msg, cmd_sock);

    msg = zmsg_recv (cmd_sock);
    assert (zmsg_size (msg) > 0);
    zframe_t *frame = zmsg_pop (msg);
    status_t status;
    assert (zframe_size (frame) == sizeof (status));
    memcpy (&status, zframe_data (frame), sizeof (status));
    zframe_destroy (&frame);
    if (status != ST_OK) {
      printf ("port %2d: CC_PORT_GET_STATE returned %d\n", port, status);
      zmsg_destroy (&msg);
      continue;
    }

    struct port_link_state state;
    frame = zmsg_pop (msg);
    assert (zframe_size (frame) == sizeof (state));
    memcpy (&state, zframe_data (frame), sizeof (state));
    zframe_destroy (&frame);

    if (state.link)
      printf ("port %2d link up at speed %d, %s duplex\n",
              port, state.speed, state.duplex ? "full" : "half");
    else
      printf ("port %2d link down\n", port);

    zmsg_destroy (&msg);
  }

  zloop_poller (loop, &pi, notify_handler, sub_sock);
  zloop_start (loop);

  return 0;
}
