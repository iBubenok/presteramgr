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
#include <linux/pdsa-mgmt.h>
#include <vlan.h>
#include <mac.h>
#include <sec.h>
#include <qos.h>
#include <zcontext.h>
#include <sys/prctl.h>
#include <debug.h>
#include <wnct.h>
#include <mcg.h>
#include <route.h>
#include <ret.h>
#include <monitor.h>
#include <control-utils.h>
#include <mgmt.h>
#include <rtbd.h>
#include <arpc.h>
#include <arpd.h>
#include <dgasp.h>
#include <diag.h>
#include <tipc.h>
#include <trunk.h>
#include <gif.h>
#include <pcl.h>
#include <ip.h>

#include <gtOs/gtOsTask.h>

static void *control_loop (void *);

static void *pub_sock;
static void *pub_arp_sock;
static void *pub_dhcp_sock;
static void *cmd_sock;
static void *inp_sock;
static void *inp_pub_sock;
static void *evt_sock;
static void *rtbd_sock;
static void *arpd_sock;
static void *sec_sock;


static void *
forwarder_thread (void *dummy)
{
  void *inp_sub_sock;

  inp_sub_sock = zsocket_new (zcontext, ZMQ_SUB);
  assert (inp_sub_sock);
  zsocket_connect (inp_sub_sock, INP_PUB_SOCK_EP);

  prctl(PR_SET_NAME, "ctl-forwarder", 0, 0, 0);

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

  uint64_t hwm=250;
  pub_arp_sock = zsocket_new (zcontext, ZMQ_PUB);
  assert (pub_arp_sock);
/*  rc = zmq_setsockopt(pub_arp_sock, ZMQ_HWM, &hwm, sizeof(hwm));
  assert(rc==0); */
  rc = zsocket_bind (pub_arp_sock, PUB_SOCK_ARP_EP);

  pub_dhcp_sock = zsocket_new (zcontext, ZMQ_PUB);
  assert (pub_dhcp_sock);
  rc = zmq_setsockopt(pub_dhcp_sock, ZMQ_HWM, &hwm, sizeof(hwm));
  assert(rc==0);
  rc = zsocket_bind (pub_dhcp_sock, PUB_SOCK_DHCP_EP);

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

  sec_sock = zsocket_new (zcontext, ZMQ_SUB);
  assert (sec_sock);
  zsocket_connect (sec_sock, SEC_PUBSUB_EP);

  rtbd_sock = zsocket_new (zcontext, ZMQ_PULL);
  assert (rtbd_sock);
  zsocket_connect (rtbd_sock, RTBD_NOTIFY_EP);

  arpd_sock = zsocket_new (zcontext, ZMQ_PULL);
  assert (arpd_sock);
  zsocket_bind (arpd_sock, ARPD_NOTIFY_EP);

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
notify_send_arp (zmsg_t **msg)
{
  zmsg_send (msg, pub_arp_sock);
}

static inline void
notify_send_dhcp (zmsg_t **msg)
{
  zmsg_send (msg, pub_dhcp_sock);
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

static void
control_notify_ip_sg_trap (port_id_t pid, struct pdsa_spec_frame *frame)
{
  if (pcl_source_guard_trap_enabled (pid)) {
    zmsg_t *sg_msg = make_notify_message (CN_SG_TRAP);
    put_vlan_id (sg_msg, frame->vid);
    put_port_id (sg_msg, pid);
    /* MAC: 6, 7, 8, 9 bytes */
    uint8_t src_mac[6];
    memcpy (src_mac, (frame->data)+6, 6);
    zmsg_addmem (sg_msg, src_mac, 6);
    /* IP: 26, 27, 28, 29 bytes */
    uint8_t src_ip[4];
    memcpy (src_ip, (frame->data)+26, 4);
    zmsg_addmem (sg_msg, src_ip, 4);

    pcl_source_guard_drop_enable(pid);
    DEBUG("packet trapped! enable drop! port #%d\r\n", pid);

    notify_send (&sg_msg);
  }
}

void
cn_port_vid_set (port_id_t pid, vid_t vid)
{
  zmsg_t *msg = make_notify_message (CN_INT_PORT_VID_SET);
  put_port_id (msg, pid);
  put_vlan_id (msg, vid);
  notify_send (&msg);
}

void
cn_mail (port_stack_role_t role, uint8_t *data, size_t len)
{
  zmsg_t *msg = make_notify_message (CN_MAIL);
  zmsg_addmem (msg, &role, sizeof (role));
  zmsg_addmem (msg, data, len);
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
DECLARE_HANDLER (CC_PORT_GET_TYPE);
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
DECLARE_HANDLER (CC_PORT_CLEAR_STATS);
DECLARE_HANDLER (CC_PORT_SET_RATE_LIMIT);
DECLARE_HANDLER (CC_PORT_SET_BANDWIDTH_LIMIT);
DECLARE_HANDLER (CC_PORT_SET_PROTECTED);
DECLARE_HANDLER (CC_PORT_SET_IGMP_SNOOP);
DECLARE_HANDLER (CC_PORT_SET_SFP_MODE);
DECLARE_HANDLER (CC_PORT_SET_XG_SFP_MODE);
DECLARE_HANDLER (CC_PORT_IS_XG_SFP_PRESENT);
DECLARE_HANDLER (CC_PORT_READ_XG_SFP_IDPROM);
DECLARE_HANDLER (CC_PORT_DUMP_PHY_REG);
DECLARE_HANDLER (CC_PORT_SET_PHY_REG);
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
DECLARE_HANDLER (CC_DGASP_ENABLE);
DECLARE_HANDLER (CC_DGASP_ADD_PACKET);
DECLARE_HANDLER (CC_DGASP_CLEAR_PACKETS);
DECLARE_HANDLER (CC_DGASP_PORT_OP);
DECLARE_HANDLER (CC_DGASP_SEND);
DECLARE_HANDLER (CC_802_3_SP_RX_ENABLE);
DECLARE_HANDLER (CC_PORT_VLAN_TRANSLATE);
DECLARE_HANDLER (CC_PORT_CLEAR_TRANSLATION);
DECLARE_HANDLER (CC_VLAN_SET_XLATE_TUNNEL);
DECLARE_HANDLER (CC_PORT_SET_TRUNK_VLANS);
DECLARE_HANDLER (CC_MAIL_TO_NEIGHBOR);
DECLARE_HANDLER (CC_STACK_PORT_GET_STATE);
DECLARE_HANDLER (CC_STACK_SET_DEV_MAP);
DECLARE_HANDLER (CC_DIAG_REG_READ);
DECLARE_HANDLER (CC_DIAG_BDC_SET_MODE);
DECLARE_HANDLER (CC_DIAG_BDC_READ);
DECLARE_HANDLER (CC_DIAG_BIC_SET_MODE);
DECLARE_HANDLER (CC_DIAG_BIC_READ);
DECLARE_HANDLER (CC_DIAG_DESC_READ);
DECLARE_HANDLER (CC_BC_LINK_STATE);
DECLARE_HANDLER (CC_STACK_TXEN);
DECLARE_HANDLER (CC_PORT_SET_VOICE_VLAN);
DECLARE_HANDLER (CC_WNCT_ENABLE_PROTO);
DECLARE_HANDLER (CC_GET_HW_PORTS);
DECLARE_HANDLER (CC_SET_HW_PORTS);
DECLARE_HANDLER (CC_TRUNK_SET_MEMBERS);
DECLARE_HANDLER (CC_GIF_TX);
DECLARE_HANDLER (CC_PORT_ENABLE_QUEUE);
DECLARE_HANDLER (CC_PORT_ENABLE_LBD);
DECLARE_HANDLER (CC_PORT_ENABLE_EAPOL);
DECLARE_HANDLER (CC_PORT_EAPOL_AUTH);
DECLARE_HANDLER (CC_DHCP_TRAP_ENABLE);
DECLARE_HANDLER (CC_ROUTE_MC_ADD);
DECLARE_HANDLER (CC_ROUTE_MC_DEL);
DECLARE_HANDLER (CC_VLAN_IGMP_SNOOP);
DECLARE_HANDLER (CC_VLAN_MC_ROUTE);
DECLARE_HANDLER (CC_PSEC_SET_MODE);
DECLARE_HANDLER (CC_PSEC_SET_MAX_ADDRS);
DECLARE_HANDLER (CC_PSEC_ENABLE);
DECLARE_HANDLER (CC_PORT_GET_SERDES_CFG);
DECLARE_HANDLER (CC_PORT_SET_SERDES_CFG);
DECLARE_HANDLER (CC_SOURCE_GUARD_ENABLE_TRAP);
DECLARE_HANDLER (CC_SOURCE_GUARD_DISABLE_TRAP);
DECLARE_HANDLER (CC_SOURCE_GUARD_ENABLE_DROP);
DECLARE_HANDLER (CC_SOURCE_GUARD_DISABLE_DROP);
DECLARE_HANDLER (CC_SOURCE_GUARD_ADD);
DECLARE_HANDLER (CC_SOURCE_GUARD_DELETE);
DECLARE_HANDLER (CC_USER_ACL_RULE);
DECLARE_HANDLER (CC_ARP_TRAP_ENABLE);
DECLARE_HANDLER (CC_INJECT_FRAME);
DECLARE_HANDLER (CC_PORT_SET_COMBO_PREFERRED_MEDIA);

static cmd_handler_t handlers[] = {
  HANDLER (CC_PORT_GET_STATE),
  HANDLER (CC_PORT_GET_TYPE),
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
  HANDLER (CC_PORT_CLEAR_STATS),
  HANDLER (CC_PORT_SET_RATE_LIMIT),
  HANDLER (CC_PORT_SET_BANDWIDTH_LIMIT),
  HANDLER (CC_PORT_SET_PROTECTED),
  HANDLER (CC_PORT_SET_IGMP_SNOOP),
  HANDLER (CC_PORT_SET_SFP_MODE),
  HANDLER (CC_PORT_SET_XG_SFP_MODE),
  HANDLER (CC_PORT_IS_XG_SFP_PRESENT),
  HANDLER (CC_PORT_READ_XG_SFP_IDPROM),
  HANDLER (CC_PORT_DUMP_PHY_REG),
  HANDLER (CC_PORT_SET_PHY_REG),
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
  HANDLER (CC_MON_SESSION_SET_DST),
  HANDLER (CC_DGASP_ENABLE),
  HANDLER (CC_DGASP_ADD_PACKET),
  HANDLER (CC_DGASP_CLEAR_PACKETS),
  HANDLER (CC_DGASP_PORT_OP),
  HANDLER (CC_DGASP_SEND),
  HANDLER (CC_802_3_SP_RX_ENABLE),
  HANDLER (CC_PORT_VLAN_TRANSLATE),
  HANDLER (CC_PORT_CLEAR_TRANSLATION),
  HANDLER (CC_VLAN_SET_XLATE_TUNNEL),
  HANDLER (CC_PORT_SET_TRUNK_VLANS),
  HANDLER (CC_MAIL_TO_NEIGHBOR),
  HANDLER (CC_STACK_PORT_GET_STATE),
  HANDLER (CC_STACK_SET_DEV_MAP),
  HANDLER (CC_DIAG_REG_READ),
  HANDLER (CC_DIAG_BDC_SET_MODE),
  HANDLER (CC_DIAG_BDC_READ),
  HANDLER (CC_DIAG_BIC_SET_MODE),
  HANDLER (CC_DIAG_BIC_READ),
  HANDLER (CC_DIAG_DESC_READ),
  HANDLER (CC_BC_LINK_STATE),
  HANDLER (CC_STACK_TXEN),
  HANDLER (CC_PORT_SET_VOICE_VLAN),
  HANDLER (CC_WNCT_ENABLE_PROTO),
  HANDLER (CC_GET_HW_PORTS),
  HANDLER (CC_SET_HW_PORTS),
  HANDLER (CC_TRUNK_SET_MEMBERS),
  HANDLER (CC_GIF_TX),
  HANDLER (CC_PORT_ENABLE_QUEUE),
  HANDLER (CC_PORT_ENABLE_LBD),
  HANDLER (CC_PORT_ENABLE_EAPOL),
  HANDLER (CC_PORT_EAPOL_AUTH),
  HANDLER (CC_DHCP_TRAP_ENABLE),
  HANDLER (CC_ROUTE_MC_ADD),
  HANDLER (CC_ROUTE_MC_DEL),
  HANDLER (CC_VLAN_IGMP_SNOOP),
  HANDLER (CC_VLAN_MC_ROUTE),
  HANDLER (CC_PSEC_SET_MODE),
  HANDLER (CC_PSEC_SET_MAX_ADDRS),
  HANDLER (CC_PSEC_ENABLE),
  HANDLER (CC_PORT_GET_SERDES_CFG),
  HANDLER (CC_PORT_SET_SERDES_CFG),
  HANDLER (CC_SOURCE_GUARD_ENABLE_TRAP),
  HANDLER (CC_SOURCE_GUARD_DISABLE_TRAP),
  HANDLER (CC_SOURCE_GUARD_ENABLE_DROP),
  HANDLER (CC_SOURCE_GUARD_DISABLE_DROP),
  HANDLER (CC_SOURCE_GUARD_ADD),
  HANDLER (CC_SOURCE_GUARD_DELETE),
  HANDLER (CC_USER_ACL_RULE),
  HANDLER (CC_ARP_TRAP_ENABLE),
  HANDLER (CC_INJECT_FRAME),
  HANDLER (CC_PORT_SET_COMBO_PREFERRED_MEDIA)
};

static int
evt_handler (zloop_t *loop, zmq_pollitem_t *pi, void *dummy)
{
  zmsg_t *msg = zmsg_recv (evt_sock);
  notify_send (&msg);
  return 0;
}

static int
secbr_handler (zloop_t *loop, zmq_pollitem_t *pi, void *dummy)
{
  zmsg_t *msg = zmsg_recv (sec_sock);
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

static int
arpd_handler (zloop_t *loop, zmq_pollitem_t *pi, void *dummy)
{
  zmsg_t *msg = zmsg_recv (arpd_sock);

  zframe_t *frame = zmsg_first (msg);
  rtbd_notif_t notif = *((rtbd_notif_t *) zframe_data (frame));
  switch (notif) {
  case ARPD_CN_IP_ADDR:
    frame = zmsg_next (msg);
    struct arpd_ip_addr_msg *iam =
      (struct arpd_ip_addr_msg *) zframe_data (frame);

DEBUG("arp_notify vid=%hu, ", iam->vid);
    arpc_set_mac_addr
      (iam->ip_addr, iam->vid, &iam->mac_addr[0], iam->port_id);
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

  zmq_pollitem_t sec_pi = { sec_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &sec_pi, secbr_handler, NULL);

  zmq_pollitem_t rtbd_pi = { rtbd_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &rtbd_pi, rtbd_handler, NULL);

  zmq_pollitem_t arpd_pi = { arpd_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &arpd_pi, arpd_handler, NULL);

  prctl(PR_SET_NAME, "ctl-loop", 0, 0, 0);

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

DEFINE_HANDLER (CC_PORT_GET_TYPE)
{
  port_id_t pid;
  port_type_t ptype;
  enum status result;

  result = POP_ARG (&pid);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  result = port_get_type (pid, &ptype);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &ptype, sizeof (ptype));
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

  if (is_stack_port (port)) {
    result = ST_BAD_STATE;
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
  uint16_t size_or_vid;

  result = POP_ARG (&size_or_vid);
  if (result != ST_OK)
    goto out;

  if (size_or_vid > 11000) { /* size */
    uint16_t size;
    size = size_or_vid - 11000;

    vid_t *arr = 0;
    arr = malloc (size);
    assert(arr);

    result = POP_ARG_SZ (arr, size);
    if (result != ST_OK)
      goto out;

    result = vlan_add_range (size / 2, arr);

    free (arr);
  } else { /* vid */
    result = vlan_add (size_or_vid);
  }

 out:
  DEBUG("CC_VLAN_ADD returns %d\r\n", result);
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_DELETE)
{
  enum status result;
  uint16_t size_or_vid;

  result = POP_ARG (&size_or_vid);
  if (result != ST_OK)
    goto out;

  if (size_or_vid > 11000) { /* size */
    uint16_t size;
    size = size_or_vid - 11000;

    vid_t *arr = 0;
    arr = malloc (size);
    assert(arr);

    result = POP_ARG_SZ (arr, size);
    if (result != ST_OK)
      goto out;

    result = vlan_delete_range (size / 2, arr);

    free (arr);
  } else { /* vid */
    result = vlan_delete (size_or_vid);
  }

 out:
  DEBUG("CC_VLAN_DELETE returns %d\r\n", result);
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

  if (!(vlan_valid (vid) || vid == SVC_VID)) {
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

DEFINE_HANDLER (CC_PORT_SET_SFP_MODE)
{
  enum status result;
  port_id_t pid;
  uint16_t mode;

  /* For some reason POP_ARG() doesn't work, using POP_ARG_SZ() instead.
   * Check it later */
  result = POP_ARG_SZ (&pid, sizeof (pid));
  if (result != ST_OK)
    goto err;

  result = POP_ARG_SZ (&mode, sizeof (mode));
  if (result != ST_OK)
    goto err;

  result = port_set_sfp_mode (pid, mode);
  if (result != ST_OK)
    goto err;

  zmsg_t *reply = make_reply (ST_OK);
  send_reply (reply);
  return;

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_XG_SFP_MODE)
{
  enum status result;
  port_id_t pid;
  uint16_t mode;

  /* For some reason POP_ARG() doesn't work, using POP_ARG_SZ() instead.
   * Check it later */
  result = POP_ARG_SZ (&pid, sizeof (pid));
  if (result != ST_OK)
    goto err;

  result = POP_ARG_SZ (&mode, sizeof (mode));
  if (result != ST_OK)
    goto err;

  result = port_set_xg_sfp_mode (pid, mode);
  if (result != ST_OK)
    goto err;

  zmsg_t *reply = make_reply (ST_OK);
  send_reply (reply);
  return;

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_IS_XG_SFP_PRESENT)
{
  enum status result;
  port_id_t pid;

  /* For some reason POP_ARG() doesn't work, using POP_ARG_SZ() instead.
   * Check it later */
  result = POP_ARG_SZ (&pid, sizeof (pid));
  if (result != ST_OK)
    goto err;

  bool_t ret = port_is_xg_sfp_present (pid);

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &ret, sizeof (ret));
  send_reply (reply);
  return;

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_READ_XG_SFP_IDPROM)
{
  enum status result;
  port_id_t pid;
  uint16_t addr;

  /* For some reason POP_ARG() doesn't work, using POP_ARG_SZ() instead.
   * Check it later */
  result = POP_ARG_SZ (&pid, sizeof (pid));
  if (result != ST_OK)
    goto err;

  result = POP_ARG_SZ (&addr, sizeof (addr));
  if (result != ST_OK)
    goto err;

  const int bufsz = 128;
  uint8_t *buf = port_read_xg_sfp_idprom (pid, addr);

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, buf, bufsz);
  send_reply (reply);
  free (buf);
  return;

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_DUMP_PHY_REG)
{
  enum status result;
  port_id_t pid;
  uint16_t page, reg, val;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto err;

  result = POP_ARG (&page);
  if (result != ST_OK)
    goto err;

  result = POP_ARG (&reg);
  if (result != ST_OK)
    goto err;

  else {
  result = port_dump_phy_reg (pid, page, reg, &val);
  if (result != ST_OK)
    goto err;
  }

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &val, sizeof (val));
  send_reply (reply);
  return;

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_PHY_REG)
{
  enum status result;
  port_id_t pid;
  uint16_t page, reg, val;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto err;

  result = POP_ARG (&page);
  if (result != ST_OK)
    goto err;

  result = POP_ARG (&reg);
  if (result != ST_OK)
    goto err;

  result = POP_ARG (&val);
  if (result != ST_OK)
    goto err;

  result = port_set_phy_reg (pid, page, reg, val);
  if (result != ST_OK)
    goto err;

  zmsg_t *reply = make_reply (ST_OK);
  send_reply (reply);
  return;

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_MAC_LIST)
{
  enum status result;
  vid_t vid;
  port_id_t pid;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto err;

  if (!(vid == ALL_VLANS || vlan_valid (vid))) {
    result = ST_BAD_VALUE;
    goto err;
  }

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto err;

  if (!(pid == ALL_PORTS || port_ptr (pid))) {
    result = ST_BAD_VALUE;
    goto err;
  }

  zmsg_t *reply = make_reply (ST_OK);
  data_encode_fdb_addrs (reply, vid, pid);
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

DEFINE_HANDLER (CC_PORT_CLEAR_STATS)
{
  enum status result;
  port_id_t pid;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto err;

  result = port_clear_stats (pid);
  if (result != ST_OK)
    goto err;

  zmsg_t *reply = make_reply (ST_OK);
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
  vid_t size_or_vid;
  bool_t cpu;

  result = POP_ARG (&size_or_vid);
  if (result != ST_OK)
    goto out;

  if ( size_or_vid > 11000 ) { /* size */
    uint16_t size = size_or_vid - 11000;
    vid_t *arr = 0;
    arr = malloc (size);
    assert(arr);

    result = POP_ARG_SZ (arr, size);
    if (result != ST_OK)
      goto out;

    result = POP_ARG (&cpu);
    if (result != ST_OK)
      goto out;

    result = vlan_set_cpu_range (size / 2, arr, cpu);

    free (arr);
  } else { /* vid */
    result = POP_ARG (&cpu);
    if (result != ST_OK)
      goto out;

    result = vlan_set_cpu (size_or_vid, cpu);
  }

 out:
  DEBUG("CC_VLAN_SET_CPU returns %d\r\n", result);
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
DEBUG("vlan_set_mac_addr (%hu, " MAC_FMT ")", addr->vid, MAC_ARG(addr->addr));
  vlan_set_mac_addr (addr->vid, addr->addr);
  arpc_send_set_mac_addr(addr->addr);
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

DEFINE_HANDLER (CC_INJECT_FRAME)
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

  mgmt_inject_frame (vid, zframe_data (frame), len);
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
  int put_vid = 0;
  uint16_t *etype;
  register int conform2stp_state = 0;

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

  result = ST_BAD_VALUE;

  switch (frame->code) {
  case CPU_CODE_IEEE_RES_MC_0_TM:
    switch (frame->data[5]) {
    case WNCT_STP:
      etype = (uint16_t *) &frame->data[12];
      switch (ntohs (*etype)) {
      case 0x88CC:
        type = CN_LLDP_MCAST;
        break;
      default:
        tipc_notify_bpdu (pid, frame->len, frame->data);
        result = ST_OK;
        goto out;
      }
      break;
    case WNCT_802_3_SP:
      switch (frame->data[14]) {
      case WNCT_802_3_SP_LACP:
        type = CN_LACPDU;
        conform2stp_state = 1;
        break;
      case WNCT_802_3_SP_OAM:
        type = CN_OAMPDU;
        conform2stp_state = 1;
        break;
      default:
        DEBUG ("IEEE 802.3 Slow Protocol subtype %02X not supported\n",
               frame->data[14]);
        goto out;
      }
      break;

    case WNCT_802_3_SP_OAM:
      etype = (uint16_t *) &frame->data[12];
      switch (ntohs (*etype)) {
      case 0x888E:
        type = CN_EAPOL;
        conform2stp_state = 1;
        break;
      case 0x88CC:
        type = CN_LLDP_MCAST;
        break;
      default:
        DEBUG ("Nearest Bridge ethertype %04X not supported\n", ntohs(*etype));
        goto out;
      }
      break;

    case WNCT_LLDP:
      type = CN_LLDP_MCAST;
      break;
    case WNCT_GVRP:
      type = CN_GVRP_PDU;
      conform2stp_state = 1;
      break;
    default:
      DEBUG ("IEEE reserved multicast %02X not supported\n",
             frame->data[5]);
      goto out;
    }
    break;

  case CPU_CODE_IPv4_IGMP_TM:
    type = CN_IPv4_IGMP_PDU;
    conform2stp_state = 1;
    // put_vid = 1;
    break;

  case CPU_CODE_ARP_BC_TM:
    type = CN_ARP_BROADCAST;
    conform2stp_state = 1;
    put_vid = 1;
    break;

  case CPU_CODE_ARP_REPLY_TO_ME:
    type = CN_ARP_REPLY_TO_ME;
    conform2stp_state = 1;
    put_vid = 1;
    break;

  case CPU_CODE_USER_DEFINED (0):
    type = CN_LBD_PDU;
    conform2stp_state = 1;
    break;

  case CPU_CODE_USER_DEFINED (1):
    type = CN_DHCP_TRAP;
    conform2stp_state = 1;
    put_vid = 1;
    break;

  case CPU_CODE_USER_DEFINED (2):
    result = ST_OK;
    if (! vlan_port_is_forwarding_on_vlan(pid, frame->vid))
      goto out;
    control_notify_ip_sg_trap (pid, frame);
    goto out;

  case CPU_CODE_USER_DEFINED (3):
    type = CN_ARP;
    conform2stp_state = 1;
    put_vid = 1;
    break;

  case CPU_CODE_IPv4_UC_ROUTE_TM_1:
    result = ST_OK;
    if (! vlan_port_is_forwarding_on_vlan(pid, frame->vid))
      goto out;
    route_handle_udt (frame->data, frame->len);
    goto out;

  case CPU_CODE_MAIL:
    stack_handle_mail (pid, frame->data, frame->len);
    result = ST_OK;
    goto out;

  default:
    DEBUG ("spec frame code %02X not supported\n", frame->code);
    goto out;
  }

  if (conform2stp_state)
    if (! vlan_port_is_forwarding_on_vlan(pid, frame->vid)) {
      result = ST_OK;
      goto out;
    }

  zmsg_t *msg = make_notify_message (type);
  if (put_vid)
    put_vlan_id (msg, frame->vid);
  put_port_id (msg, pid);

  // Mud mad hack  - added dirty duty hack? kif

  unsigned char buf[60];

  if ((type == CN_IPv4_IGMP_PDU) && (frame->len == 56)) {
    memset (buf, 0, 60);
    memcpy (buf, frame->data, frame->len);
    zmsg_addmem (msg, buf, 60);
  } else
    zmsg_addmem (msg, frame->data, frame->len);

  // End of Mud mad hack

  switch (type) {
    case CN_ARP_BROADCAST:
    case CN_ARP_REPLY_TO_ME:
    case CN_ARP:
      notify_send_arp (&msg);
      break;
    case CN_DHCP_TRAP:
      notify_send_dhcp (&msg);
      break;
    default:
      notify_send (&msg);
      break;
  }

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

DEFINE_HANDLER (CC_DGASP_ENABLE)
{
  enum status result;
  bool_t enable;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = dgasp_enable (enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_DGASP_ADD_PACKET)
{
  zframe_t *frame;
  enum status result = ST_BAD_FORMAT;

  if (ARGS_SIZE != 1)
    goto out;

  frame = FIRST_ARG;
  result = dgasp_add_packet (zframe_size (frame), zframe_data (frame));

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_DGASP_CLEAR_PACKETS)
{
  report_status (dgasp_clear_packets ());
}

DEFINE_HANDLER (CC_DGASP_PORT_OP)
{
  enum status result;
  port_id_t pid;
  bool_t add;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&add);
  if (result != ST_OK)
    goto out;

  result = dgasp_port_op (pid, add);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_DGASP_SEND)
{
  report_status (dgasp_send ());
}

DEFINE_HANDLER (CC_802_3_SP_RX_ENABLE)
{
  enum status result;
  bool_t enable;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = wnct_enable_proto (WNCT_802_3_SP, enable);

 out:
  report_status (result);

}

DEFINE_HANDLER (CC_PORT_VLAN_TRANSLATE)
{
  enum status result;
  port_id_t pid;
  vid_t from, to;
  bool_t enable;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&from);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&to);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = port_vlan_translate (pid, from, to, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MAIL_TO_NEIGHBOR)
{
  port_stack_role_t role;
  size_t len;
  zframe_t *frame;
  enum status result = ST_BAD_FORMAT;

  result = POP_ARG (&role);
  if (result != ST_OK)
    goto out;

  if (ARGS_SIZE != 1) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  frame = zmsg_pop (__args); /* TODO: maybe add a macro for this. */
  if ((len = zframe_size (frame)) < 1)
    goto destroy_frame;

  result = stack_mail (role, zframe_data (frame), len);

 destroy_frame:
  zframe_destroy (&frame);
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_CLEAR_TRANSLATION)
{
  enum status result;
  port_id_t pid;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = port_clear_translation (pid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_SET_XLATE_TUNNEL)
{
  enum status result;
  bool_t enable;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = vlan_set_xlate_tunnel (enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_TRUNK_VLANS)
{
  enum status result;
  port_id_t pid;
  zframe_t *frame;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  frame = zmsg_pop (__args);
  if (!frame || zframe_size (frame) != VLAN_BITMAP_SIZE) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  result = port_set_trunk_vlans (pid, zframe_data (frame));

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_STACK_PORT_GET_STATE)
{
   port_stack_role_t role;
   enum status result;
   uint8_t state;

   result = POP_ARG (&role);
   if (result != ST_OK) {
     report_status (result);
     return;
   }

   zmsg_t *reply = make_reply (ST_OK);
   state = stack_port_get_state (role);
   zmsg_addmem (reply, &state, sizeof (state));
   send_reply (reply);
}

DEFINE_HANDLER (CC_STACK_SET_DEV_MAP)
{
  uint8_t dev;
  enum status result;

  result = POP_ARG (&dev);
  if (result != ST_OK)
    goto out;

  result = ST_BAD_FORMAT;

  zframe_t *frame = zmsg_pop (__args);
  if (!frame)
    goto out;

  if (zframe_size (frame) != 2)
    goto destroy_frame;

  zframe_t *frame_pp = zmsg_pop(__args);
  if (!frame_pp)
    goto destroy_frame;

  uint8_t num_pp= * (uint8_t *) zframe_data(frame_pp);
  if (zframe_size(frame_pp) != 1 || num_pp < 1 || num_pp > 2)
    goto destroy_frame_pp;

  result = stack_set_dev_map (dev, zframe_data (frame), num_pp);

destroy_frame_pp:
  zframe_destroy(&frame_pp);
destroy_frame:
  zframe_destroy (&frame);
out:
  report_status (result);
}

DEFINE_HANDLER (CC_DIAG_REG_READ)
{
  uint32_t reg, val;
  enum status result;

  result = POP_ARG (&reg);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  result = diag_reg_read (reg, &val);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &val, sizeof (val));
  send_reply (reply);
}

DEFINE_HANDLER (CC_DIAG_BDC_SET_MODE)
{
  uint8_t mode;
  enum status result;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = diag_bdc_set_mode (mode);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_DIAG_BDC_READ)
{
  uint32_t val;
  enum status result;

  result = diag_bdc_read (&val);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &val, sizeof (val));
  send_reply (reply);
}

DEFINE_HANDLER (CC_DIAG_BIC_SET_MODE)
{
  uint8_t set, mode, port;
  vid_t vid;
  enum status result;

  result = POP_ARG (&set);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&port);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = diag_bic_set_mode (set, mode, port, vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_DIAG_BIC_READ)
{
  uint8_t set;
  uint32_t data[4];
  int i;
  enum status result;

  result = POP_ARG (&set);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  result = diag_bic_read (set, data);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  zmsg_t *reply = make_reply (ST_OK);
  for (i = 0; i < 4; i++)
    zmsg_addmem (reply, &data[i], sizeof (data[i]));
  send_reply (reply);
}

DEFINE_HANDLER (CC_DIAG_DESC_READ)
{
  uint8_t subj, valid;
  uint32_t data;
  enum status result;

  result = POP_ARG (&subj);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  result = diag_desc_read (subj, &valid, &data);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &valid, sizeof (valid));
  zmsg_addmem (reply, &data, sizeof (data));
  send_reply (reply);
}

DEFINE_HANDLER (CC_BC_LINK_STATE)
{
  tipc_bc_link_state ();
  report_status (ST_OK);
}

DEFINE_HANDLER (CC_STACK_TXEN)
{
  uint8_t dev;
  bool_t txen;
  enum status result;

  result = POP_ARG (&dev);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&txen);
  if (result != ST_OK)
    goto out;

  result = stack_txen (dev, txen);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_VOICE_VLAN)
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

  result = port_set_voice_vid (pid, vid);

   out:
  report_status (result);
}

DEFINE_HANDLER (CC_GET_HW_PORTS)
{
  static struct port_def pd[NPORTS];
  static int done = 0;
  enum status result = ST_OK;

  if (!done) {
    result = gif_get_hw_ports (pd);
    done = 1;
  }

  zmsg_t *reply = make_reply (result);
  if (result == ST_OK) {
    uint8_t n = NPORTS;
    zmsg_addmem (reply, &n, sizeof (n));
    zmsg_addmem (reply, pd, sizeof (pd));
  }
  send_reply (reply);
}

DEFINE_HANDLER (CC_SET_HW_PORTS)
{
  uint8_t d, n;
  struct port_def *pd = NULL;
  enum status result;

  result = POP_ARG (&d);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&n);
  if (result != ST_OK)
    goto out;

  if (n) {
    zframe_t *frame = FIRST_ARG;
    if (!frame) {
      result = ST_BAD_FORMAT;
      goto out;
    }
    pd = (struct port_def *) zframe_data (frame);
  }

  result = gif_set_hw_ports (d, n, pd);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_WNCT_ENABLE_PROTO)
{
  enum status result;
  wnct_t proto;
  bool_t enable;

  result = POP_ARG (&proto);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = wnct_enable_proto (proto, enable);
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_TRUNK_SET_MEMBERS)
{
  struct trunk_member mem[8];
  trunk_id_t id;
  enum status result;
  zframe_t *frame;
  int n = 0;

  result = POP_ARG (&id);
  if (result != ST_OK)
    goto out;

  result = ST_BAD_FORMAT;
  frame = FIRST_ARG;
  while (frame) {
    if (zframe_size (frame) != sizeof (struct trunk_member))
      goto out;
    memcpy (&mem[n++], zframe_data (frame), sizeof (struct trunk_member));
    if (n > 8)
      goto out;
    frame = NEXT_ARG;
  }

  result = trunk_set_members (id, n, mem);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_GIF_TX)
{
  zframe_t *frame;
  struct gif_id *id;
  struct gif_tx_opts *opts;
  enum status result = ST_BAD_FORMAT;

  frame = FIRST_ARG;
  if (zframe_size (frame) != sizeof (*id))
    goto out;
  id = (struct gif_id *) zframe_data (frame);

  frame = NEXT_ARG;
  if (zframe_size (frame) != sizeof (*opts))
    goto out;
  opts = (struct gif_tx_opts *) zframe_data (frame);

  frame = NEXT_ARG;
  result = gif_tx (id, opts, zframe_size (frame), zframe_data (frame));

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_ENABLE_QUEUE)
{
  port_id_t pid;
  uint8_t q;
  bool_t en;
  enum status result;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&q);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&en);
  if (result != ST_OK)
    goto out;

  result = port_enable_queue (pid, q, en);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_ENABLE_LBD)
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

  result = pcl_enable_lbd_trap (pid, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_ENABLE_EAPOL)
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

  result = port_enable_eapol (pid, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_EAPOL_AUTH)
{
  enum status result;
  port_id_t pid;
  vid_t vid;
  mac_addr_t mac;
  bool_t auth;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&mac);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&auth);
  if (result != ST_OK)
    goto out;

  result = port_eapol_auth (pid, vid, mac, auth);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_DHCP_TRAP_ENABLE)
{
  enum status result;
  bool_t enable;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = pcl_enable_dhcp_trap (enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_ROUTE_MC_ADD)
{
  enum status result;
  vid_t vid, src_vid;
  ip_addr_t d, s;
  mcg_t via;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&d);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&s);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&via);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&src_vid);
  if (result != ST_OK)
    goto out;

  DEBUG ("Route mc add src vid %d\n", src_vid);

  result = route_mc_add (vid, d, s, via, src_vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_ROUTE_MC_DEL)
{
  enum status result;
  vid_t vid, src_vid;
  ip_addr_t d, s;
  mcg_t via;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&d);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&s);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&via);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&src_vid);
  if (result != ST_OK)
    goto out;

  result = route_mc_del (vid, d, s, via, src_vid);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_IGMP_SNOOP)
{
  enum status result;
  vid_t vid;
  bool_t enable;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = vlan_igmp_snoop (vid, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_MC_ROUTE)
{
  enum status result;
  vid_t vid;
  bool_t enable;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = vlan_mc_route (vid, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PSEC_SET_MODE)
{
  enum status result;
  port_id_t pid;
  psec_mode_t mode;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = psec_set_mode (pid, mode);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PSEC_SET_MAX_ADDRS)
{
  enum status result;
  port_id_t pid;
  psec_max_addrs_t max;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&max);
  if (result != ST_OK)
    goto out;

  result = psec_set_max_addrs (pid, max);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PSEC_ENABLE)
{
  enum status result;
  port_id_t pid;
  bool_t enable;
  psec_action_t act;
  uint32_t intv;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&act);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&intv);
  if (result != ST_OK)
    goto out;

  result = psec_enable (pid, enable, act, intv);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_GET_SERDES_CFG)
{
  enum status result;
  port_id_t pid;
  struct port_serdes_cfg c;
  zmsg_t *reply;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = port_get_serdes_cfg (pid, &c);

out:
  reply = make_reply (result);
  if (result == ST_OK)
    zmsg_addmem (reply, &c, sizeof (c));
  send_reply (reply);
}

DEFINE_HANDLER (CC_PORT_SET_SERDES_CFG)
{
  enum status result;
  port_id_t pid;
  zframe_t *frame;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  frame = zmsg_pop (__args);
  if (!frame || zframe_size (frame) != sizeof (struct port_serdes_cfg)) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  result = port_set_serdes_cfg
    (pid, (struct port_serdes_cfg *) zframe_data (frame));

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_SOURCE_GUARD_ENABLE_TRAP)
{
  enum status result;
  port_id_t pid;


  if ((result = POP_ARG (&pid)) != ST_OK)
    goto out;
  DEBUG("CC_SOURCE_GUARD_ENABLE_TRAP port#%d\r\n", pid);

  pcl_source_guard_trap_enable (pid);
  result = ST_OK;
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_SOURCE_GUARD_DISABLE_TRAP)
{
  enum status result;
  port_id_t pid;


  if ((result = POP_ARG (&pid)) != ST_OK)
    goto out;
  DEBUG("CC_SOURCE_GUARD_DISABLE_TRAP port#%d\r\n", pid);

  pcl_source_guard_trap_disable (pid);
  result = ST_OK;
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_SOURCE_GUARD_ENABLE_DROP)
{
  enum status result;
  port_id_t pid;


  if ((result = POP_ARG (&pid)) != ST_OK)
    goto out;
  DEBUG("CC_SOURCE_GUARD_ENABLE_DROP port#%d\r\n", pid);

  pcl_source_guard_drop_enable (pid);
  result = ST_OK;
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_SOURCE_GUARD_DISABLE_DROP)
{
  enum status result;
  port_id_t pid;


  if ((result = POP_ARG (&pid)) != ST_OK)
    goto out;
  DEBUG("CC_SOURCE_GUARD_DISABLE_DROP port#%d\r\n", pid);

  pcl_source_guard_drop_disable (pid);
  result = ST_OK;
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_SOURCE_GUARD_ADD)
{
  enum status result;
  port_id_t pid;
  mac_addr_t mac;
  vid_t vid;
  ip_addr_t ip;
  uint16_t rule_ix;
  uint8_t verify_mac;


  if ((result = POP_ARG (&pid)) != ST_OK)
    goto out;
  if ((result = POP_ARG (&mac)) != ST_OK)
    goto out;
  if ((result = POP_ARG (&vid)) != ST_OK)
    goto out;
  if ((result = POP_ARG (&ip)) != ST_OK)
    goto out;
  if ((result = POP_ARG (&rule_ix)) != ST_OK)
    goto out;
  if ((result = POP_ARG (&verify_mac)) != ST_OK)
    goto out;
  DEBUG("CC_SOURCE_GUARD_ADD rule\r\n");

  pcl_source_guard_rule_set (pid, mac, vid, ip, rule_ix, verify_mac);
  result = ST_OK;
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_SOURCE_GUARD_DELETE)
{
  enum status result;
  port_id_t pid;
  uint16_t rule_ix;


  if ((result = POP_ARG (&pid)) != ST_OK)
    goto out;
  if ((result = POP_ARG (&rule_ix)) != ST_OK)
    goto out;
  DEBUG("CC_SOURCE_GUARD_DELETE rule\r\n");

  pcl_source_guard_rule_unset (pid, rule_ix);
  result = ST_OK;
 out:
  report_status (result);
}

DEFINE_HANDLER (CC_USER_ACL_RULE) {
  DEBUG("CC_USER_ACL_RULE\r\n");
  enum status result;
  port_id_t pid;
  uint8_t destination;
  uint8_t rule_type;
  bool_t enable;

  if ((result = POP_ARG (&pid)) != ST_OK) {
    DEBUG("ST_BAD_FORMAT: pid: %d\r\n", pid);
    goto out;
  }
  if ((result = POP_ARG (&destination)) != ST_OK) {
    DEBUG("ST_BAD_FORMAT: destination: %d\r\n", destination);
    goto out;
  }
  if ((result = POP_ARG (&rule_type)) != ST_OK) {
    DEBUG("ST_BAD_FORMAT: rule_type: %d\r\n", rule_type);
    goto out;
  }
  if ((result = POP_ARG (&enable)) != ST_OK) {
    DEBUG("ST_BAD_FORMAT: enable: %d\r\n", enable);
    goto out;
  }

  switch (rule_type) {
    case PCL_RULE_TYPE_IP: {
      struct ip_pcl_rule ip_rule;
      DEBUG("rule struct size: %d\r\n", sizeof(ip_rule));
      if ((result = POP_ARG (&ip_rule)) != ST_OK) {
        DEBUG("ST_BAD_FORMAT: rule\r\n");
        goto out;
      }
      pcl_ip_rule_set(pid, &ip_rule, destination, enable);
      break;
    }
    case PCL_RULE_TYPE_MAC: {
      struct mac_pcl_rule mac_rule;
      DEBUG("rule struct size: %d\r\n", sizeof(mac_rule));
      if ((result = POP_ARG (&mac_rule)) != ST_OK) {
        DEBUG("ST_BAD_FORMAT: rule\r\n");
        goto out;
      }
      pcl_mac_rule_set(pid, &mac_rule, destination, enable);
      break;
    }
    case PCL_RULE_TYPE_IPV6: {
      struct ipv6_pcl_rule ipv6_rule;
      DEBUG("rule struct size: %d\r\n", sizeof(ipv6_rule));
      if ((result = POP_ARG (&ipv6_rule)) != ST_OK) {
        DEBUG("ST_BAD_FORMAT: rule\r\n");
        goto out;
      }
      pcl_ipv6_rule_set(pid, &ipv6_rule, destination, enable);
      break;
    }
    case PCL_RULE_TYPE_DEFAULT: {
      struct default_pcl_rule default_rule;
      DEBUG("rule struct size: %d\r\n", sizeof(default_rule));
      if ((result = POP_ARG (&default_rule)) != ST_OK) {
        DEBUG("ST_BAD_FORMAT: rule\r\n");
        goto out;
      }
      pcl_default_rule_set(pid, &default_rule, destination, enable);
      break;
    }
    default:
      DEBUG("CC_USER_ACL_RULE: unknown rule type: %d\r\n", rule_type);
  };

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_ARP_TRAP_ENABLE)
{
  enum status result;
  bool_t enable;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  ip_arp_trap_enable (enable);
  result = pcl_enable_arp_trap (enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_COMBO_PREFERRED_MEDIA)
{
  enum status result;
  port_id_t pid;
  combo_pref_media_t media;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&media);
  if (result != ST_OK)
    goto out;

  result = port_set_combo_preferred_media (pid, media);

 out:
  report_status (result);
}
