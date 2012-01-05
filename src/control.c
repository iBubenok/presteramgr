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
#include <presteramgr.h>
#include <pdsa-mgmt.h>
#include <vlan.h>
#include <mac.h>
#include <qos.h>

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

void
control_notify_spec_frame (port_num_t port,
                           uint8_t code,
                           const unsigned char *data,
                           size_t len)
{
  notification_t type;

  switch (code) {
  case CPU_CODE_IEEE_RES_MC_0_TM:
    type = CN_BPDU;
    break;
  default:
    fprintf (stderr, "spec frame code %02X not supported\n", code);
    return;
  }

  zmsg_t *msg = make_notify_message (type);
  put_port_num (msg, port);
  zmsg_addmem (msg, data, len);
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

#define POP_ARG_SZ(buf, size) (pop_size (buf, args, size, 0))

#define POP_ARG(ptr) ({                         \
      typeof (ptr) __buf = ptr;                 \
      POP_ARG_SZ (__buf, sizeof (*__buf));      \
    })

#define POP_OPT_ARG_SZ(buf, size) (pop_size (buf, args, size, 1))

#define POP_OPT_ARG(ptr) ({                     \
      typeof (ptr) __buf = ptr;                 \
      POP_OPT_ARG_SZ (__buf, sizeof (*__buf));  \
    })

DECLARE_HANDLER (CC_PORT_GET_STATE);
DECLARE_HANDLER (CC_PORT_SET_STP_STATE);
DECLARE_HANDLER (CC_PORT_SEND_BPDU);
DECLARE_HANDLER (CC_PORT_SHUTDOWN);
DECLARE_HANDLER (CC_PORT_BLOCK);
DECLARE_HANDLER (CC_PORT_FDB_FLUSH);
DECLARE_HANDLER (CC_PORT_SET_MODE);
DECLARE_HANDLER (CC_PORT_SET_ACCESS_VLAN);
DECLARE_HANDLER (CC_PORT_SET_NATIVE_VLAN);
DECLARE_HANDLER (CC_PORT_SET_SPEED);
DECLARE_HANDLER (CC_SET_FDB_MAP);
DECLARE_HANDLER (CC_VLAN_ADD);
DECLARE_HANDLER (CC_VLAN_DELETE);
DECLARE_HANDLER (CC_VLAN_SET_DOT1Q_TAG_NATIVE);
DECLARE_HANDLER (CC_VLAN_DUMP);
DECLARE_HANDLER (CC_MAC_OP);
DECLARE_HANDLER (CC_MAC_SET_AGING_TIME);
DECLARE_HANDLER (CC_MAC_LIST);
DECLARE_HANDLER (CC_MAC_FLUSH_DYNAMIC);
DECLARE_HANDLER (CC_QOS_SET_MLS_QOS_TRUST);

static cmd_handler_t handlers[] = {
  HANDLER (CC_PORT_GET_STATE),
  HANDLER (CC_PORT_SET_STP_STATE),
  HANDLER (CC_PORT_SEND_BPDU),
  HANDLER (CC_PORT_SHUTDOWN),
  HANDLER (CC_PORT_BLOCK),
  HANDLER (CC_PORT_FDB_FLUSH),
  HANDLER (CC_PORT_SET_MODE),
  HANDLER (CC_PORT_SET_ACCESS_VLAN),
  HANDLER (CC_PORT_SET_NATIVE_VLAN),
  HANDLER (CC_PORT_SET_SPEED),
  HANDLER (CC_SET_FDB_MAP),
  HANDLER (CC_VLAN_ADD),
  HANDLER (CC_VLAN_DELETE),
  HANDLER (CC_VLAN_SET_DOT1Q_TAG_NATIVE),
  HANDLER (CC_VLAN_DUMP),
  HANDLER (CC_MAC_OP),
  HANDLER (CC_MAC_SET_AGING_TIME),
  HANDLER (CC_MAC_LIST),
  HANDLER (CC_MAC_FLUSH_DYNAMIC),
  HANDLER (CC_QOS_SET_MLS_QOS_TRUST)
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

  result = POP_ARG (&port);
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

DEFINE_HANDLER (CC_PORT_SET_STP_STATE)
{
  port_num_t port;
  stp_id_t stp_id;
  stp_state_t state;
  enum status result;

  result = POP_ARG (&port);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&state);
  if (result != ST_OK)
    goto out;

  result = POP_OPT_ARG (&stp_id);
  switch (result) {
  case ST_OK:
    result = port_set_stp_state (port, stp_id, state);
    break;

  case ST_DOES_NOT_EXIST:
    /* FIXME: set state for all STGs. */
    result = port_set_stp_state (port, 0, state);
    break;

  default:
    break;
  }

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SEND_BPDU)
{
  port_num_t n;
  struct port *port;
  size_t len;
  zframe_t *frame;
  enum status result = ST_BAD_FORMAT;

  result = POP_ARG (&n);
  if (result != ST_OK)
    goto out;

  if (!(port = port_ptr (n))) {
    result = ST_BAD_VALUE;
    goto out;
  }

  if (zmsg_size (args) != 1) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  frame = zmsg_pop (args);
  if ((len = zframe_size (frame)) < 1)
    goto destroy_frame;

  mgmt_send_frame (port->ldev, port->lport, zframe_data (frame), len);
  result = ST_OK;

 destroy_frame:
  zframe_destroy (&frame);
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SHUTDOWN)
{
  enum status result;
  port_num_t port;
  bool_t shutdown;

  result = POP_ARG (&port);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&shutdown);
  if (result != ST_OK)
    goto out;

  result = port_shutdown (port, shutdown);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_BLOCK)
{
  enum status result;
  port_num_t port;
  struct port_block what;

  result = POP_ARG (&port);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&what);
  if (result != ST_OK)
    goto out;

  result = port_block (port, &what);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_FDB_FLUSH)
{
  /* FIXME: stub. */
  report_ok ();
}

DEFINE_HANDLER (CC_SET_FDB_MAP)
{
  /* FIXME: stub. */
  report_ok ();
}

DEFINE_HANDLER (CC_VLAN_ADD)
{
  enum status result;
  vid_t vid;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = vlan_add (vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_DELETE)
{
  enum status result;
  vid_t vid;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = vlan_delete (vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_SET_DOT1Q_TAG_NATIVE)
{
  enum status result;
  bool_t value;

  result = POP_ARG (&value);
  if (result != ST_OK)
    goto out;

  result = vlan_set_dot1q_tag_native (value);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_DUMP)
{
  enum status result;
  vid_t vid;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  if (!vlan_valid (vid)) {
    result = ST_BAD_VALUE;
    goto out;
  }

  result = vlan_dump (vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MAC_OP)
{
  enum status result;
  struct mac_op_arg op_arg;

  result = POP_ARG (&op_arg);
  if (result != ST_OK)
    goto out;

  result = mac_op (&op_arg);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MAC_SET_AGING_TIME)
{
  enum status result;
  aging_time_t time;

  result = POP_ARG (&time);
  if (result != ST_OK)
    goto out;

  result = mac_set_aging_time (time);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_MODE)
{
  enum status result;
  port_num_t port;
  port_mode_t mode;

  result = POP_ARG (&port);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = port_set_mode (port, mode);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_ACCESS_VLAN)
{
  enum status result;
  port_num_t port;
  vid_t vid;

  result = POP_ARG (&port);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = port_set_access_vid (port, vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_NATIVE_VLAN)
{
  enum status result;
  port_num_t port;
  vid_t vid;

  result = POP_ARG (&port);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = port_set_native_vid (port, vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_SPEED)
{
  enum status result;
  port_num_t port;
  port_speed_t speed;

  result = POP_ARG (&port);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&speed);
  if (result != ST_OK)
    goto out;

  result = port_set_speed (port, speed);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MAC_LIST)
{
  enum status result;

  result = mac_list ();

  report_status (result);
}

DEFINE_HANDLER (CC_MAC_FLUSH_DYNAMIC)
{
  enum status result;
  struct mac_flush_arg fa;

  result = POP_ARG (&fa);
  if (result != ST_OK)
    goto out;

  result = mac_flush_dynamic (&fa);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_QOS_SET_MLS_QOS_TRUST)
{
  enum status result;
  bool_t trust;

  result = POP_ARG (&trust);
  if (result != ST_OK)
    goto out;

  result = qos_set_mls_qos_trust (trust);

 out:
  report_status (result);
}
