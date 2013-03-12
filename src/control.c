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
#include <mcg.h>
#include <route.h>
#include <ret.h>
#include <monitor.h>
#include <control-utils.h>
#include <mgmt.h>
#include <rtbd.h>

#include <gtOs/gtOsTask.h>

static void *control_loop (void *);

static void *pub_sock;
static void *cmd_sock;
static void *inp_sock;
static void *inp_pub_sock;
static void *evt_sock;
static void *rtbd_sock;


static void *
forwarder_thread (void *dummy)
{
  void *inp_sub_sock;

  inp_sub_sock = zsocket_new (zcontext, ZMQ_SUB);
  assert (inp_sub_sock);
  zsocket_connect (inp_sub_sock, INP_PUB_SOCK_EP);

  DEBUG ("start forwarder device");
  zmq_device (ZMQ_FORWARDER, inp_sub_sock, pub_sock);

  return NULL;
}

int
control_init (void)
{
  int rc;
  pthread_t tid;

  pub_sock = zsocket_new (zcontext, ZMQ_PUB);
  assert (pub_sock);
  rc = zsocket_bind (pub_sock, PUB_SOCK_EP);

  inp_pub_sock = zsocket_new (zcontext, ZMQ_PUB);
  assert (inp_pub_sock);
  rc = zsocket_bind (inp_pub_sock, INP_PUB_SOCK_EP);

  pthread_create (&tid, NULL, forwarder_thread, NULL);

  cmd_sock = zsocket_new (zcontext, ZMQ_REP);
  assert (cmd_sock);
  rc = zsocket_bind (cmd_sock, CMD_SOCK_EP);

  inp_sock = zsocket_new (zcontext, ZMQ_REP);
  assert (inp_sock);
  rc = zsocket_bind (inp_sock, INP_SOCK_EP);

  evt_sock = zsocket_new (zcontext, ZMQ_SUB);
  assert (evt_sock);
  rc = zsocket_connect (evt_sock, EVENT_PUBSUB_EP);

  rtbd_sock = zsocket_new (zcontext, ZMQ_PULL);
  assert (rtbd_sock);
  zsocket_connect (rtbd_sock, RTBD_NOTIFY_EP);

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
  zmsg_send (msg, inp_pub_sock);
}

static inline void
put_port_id (zmsg_t *msg, port_id_t pid)
{
  zmsg_addmem (msg, &pid, sizeof (pid));
}

static inline void
put_stp_id (zmsg_t *msg, stp_id_t stp_id)
{
  zmsg_addmem (msg, &stp_id, sizeof (stp_id));
}

static inline void
put_vlan_id (zmsg_t *msg, vid_t vid)
{
  zmsg_addmem (msg, &vid, sizeof (vid));
}

static inline void
put_stp_state (zmsg_t *msg, stp_state_t state)
{
  zmsg_addmem (msg, &state, sizeof (state));
}

static void
control_notify_stp_state (port_id_t pid, stp_id_t stp_id,
                          enum port_stp_state state)
{
  zmsg_t *msg = make_notify_message (CN_STP_STATE);
  put_port_id (msg, pid);
  put_stp_id (msg, stp_id);
  put_stp_state (msg, state);
  notify_send (&msg);
}

void
cn_port_vid_set (port_id_t pid, vid_t vid)
{
  zmsg_t *msg = make_notify_message (CN_INT_PORT_VID_SET);
  put_port_id (msg, pid);
  put_vlan_id (msg, vid);
  notify_send (&msg);
}

int
control_start (void)
{
  pthread_t tid;

  pthread_create (&tid, NULL, control_loop, NULL);

  return 0;
}

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
DECLARE_HANDLER (CC_PORT_SET_IGMP_SNOOP);
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
DECLARE_HANDLER (CC_MAC_MC_IP_OP);
DECLARE_HANDLER (CC_QOS_SET_MLS_QOS_TRUST);
DECLARE_HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_COS);
DECLARE_HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_DSCP);
DECLARE_HANDLER (CC_QOS_SET_DSCP_PRIO);
DECLARE_HANDLER (CC_QOS_SET_COS_PRIO);
DECLARE_HANDLER (CC_GVRP_ENABLE);
DECLARE_HANDLER (CC_MCG_CREATE);
DECLARE_HANDLER (CC_MCG_DELETE);
DECLARE_HANDLER (CC_MCG_ADD_PORT);
DECLARE_HANDLER (CC_MCG_DEL_PORT);
DECLARE_HANDLER (CC_MGMT_IP_ADD);
DECLARE_HANDLER (CC_MGMT_IP_DEL);
DECLARE_HANDLER (CC_INT_ROUTE_ADD_PREFIX);
DECLARE_HANDLER (CC_INT_ROUTE_DEL_PREFIX);
DECLARE_HANDLER (CC_INT_SPEC_FRAME_FORWARD);
DECLARE_HANDLER (CC_SEND_FRAME);
DECLARE_HANDLER (CC_VLAN_SET_IP_ADDR);
DECLARE_HANDLER (CC_VLAN_DEL_IP_ADDR);
DECLARE_HANDLER (CC_ROUTE_SET_ROUTER_MAC_ADDR);
DECLARE_HANDLER (CC_PORT_SET_MRU);
DECLARE_HANDLER (CC_INT_RET_SET_MAC_ADDR);
DECLARE_HANDLER (CC_PORT_SET_PVE_DST);
DECLARE_HANDLER (CC_QOS_SET_PRIOQ_NUM);
DECLARE_HANDLER (CC_QOS_SET_WRR_QUEUE_WEIGHTS);
DECLARE_HANDLER (CC_PORT_TDR_TEST_START);
DECLARE_HANDLER (CC_PORT_TDR_TEST_GET_RESULT);
DECLARE_HANDLER (CC_PORT_SET_COMM);
DECLARE_HANDLER (CC_MON_SESSION_ADD);
DECLARE_HANDLER (CC_MON_SESSION_ENABLE);
DECLARE_HANDLER (CC_MON_SESSION_DEL);
DECLARE_HANDLER (CC_PORT_SET_CUSTOMER_VLAN);
DECLARE_HANDLER (CC_MON_SESSION_SET_SRCS);
DECLARE_HANDLER (CC_MON_SESSION_SET_DST);

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
  HANDLER (CC_PORT_SET_IGMP_SNOOP),
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
  HANDLER (CC_MAC_MC_IP_OP),
  HANDLER (CC_QOS_SET_MLS_QOS_TRUST),
  HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_COS),
  HANDLER (CC_QOS_SET_PORT_MLS_QOS_TRUST_DSCP),
  HANDLER (CC_QOS_SET_DSCP_PRIO),
  HANDLER (CC_QOS_SET_COS_PRIO),
  HANDLER (CC_GVRP_ENABLE),
  HANDLER (CC_MCG_CREATE),
  HANDLER (CC_MCG_DELETE),
  HANDLER (CC_MCG_ADD_PORT),
  HANDLER (CC_MCG_DEL_PORT),
  HANDLER (CC_MGMT_IP_ADD),
  HANDLER (CC_MGMT_IP_DEL),
  HANDLER (CC_INT_ROUTE_ADD_PREFIX),
  HANDLER (CC_INT_ROUTE_DEL_PREFIX),
  HANDLER (CC_INT_SPEC_FRAME_FORWARD),
  HANDLER (CC_SEND_FRAME),
  HANDLER (CC_VLAN_SET_IP_ADDR),
  HANDLER (CC_VLAN_DEL_IP_ADDR),
  HANDLER (CC_ROUTE_SET_ROUTER_MAC_ADDR),
  HANDLER (CC_PORT_SET_MRU),
  HANDLER (CC_INT_RET_SET_MAC_ADDR),
  HANDLER (CC_PORT_SET_PVE_DST),
  HANDLER (CC_QOS_SET_PRIOQ_NUM),
  HANDLER (CC_QOS_SET_WRR_QUEUE_WEIGHTS),
  HANDLER (CC_PORT_TDR_TEST_START),
  HANDLER (CC_PORT_TDR_TEST_GET_RESULT),
  HANDLER (CC_PORT_SET_COMM),
  HANDLER (CC_MON_SESSION_ADD),
  HANDLER (CC_MON_SESSION_ENABLE),
  HANDLER (CC_MON_SESSION_DEL),
  HANDLER (CC_PORT_SET_CUSTOMER_VLAN),
  HANDLER (CC_MON_SESSION_SET_SRCS),
  HANDLER (CC_MON_SESSION_SET_DST)
};

static int
evt_handler (zloop_t *loop, zmq_pollitem_t *pi, void *dummy)
{
  zmsg_t *msg = zmsg_recv (evt_sock);
  notify_send (&msg);
  return 0;
}

static int
rtbd_handler (zloop_t *loop, zmq_pollitem_t *pi, void *dummy)
{
  zmsg_t *msg = zmsg_recv (rtbd_sock);

  zframe_t *frame = zmsg_first (msg);
  rtbd_notif_t notif = *((rtbd_notif_t *) zframe_data (frame));
  switch (notif) {
  case RCN_IP_ADDR:
    frame = zmsg_next (msg);
    struct rtbd_ip_addr_msg *am = (struct rtbd_ip_addr_msg *) zframe_data (frame);
    ip_addr_t addr;
    memcpy (&addr, &am->addr, 4);
    switch (am->op) {
    case RIAO_ADD:
      route_add_mgmt_ip (addr);
      break;
    case RIAO_DEL:
      route_del_mgmt_ip (addr);
      break;
    default:
      break;
    }
    break;

  case RCN_ROUTE:
    frame = zmsg_next (msg);
    struct rtbd_route_msg *rm = (struct rtbd_route_msg *) zframe_data (frame);
    struct route rt;
    rt.pfx.addr.u32Ip = rm->dst;
    rt.pfx.alen = rm->dst_len;
    rt.gw.u32Ip = rm->gw;
    rt.vid = rm->vid;
    switch (rm->op) {
    case RRTO_ADD:
      route_add (&rt);
      break;
    case RRTO_DEL:
      route_del (&rt);
      break;
    default:
      break;
    }
    break;

  default:
    break;
  }

  zmsg_destroy (&msg);
  return 0;
}

static void *
control_loop (void *dummy)
{
  zloop_t *loop = zloop_new ();

  zmq_pollitem_t cmd_pi = { cmd_sock, 0, ZMQ_POLLIN };
  struct handler_data cmd_hd = { cmd_sock, handlers, ARRAY_SIZE (handlers) };
  zloop_poller (loop, &cmd_pi, control_handler, &cmd_hd);

  zmq_pollitem_t inp_pi = { inp_sock, 0, ZMQ_POLLIN };
  struct handler_data inp_hd = { inp_sock, handlers, ARRAY_SIZE (handlers) };
  zloop_poller (loop, &inp_pi, control_handler, &inp_hd);

  zmq_pollitem_t evt_pi = { evt_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &evt_pi, evt_handler, NULL);

  zmq_pollitem_t rtbd_pi = { rtbd_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &rtbd_pi, rtbd_handler, NULL);

  zloop_start (loop);

  return NULL;
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
    result = port_set_stp_state (pid, stp_id, 0, state);
    break;
  case ST_DOES_NOT_EXIST:
    stp_id = ALL_STP_IDS;
    result = port_set_stp_state (pid, 0, 1, state);
    break;
  default:
    break;
  }

  if (result == ST_OK)
    control_notify_stp_state (pid, stp_id, state);

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

  if (zframe_size (frame) != sizeof (stp_id_t) * NVLANS)
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

  if (!(vid == ALL_VLANS || vlan_valid (vid))) {
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

DEFINE_HANDLER (CC_PORT_SET_IGMP_SNOOP)
{
  enum status result;
  port_id_t pid;
  bool_t enable;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = port_set_igmp_snoop (pid, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MCG_CREATE)
{
  enum status result;
  mcg_t mcg;

  result = POP_ARG (&mcg);
  if (result != ST_OK)
    goto out;

  result = mcg_create (mcg);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MCG_DELETE)
{
  enum status result;
  mcg_t mcg;

  result = POP_ARG (&mcg);
  if (result != ST_OK)
    goto out;

  result = mcg_delete (mcg);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MCG_ADD_PORT)
{
  enum status result;
  mcg_t mcg;
  port_id_t pid;

  result = POP_ARG (&mcg);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = mcg_add_port (mcg, pid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MCG_DEL_PORT)
{
  enum status result;
  mcg_t mcg;
  port_id_t pid;

  result = POP_ARG (&mcg);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = mcg_del_port (mcg, pid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MAC_MC_IP_OP)
{
  enum status result;
  struct mc_ip_op_arg arg;

  result = POP_ARG (&arg);
  if (result != ST_OK)
    goto out;

  result = mac_mc_ip_op (&arg);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MGMT_IP_ADD)
{
  enum status result;
  ip_addr_t addr;

  result = POP_ARG (&addr);
  if (result != ST_OK)
    goto out;

  result = route_add_mgmt_ip (addr);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MGMT_IP_DEL)
{
  enum status result;
  ip_addr_t addr;

  result = POP_ARG (&addr);
  if (result != ST_OK)
    goto out;

  result = route_del_mgmt_ip (addr);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_INT_ROUTE_ADD_PREFIX)
{
  enum status result;
  struct route rt;

  result = POP_ARG (&rt);
  if (result != ST_OK)
    goto out;

  result = route_add (&rt);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_INT_ROUTE_DEL_PREFIX)
{
  enum status result;
  struct route rt;

  result = POP_ARG (&rt);
  if (result != ST_OK)
    goto out;

  result = route_del (&rt);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_SEND_FRAME)
{
  vid_t vid;
  size_t len;
  zframe_t *frame;
  enum status result = ST_BAD_FORMAT;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  if (ARGS_SIZE != 1) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  frame = zmsg_pop (__args); /* TODO: maybe add a macro for this. */
  if ((len = zframe_size (frame)) < 1)
    goto destroy_frame;

  mgmt_send_regular_frame (vid, zframe_data (frame), len);
  result = ST_OK;

 destroy_frame:
  zframe_destroy (&frame);
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_INT_SPEC_FRAME_FORWARD)
{
  enum status result;
  struct pdsa_spec_frame *frame;
  notification_t type;
  port_id_t pid;

  if (ARGS_SIZE != 1) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  frame = (struct pdsa_spec_frame *) zframe_data (FIRST_ARG);

  pid = port_id (frame->dev, frame->port);
  if (!pid) {
    result = ST_OK;
    goto out;
  }

  switch (frame->code) {
  case CPU_CODE_IEEE_RES_MC_0_TM:
    switch (frame->data[5]) {
    case WNCT_STP:
      type = CN_BPDU;
      break;
    case WNCT_GVRP:
      type = CN_GVRP_PDU;
      break;
    default:
      DEBUG ("IEEE reserved multicast %02X not supported\n", frame->data[5]);
      return;
    }
    break;

  case CPU_CODE_IPv4_IGMP_TM:
    type = CN_IPv4_IGMP_PDU;
    break;

  case CPU_CODE_ARP_BC_TM:
    type = CN_ARP_BROADCAST;
    break;

  case CPU_CODE_ARP_REPLY_TO_ME:
    type = CN_ARP_REPLY_TO_ME;
    break;

  case CPU_CODE_USER_DEFINED (0):
    type = CN_LBD_PDU;
    break;

  case CPU_CODE_IPv4_UC_ROUTE_TM_1:
    route_handle_udt (frame->data, frame->len);
    result = ST_OK;
    goto out;

  default:
    DEBUG ("spec frame code %02X not supported\n", frame->code);
    result = ST_BAD_VALUE;
    goto out;
  }

  zmsg_t *msg = make_notify_message (type);
  if (type == CN_ARP_REPLY_TO_ME)
    put_vlan_id (msg, frame->vid);
  put_port_id (msg, pid);
  zmsg_addmem (msg, frame->data, frame->len);
  notify_send (&msg);

  result = ST_OK;

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_SET_IP_ADDR)
{
  vid_t vid;
  ip_addr_t addr;
  enum status result;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&addr);
  if (result != ST_OK)
    goto out;

  result = vlan_set_ip_addr (vid, addr);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_DEL_IP_ADDR)
{
  vid_t vid;
  enum status result;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = vlan_del_ip_addr (vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_ROUTE_SET_ROUTER_MAC_ADDR)
{
  mac_addr_t addr;
  enum status result;

  result = POP_ARG (&addr);
  if (result != ST_OK)
    goto out;

  result = route_set_router_mac_addr (addr);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_MRU)
{
  uint16_t mru;
  enum status result;

  result = POP_ARG (&mru);
  if (result != ST_OK)
    goto out;

  result = port_set_mru (mru);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_INT_RET_SET_MAC_ADDR)
{
  enum status result;
  struct gw gw;
  port_id_t pid;
  zframe_t *frame;

  result = POP_ARG (&gw);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  frame = FIRST_ARG;

  result = ret_set_mac_addr
    (&gw, (const GT_ETHERADDR *) zframe_data (frame), pid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_PVE_DST)
{
  enum status result;
  port_id_t spid, dpid;
  bool_t enable;

  result = POP_ARG (&spid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&dpid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = port_set_pve_dst (spid, dpid, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_QOS_SET_PRIOQ_NUM)
{
  enum status result;
  uint8_t num;

  result = POP_ARG (&num);
  if (result != ST_OK)
    goto out;

  result = qos_set_prioq_num (num);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_QOS_SET_WRR_QUEUE_WEIGHTS)
{
  enum status result = ST_BAD_FORMAT;
  zframe_t *frame = FIRST_ARG;

  if (frame) {
    if (zframe_size (frame) != 8)
      goto out;

    result = qos_set_wrr_queue_weights (zframe_data (frame));
  } else
    result = qos_set_wrr_queue_weights (qos_default_wrr_weights);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_TDR_TEST_START)
{
  enum status result;
  port_id_t pid;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = port_tdr_test_start (pid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_TDR_TEST_GET_RESULT)
{
  enum status result;
  struct vct_cable_status cs;
  port_id_t pid;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto err;

  result = port_tdr_test_get_result (pid, &cs);
  if (result == ST_OK) {
    zmsg_t *reply = make_reply (ST_OK);
    zmsg_addmem (reply, &cs, sizeof (cs));
    send_reply (reply);
    return;
  }

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_COMM)
{
  enum status result;
  port_id_t pid;
  port_comm_t comm;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&comm);
  if (result != ST_OK)
    goto out;

  result = port_set_comm (pid, comm);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_CUSTOMER_VLAN)
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

  result = port_set_customer_vid (pid, vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MON_SESSION_ADD)
{
  enum status result;
  mon_session_t num;

  result = POP_ARG (&num);
  if (result != ST_OK)
    goto out;

  result = mon_session_add (num);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MON_SESSION_ENABLE)
{
  enum status result;
  mon_session_t num;
  bool_t enable;

  result = POP_ARG (&num);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = mon_session_enable (num, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MON_SESSION_DEL)
{
  enum status result;
  mon_session_t num;

  result = POP_ARG (&num);
  if (result != ST_OK)
    goto out;

  result = mon_session_del (num);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MON_SESSION_SET_SRCS)
{
  enum status result;
  mon_session_t num;
  zframe_t *frame;
  int nsrcs;

  result = POP_ARG (&num);
  if (result != ST_OK)
    goto out;

  frame = FIRST_ARG;
  if (!frame || ((nsrcs = zframe_size (frame)) % sizeof (struct mon_if))) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  result = mon_session_set_src
    (num, nsrcs / sizeof (struct mon_if),
     (const struct mon_if *) zframe_data (frame));

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MON_SESSION_SET_DST)
{
  enum status result;
  mon_session_t num;
  port_id_t pid;
  vid_t vid;

  result = POP_ARG (&num);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = mon_session_set_dst (num, pid, vid);

 out:
  report_status (result);
}
