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
#include <zcontext.h>
#include <debug.h>
#include <wnct.h>

#include <gtOs/gtOsTask.h>

static unsigned __TASKCONV control_loop (GT_VOID *);

static void *pub_sock;
static void *cmd_sock;
static void *inp_sock;

int
control_init (void)
{
  int rc;

  pub_sock = zsocket_new (zcontext, ZMQ_PUB);
  assert (pub_sock);
  rc = zsocket_bind (pub_sock, PUB_SOCK_EP);

  cmd_sock = zsocket_new (zcontext, ZMQ_REP);
  assert (cmd_sock);
  rc = zsocket_bind (cmd_sock, CMD_SOCK_EP);

  inp_sock = zsocket_new (zcontext, ZMQ_REP);
  assert (inp_sock);
  rc = zsocket_bind (inp_sock, INP_SOCK_EP);

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
put_port_id (zmsg_t *msg, port_id_t pid)
{
  zmsg_addmem (msg, &pid, sizeof (pid));
}

static void
put_port_state (zmsg_t *msg, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  struct port_link_state state;

  data_encode_port_state (&state, attrs);
  zmsg_addmem (msg, &state, sizeof (state));
}

void
control_notify_port_state (port_id_t pid, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  zmsg_t *msg = make_notify_message (CN_PORT_LINK_STATE);
  put_port_id (msg, pid);
  put_port_state (msg, attrs);
  notify_send (&msg);
}

void
control_notify_spec_frame (port_id_t pid,
                           uint8_t code,
                           const unsigned char *data,
                           size_t len)
{
  notification_t type;

  switch (code) {
  case CPU_CODE_IEEE_RES_MC_0_TM:
    switch (data[5]) {
    case WNCT_STP:
      type = CN_BPDU;
      break;
    case WNCT_GVRP:
      type = CN_GVRP_PDU;
      break;
    default:
      DEBUG ("IEEE reserved multicast %02X not supported\n", data[5]);
      return;
    }
    break;

  default:
    DEBUG ("spec frame code %02X not supported\n", code);
    return;
  }

  zmsg_t *msg = make_notify_message (type);
  put_port_id (msg, pid);
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
__send_reply (zmsg_t *reply, void *sock)
{
  zmsg_send (&reply, sock);
}

static inline void
__report_status (enum status code, void *sock)
{
  __send_reply (make_reply (code), sock);
}

static inline void
__report_ok (void *sock)
{
  __report_status (ST_OK, sock);
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

typedef void (*cmd_handler_t) (zmsg_t *, void *);

#define DECLARE_HANDLER(cmd)                    \
  static void handle_##cmd (zmsg_t *, void *)

#define DEFINE_HANDLER(cmd)                                 \
  static void handle_##cmd (zmsg_t *__args, void *__sock)

#define HANDLER(cmd) [cmd] = handle_##cmd

#define send_reply(reply) __send_reply ((reply), __sock)

#define report_status(status) __report_status ((status), __sock)

#define report_ok() __report_ok (__sock)

#define ARGS_SIZE (zmsg_size (__args))

#define POP_ARG_SZ(buf, size) (pop_size (buf, __args, size, 0))

#define POP_ARG(ptr) ({                         \
      typeof (ptr) __buf = ptr;                 \
      POP_ARG_SZ (__buf, sizeof (*__buf));      \
    })

#define POP_OPT_ARG_SZ(buf, size) (pop_size (buf, __args, size, 1))

#define POP_OPT_ARG(ptr) ({                     \
      typeof (ptr) __buf = ptr;                 \
      POP_OPT_ARG_SZ (__buf, sizeof (*__buf));  \
    })

#define FIRST_ARG (zmsg_first (__args))

DECLARE_HANDLER (CC_PORT_GET_STATE);
DECLARE_HANDLER (CC_PORT_SET_STP_STATE);
DECLARE_HANDLER (CC_PORT_SEND_FRAME);
DECLARE_HANDLER (CC_PORT_SHUTDOWN);
DECLARE_HANDLER (CC_PORT_BLOCK);
DECLARE_HANDLER (CC_PORT_FDB_FLUSH);
DECLARE_HANDLER (CC_PORT_SET_MODE);
DECLARE_HANDLER (CC_PORT_SET_ACCESS_VLAN);
DECLARE_HANDLER (CC_PORT_SET_NATIVE_VLAN);
DECLARE_HANDLER (CC_PORT_SET_SPEED);
DECLARE_HANDLER (CC_PORT_SET_DUPLEX);
DECLARE_HANDLER (CC_PORT_SET_MDIX_AUTO);
DECLARE_HANDLER (CC_PORT_SET_FLOW_CONTROL);
DECLARE_HANDLER (CC_PORT_GET_STATS);
DECLARE_HANDLER (CC_PORT_SET_RATE_LIMIT);
DECLARE_HANDLER (CC_PORT_SET_BANDWIDTH_LIMIT);
DECLARE_HANDLER (CC_PORT_SET_PROTECTED);
DECLARE_HANDLER (CC_PORT_DUMP_PHY_REG);
DECLARE_HANDLER (CC_SET_FDB_MAP);
DECLARE_HANDLER (CC_VLAN_ADD);
DECLARE_HANDLER (CC_VLAN_DELETE);
DECLARE_HANDLER (CC_VLAN_SET_DOT1Q_TAG_NATIVE);
DECLARE_HANDLER (CC_VLAN_SET_CPU);
DECLARE_HANDLER (CC_VLAN_SET_MAC_ADDR);
DECLARE_HANDLER (CC_VLAN_DUMP);
DECLARE_HANDLER (CC_MAC_OP);
DECLARE_HANDLER (CC_MAC_SET_AGING_TIME);
DECLARE_HANDLER (CC_MAC_LIST);
DECLARE_HANDLER (CC_MAC_FLUSH_DYNAMIC);
DECLARE_HANDLER (CC_QOS_SET_MLS_QOS_TRUST);
DECLARE_HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_COS);
DECLARE_HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_DSCP);
DECLARE_HANDLER (CC_QOS_SET_DSCP_PRIO);
DECLARE_HANDLER (CC_QOS_SET_COS_PRIO);
DECLARE_HANDLER (CC_GVRP_ENABLE);

static cmd_handler_t handlers[] = {
  HANDLER (CC_PORT_GET_STATE),
  HANDLER (CC_PORT_SET_STP_STATE),
  HANDLER (CC_PORT_SEND_FRAME),
  HANDLER (CC_PORT_SHUTDOWN),
  HANDLER (CC_PORT_BLOCK),
  HANDLER (CC_PORT_FDB_FLUSH),
  HANDLER (CC_PORT_SET_MODE),
  HANDLER (CC_PORT_SET_ACCESS_VLAN),
  HANDLER (CC_PORT_SET_NATIVE_VLAN),
  HANDLER (CC_PORT_SET_SPEED),
  HANDLER (CC_PORT_SET_DUPLEX),
  HANDLER (CC_PORT_SET_MDIX_AUTO),
  HANDLER (CC_PORT_SET_FLOW_CONTROL),
  HANDLER (CC_PORT_GET_STATS),
  HANDLER (CC_PORT_SET_RATE_LIMIT),
  HANDLER (CC_PORT_SET_BANDWIDTH_LIMIT),
  HANDLER (CC_PORT_SET_PROTECTED),
  HANDLER (CC_PORT_DUMP_PHY_REG),
  HANDLER (CC_SET_FDB_MAP),
  HANDLER (CC_VLAN_ADD),
  HANDLER (CC_VLAN_DELETE),
  HANDLER (CC_VLAN_SET_DOT1Q_TAG_NATIVE),
  HANDLER (CC_VLAN_SET_CPU),
  HANDLER (CC_VLAN_SET_MAC_ADDR),
  HANDLER (CC_VLAN_DUMP),
  HANDLER (CC_MAC_OP),
  HANDLER (CC_MAC_SET_AGING_TIME),
  HANDLER (CC_MAC_LIST),
  HANDLER (CC_MAC_FLUSH_DYNAMIC),
  HANDLER (CC_QOS_SET_MLS_QOS_TRUST),
  HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_COS),
  HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_DSCP),
  HANDLER (CC_QOS_SET_DSCP_PRIO),
  HANDLER (CC_QOS_SET_COS_PRIO),
  HANDLER (CC_GVRP_ENABLE)
};


static int
cmd_handler (zloop_t *loop, zmq_pollitem_t *pi, void *sock)
{
  zmsg_t *msg;
  command_t cmd;
  cmd_handler_t handler;
  enum status result;

  msg = zmsg_recv (cmd_sock);

  result = pop_size (&cmd, msg, sizeof (cmd), 0);
  if (result != ST_OK) {
    __report_status (result, sock);
    goto out;
  }

  if (cmd >= ARRAY_SIZE (handlers) ||
      (handler = handlers[cmd]) == NULL) {
    __report_status (ST_BAD_REQUEST, sock);
    goto out;
  }
  handler (msg, sock);

 out:
  zmsg_destroy (&msg);
  return 0;
}

static unsigned __TASKCONV
control_loop (GT_VOID *dummy)
{
  zloop_t *loop = zloop_new ();

  zmq_pollitem_t cmd_pi = { cmd_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &cmd_pi, cmd_handler, cmd_sock);

  zmq_pollitem_t inp_pi = { inp_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &inp_pi, cmd_handler, inp_sock);

  zloop_start (loop);

  return 0; /* Make the compiler happy. */
}


/*
 * Command handlers.
 */

DEFINE_HANDLER (CC_PORT_GET_STATE)
{
  port_id_t pid;
  enum status result;
  struct port_link_state state;

  result = POP_ARG (&pid);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  result = port_get_state (pid, &state);
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
  port_id_t pid;
  stp_id_t stp_id;
  stp_state_t state;
  enum status result;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&state);
  if (result != ST_OK)
    goto out;

  result = POP_OPT_ARG (&stp_id);
  switch (result) {
  case ST_OK:
    result = port_set_stp_state (pid, stp_id, state);
    break;

  case ST_DOES_NOT_EXIST:
    /* FIXME: set state for all STGs. */
    result = port_set_stp_state (pid, 0, state);
    break;

  default:
    break;
  }

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SEND_FRAME)
{
  port_id_t pid;
  struct port *port;
  size_t len;
  zframe_t *frame;
  enum status result = ST_BAD_FORMAT;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  if (!(port = port_ptr (pid))) {
    result = ST_BAD_VALUE;
    goto out;
  }

  if (ARGS_SIZE != 1) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  frame = zmsg_pop (__args); /* TODO: maybe add a macro for this. */
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
  port_id_t pid;
  bool_t shutdown;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&shutdown);
  if (result != ST_OK)
    goto out;

  result = port_shutdown (pid, shutdown);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_BLOCK)
{
  enum status result;
  port_id_t pid;
  struct port_block what;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&what);
  if (result != ST_OK)
    goto out;

  result = port_block (pid, &what);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_FDB_FLUSH)
{
  struct mac_age_arg arg;
  port_id_t pid;
  enum status result;

  /* TODO: support flush for specific STP instance. */

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  arg.vid = ALL_VLANS;
  arg.port = pid;
  result = mac_flush (&arg, GT_FALSE);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_SET_FDB_MAP)
{
  enum status result = ST_BAD_FORMAT;
  zframe_t *frame = FIRST_ARG;

  if (zframe_size (frame) != sizeof (stp_id_t) * 4096)
    goto out;

  result = vlan_set_fdb_map ((stp_id_t *) zframe_data (frame));

 out:
  report_status (result);
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
  port_id_t pid;
  port_mode_t mode;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = port_set_mode (pid, mode);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_ACCESS_VLAN)
{
  enum status result;
  port_id_t pid;
  vid_t vid;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = port_set_access_vid (pid, vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_NATIVE_VLAN)
{
  enum status result;
  port_id_t pid;
  vid_t vid;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = port_set_native_vid (pid, vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_SPEED)
{
  enum status result;
  port_id_t pid;
  struct port_speed_arg psa;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&psa);
  if (result != ST_OK)
    goto out;

  result = port_set_speed (pid, &psa);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_DUPLEX)
{
  enum status result;
  port_id_t pid;
  port_duplex_t duplex;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&duplex);
  if (result != ST_OK)
    goto out;

  result = port_set_duplex (pid, duplex);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_DUMP_PHY_REG)
{
  enum status result;
  port_id_t pid;
  uint16_t reg;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&reg);
  if (result != ST_OK)
    goto out;

  result = port_dump_phy_reg (pid, reg);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MAC_LIST)
{
  enum status result;
  vid_t vid;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto err;

  if (!vlan_valid (vid)) {
    result = ST_BAD_VALUE;
    goto err;
  }

  result = mac_list ();
  if (result != ST_OK)
    goto err;

  zmsg_t *reply = make_reply (ST_OK);
  data_encode_fdb_addrs (reply, vid);
  send_reply (reply);
  return;

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_MAC_FLUSH_DYNAMIC)
{
  enum status result;
  struct mac_age_arg aa;

  result = POP_ARG (&aa);
  if (result != ST_OK)
    goto out;

  result = mac_flush (&aa, GT_FALSE);

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

DEFINE_HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_COS)
{
  enum status result;
  port_id_t pid;
  bool_t trust;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&trust);
  if (result != ST_OK)
    goto out;

  result = qos_set_port_mls_qos_trust_cos (pid, trust);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_DSCP)
{
  enum status result;
  port_id_t pid;
  bool_t trust;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&trust);
  if (result != ST_OK)
    goto out;

  result = qos_set_port_mls_qos_trust_dscp (pid, trust);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_MDIX_AUTO)
{
  enum status result;
  port_id_t pid;
  bool_t mdix_auto;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&mdix_auto);
  if (result != ST_OK)
    goto out;

  result = port_set_mdix_auto (pid, mdix_auto);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_FLOW_CONTROL)
{
  enum status result;
  port_id_t pid;
  flow_control_t fc;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&fc);
  if (result != ST_OK)
    goto out;

  result = port_set_flow_control (pid, fc);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_GET_STATS)
{
  enum status result;
  port_id_t pid;
  struct port_stats stats;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto err;

  result = port_get_stats (pid, &stats);
  if (result != ST_OK)
    goto err;

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &stats, sizeof (stats));
  send_reply (reply);
  return;

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_RATE_LIMIT)
{
  enum status result;
  port_id_t pid;
  struct rate_limit limit;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&limit);
  if (result != ST_OK)
    goto out;

  result = port_set_rate_limit (pid, &limit);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_BANDWIDTH_LIMIT)
{
  enum status result;
  port_id_t pid;
  bps_t limit;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&limit);
  if (result != ST_OK)
    goto out;

  result = port_set_bandwidth_limit (pid, limit);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_PROTECTED)
{
  enum status result;
  port_id_t pid;
  bool_t protected;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&protected);
  if (result != ST_OK)
    goto out;

  result = port_set_protected (pid, protected);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_QOS_SET_DSCP_PRIO)
{
  enum status result = ST_BAD_FORMAT;
  zframe_t *frame = FIRST_ARG;
  int size;

  if (!frame)
    goto out;

  size = zframe_size (frame);
  if (size == 0 || size % sizeof (struct dscp_map))
    goto out;

  result = qos_set_dscp_prio
    (size / sizeof (struct dscp_map),
     (const struct dscp_map *) zframe_data (frame));

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_QOS_SET_COS_PRIO)
{
  enum status result = ST_BAD_FORMAT;
  zframe_t *frame = FIRST_ARG;
  int size;

  if (!frame)
    goto out;

  size = zframe_size (frame);
  if (size != sizeof (queue_id_t) * 8)
    goto out;

  result = qos_set_cos_prio ((const queue_id_t *) zframe_data (frame));

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_SET_CPU)
{
  enum status result;
  vid_t vid;
  bool_t cpu;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&cpu);
  if (result != ST_OK)
    goto out;

  result = vlan_set_cpu (vid, cpu);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_GVRP_ENABLE)
{
  enum status result;
  bool_t enable;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = wnct_enable_proto (WNCT_GVRP, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_SET_MAC_ADDR)
{
  enum status result = ST_BAD_FORMAT;
  struct pdsa_vlan_mac_addr *addr;
  zframe_t *frame = FIRST_ARG;

  if (!frame)
    goto out;

  if (zframe_size (frame) != sizeof (*addr))
    goto out;

  addr = (struct pdsa_vlan_mac_addr *) zframe_data (frame);
  vlan_set_mac_addr (addr->vid, addr->addr);
  result = ST_OK;

 out:
  report_status (result);
}
