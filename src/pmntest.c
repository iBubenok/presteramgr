#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <zmq.h>
#include <czmq.h>
#include <control-proto.h>

static void *sub_sock, *cmd_sock;

static void
handle_port_link_state (zmsg_t *msg)
{
  zframe_t *frame;
  port_num_t port;
  struct port_link_state state;

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
}

static void
handle_bpdu (zmsg_t *msg)
{
  zframe_t *frame;
  port_num_t port;
  unsigned char *data;
  int len, i;

  frame = zmsg_pop (msg);
  assert (zframe_size (frame) == sizeof (port));
  memcpy (&port, zframe_data (frame), sizeof (port));
  zframe_destroy (&frame);

  frame = zmsg_pop (msg);
  len = zframe_size (frame);
  data = zframe_data (frame);
  printf ("got BPDU of length %d from port %d:", len, port);
  for (i = 0; i < len; i++)
    printf ("%c%02X", (i % 16) ? ' ' : '\n', data[i]);
  printf ("\n\n");
  zframe_destroy (&frame);
}

static int
notify_handler (zloop_t *loop, zmq_pollitem_t *pi, void *sock)
{
  zmsg_t *msg = zmsg_recv (sock);
  zframe_t *frame;
  notification_t type;


  frame = zmsg_pop (msg);
  assert (zframe_size (frame) == sizeof (type));
  memcpy (&type, zframe_data (frame), sizeof (type));
  zframe_destroy (&frame);

  switch (type) {
  case CN_PORT_LINK_STATE:
    handle_port_link_state (msg);
    break;
  case CN_BPDU:
    handle_bpdu (msg);
    break;
  default:
    fprintf (stderr, "notification %d not supported\n", type);
  }

  zmsg_destroy (&msg);
  return 0;
}

static void
test_CC_PORT_GET_STATE (void)
{
  port_num_t port;

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
}

static void
test_CC_PORT_SET_STP_STATE (void)
{
  zmsg_t *msg = zmsg_new ();

  command_t command = CC_PORT_SET_STP_STATE;
  zmsg_addmem (msg, &command, sizeof (command));

  port_num_t port = 3;
  zmsg_addmem (msg, &port, sizeof (port));

  stp_state_t state = STP_STATE_DISCARDING;
  zmsg_addmem (msg, &state, sizeof (state));

  stp_id_t id = 0;
  zmsg_addmem (msg, &id, sizeof (id));

  zmsg_send (&msg, cmd_sock);

  msg = zmsg_recv (cmd_sock);
  assert (zmsg_size (msg) == 1);
  status_t status;
  zframe_t *frame = zmsg_pop (msg);
  assert (zframe_size (frame) == sizeof (status));
  memcpy (&status, zframe_data (frame), sizeof (status));
  zframe_destroy (&frame);
  zmsg_destroy (&msg);

  printf ("CC_PORT_SET_STP_STATE for port %2d, STP ID %3d returned %d\n",
          port, id, status);
}

int
main (int argc, char **argv)
{
  zctx_t *context = zctx_new ();
  zloop_t *loop = zloop_new ();

  sub_sock = zsocket_new (context, ZMQ_SUB);
  zmq_setsockopt (sub_sock, ZMQ_UNSUBSCRIBE, "", 0);
  control_notification_subscribe (sub_sock, CN_PORT_LINK_STATE);
  control_notification_subscribe (sub_sock, CN_BPDU);
  control_notification_connect (sub_sock);

  cmd_sock = zsocket_new (context, ZMQ_REQ);
  zsocket_connect (cmd_sock, CMD_SOCK_EP);

  /* Test CC_PORT_GET_STATE request. */
  test_CC_PORT_GET_STATE ();

  /* Test CC_PORT_SET_STP_STATE request. */
  test_CC_PORT_SET_STP_STATE ();

  zmq_pollitem_t pi = { sub_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &pi, notify_handler, sub_sock);

  zloop_start (loop);

  return 0;
}
