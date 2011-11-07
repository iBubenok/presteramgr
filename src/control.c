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
#include <sysdeps.h>
#include <utils.h>

#include <gtOs/gtOsTask.h>

static unsigned __TASKCONV control_loop (GT_VOID *);

static zctx_t *context;
static void *pub_sock;
static void *cmd_sock;

int
control_init (void)
{
  int rc;

  context = zctx_new ();
  assert (context);

  pub_sock = zsocket_new (context, ZMQ_PUB);
  assert (pub_sock);
  rc = zsocket_bind (pub_sock, PUB_SOCK_EP);

  cmd_sock = zsocket_new (context, ZMQ_REP);
  assert (cmd_sock);
  rc = zsocket_bind (cmd_sock, CMD_SOCK_EP);

  return 0;
}

static zmsg_t *
make_notify_message (enum control_notification type)
{
  zmsg_t *msg = zmsg_new ();
  assert (msg);

  notification_t tmp = type;
  zmsg_addmem (msg, &tmp, sizeof (tmp));

  return msg;
}

static inline void
notify_send (zmsg_t **msg)
{
  zmsg_send (msg, pub_sock);
}

static inline void
put_port_num (zmsg_t *msg, port_num_t port)
{
  zmsg_addmem (msg, &port, sizeof (port));
}

static void
put_port_state (zmsg_t *msg, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  struct port_link_state state;

  data_encode_port_state (&state, attrs);
  zmsg_addmem (msg, &state, sizeof (state));
}

void
control_notify_port_state (port_num_t port, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{

  zmsg_t *msg = make_notify_message (CN_PORT_LINK_STATE);
  put_port_num (msg, port);
  put_port_state (msg, attrs);
  notify_send (&msg);
}

int
control_start (void)
{
  GT_TASK control_loop_tid;

  osTaskCreate ("control", 0, sysdeps_default_stack_size,
                control_loop, NULL, &control_loop_tid);
  return 0;
}

static zmsg_t *
make_reply (enum status code)
{
  zmsg_t *msg = zmsg_new ();
  assert (msg);

  status_t val = code;
  zmsg_addmem (msg, &val, sizeof (val));

  return msg;
}

static inline void
send_reply (zmsg_t *reply)
{
  zmsg_send (&reply, cmd_sock);
}

static inline void
report_status (enum status code)
{
  send_reply (make_reply (code));
}

static inline void
report_ok (void)
{
  report_status (ST_OK);
}

static enum status
pop_size (void *buf, zmsg_t *msg, size_t size, int opt)
{
  if (zmsg_size (msg) < 1)
    return opt ? ST_DOES_NOT_EXIST : ST_BAD_FORMAT;

  zframe_t *frame = zmsg_pop (msg);
  if (zframe_size (frame) != size) {
    zframe_destroy (&frame);
    return ST_BAD_FORMAT;
  }

  memcpy (buf, zframe_data (frame), size);
  zframe_destroy (&frame);

  return ST_OK;
}

typedef void (*cmd_handler_t) (zmsg_t *);
#define DECLARE_HANDLER(cmd) static void handle_##cmd (zmsg_t *)
#define DEFINE_HANDLER(cmd) static void handle_##cmd (zmsg_t *args)
#define HANDLER(cmd) [cmd] = handle_##cmd
#define POP_ARG(buf, size) (pop_size (buf, args, size, 0))
#define POP_OPT_ARG(buf, size) (pop_size (buf, args, size, 1))

DECLARE_HANDLER (CC_PORT_GET_STATE);

static cmd_handler_t handlers[] = {
  HANDLER (CC_PORT_GET_STATE)
};


static int
cmd_handler (zloop_t *loop, zmq_pollitem_t *pi, void *dummy)
{
  zmsg_t *msg;
  command_t cmd;
  cmd_handler_t handler;
  enum status result;

  msg = zmsg_recv (cmd_sock);

  result = pop_size (&cmd, msg, sizeof (cmd), 0);
  if (result != ST_OK) {
    report_status (result);
    goto out;
  }

  if (cmd >= ARRAY_SIZE (handlers) ||
      (handler = handlers[cmd]) == NULL) {
    report_status (ST_BAD_REQUEST);
    goto out;
  }
  handler (msg);

 out:
  zmsg_destroy (&msg);
  return 0;
}

static unsigned __TASKCONV
control_loop (GT_VOID *dummy)
{
  zloop_t *loop = zloop_new ();

  zmq_pollitem_t pi = { cmd_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &pi, cmd_handler, NULL);

  zloop_start (loop);

  return 0; /* Make the compiler happy. */
}


/*
 * Command handlers.
 */

DEFINE_HANDLER (CC_PORT_GET_STATE)
{
  port_num_t port;
  enum status result;
  struct port_link_state state;

  result = POP_ARG (&port, sizeof (port));
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  result = port_get_state (port, &state);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &state, sizeof (state));
  send_reply (reply);
}
