#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <zmq.h>
#include <czmq.h>
#include <control-proto.h>

static void *cmd_sock;

static enum status
get_status (void)
{
  zmsg_t *msg = zmsg_recv (cmd_sock);
  assert (zmsg_size (msg) == 1);

  status_t status;
  zframe_t *frame = zmsg_first (msg);
  assert (zframe_size (frame) == sizeof (status));
  memcpy (&status, zframe_data (frame), sizeof (status));

  return status;
}

static enum status
add_vlan (vid_t vid)
{
  zmsg_t *msg = zmsg_new ();
  command_t command = CC_VLAN_ADD;
  zmsg_addmem (msg, &command, sizeof (command));
  zmsg_addmem (msg, &vid, sizeof (vid));
  zmsg_send (&msg, cmd_sock);

  return get_status ();
}

static enum status
set_cpu_vlan (vid_t vid, bool_t cpu)
{
  zmsg_t *msg = zmsg_new ();
  command_t command = CC_VLAN_SET_CPU;
  zmsg_addmem (msg, &command, sizeof (command));
  zmsg_addmem (msg, &vid, sizeof (vid));
  cpu = !!cpu;
  zmsg_addmem (msg, &cpu, sizeof (cpu));
  zmsg_send (&msg, cmd_sock);

  return get_status ();
}

static enum status
set_port_stp_state (port_id_t pid, stp_state_t state)
{
  zmsg_t *msg = zmsg_new ();
  command_t command = CC_PORT_SET_STP_STATE;
  zmsg_addmem (msg, &command, sizeof (command));
  zmsg_addmem (msg, &pid, sizeof (pid));
  zmsg_addmem (msg, &state, sizeof (state));
  zmsg_send (&msg, cmd_sock);

  return get_status ();
}

#define CHECK_STATUS(call) ({                               \
      enum status __status = (call);                        \
      if (__status != ST_OK) {                              \
        fprintf (stderr, #call "returned %d\n", __status);  \
        exit (EXIT_FAILURE);                                \
      }                                                     \
    })

int
main (int argc, char **argv)
{
  zctx_t *context = zctx_new ();

  cmd_sock = zsocket_new (context, ZMQ_REQ);
  zsocket_connect (cmd_sock, CMD_SOCK_EP);

  CHECK_STATUS (add_vlan (1));
  CHECK_STATUS (set_cpu_vlan (1, 1));
  CHECK_STATUS (set_port_stp_state (1, STP_STATE_FORWARDING));

  return EXIT_SUCCESS;
}
