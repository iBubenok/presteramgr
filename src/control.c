#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <zmq.h>
#include <czmq.h>
#include <assert.h>
#include <control.h>
#include <control-proto.h>
#include <vif.h>
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
#include <sys/prctl.h>
#include <debug.h>
#include <wnct.h>
#include <mcg.h>
#include <fib.h>
#include <fib_ipv6.h>
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
#include <dev.h>
#include <ipsg.h>
#include <netlink/socket.h>
#include <sflow.h>

#include <nht.h>
#include <fib.h>

#include <gtOs/gtOsTask.h>

static void *control_loop (void *);
static void *control_packet_loop (void *);

static void *pub_sock;
static void *pub_arp_sock;
static void *pub_sflow_sock;
static void *pub_dhcp_sock;
static void *pub_stack_sock;
static void *cmd_sock;
static void *pkt_sock;
static void *inp_sock;
static void *inp_pub_sock;
static void *evt_sock;
static void *rtbd_sock;
static void *arpd_sock;
static void *sec_sock;
static void *fdb_sock;
static void *stack_cmd_sock;
static void *evtntf_sock;

static void *
forwarder_thread (void *dummy)
{
  void *inp_sub_sock;

  inp_sub_sock = zsock_new (ZMQ_SUB);
  assert (inp_sub_sock);
  zsock_connect (inp_sub_sock, INP_PUB_SOCK_EP);
  zsock_set_subscribe (inp_sub_sock, "");

  prctl(PR_SET_NAME, "ctl-forwarder", 0, 0, 0);

  DEBUG ("start forwarder device");
  zmq_proxy (zsock_resolve(inp_sub_sock),
             zsock_resolve(pub_sock),
             NULL);

  return NULL;
}

static void *
event_forwarder_thread (void *dummy)
{
  void *event_pull_sock;
  int rc;

  event_pull_sock = zsock_new (ZMQ_PULL);
  assert (event_pull_sock);
  rc = zsock_bind (event_pull_sock, EVENT_SOCK_EP);
  assert (rc == 0);

  prctl(PR_SET_NAME, "event-forwarder", 0, 0, 0);

  DEBUG ("start event forwarder device");

  zloop_t *loop = zloop_new();

  zloop_reader (loop, event_pull_sock, event_forward, NULL);

  zloop_start(loop);

  return NULL;
}

int
event_forward (zloop_t *loop, zsock_t *event_pull_sock, void *arg)
{
  int rc;
  zmsg_t *msg = zmsg_recv (event_pull_sock);

  rc = zmsg_send(&msg, pub_sock);
  assert (rc == 0);

  zmsg_destroy (&msg);

  return 0;
}


void
control_pre_mac_init(void) {

  stack_cmd_sock = zsock_new (ZMQ_PULL);
  assert (stack_cmd_sock);
  zsock_bind (stack_cmd_sock, STACK_CMD_SOCK_EP);

  cmd_sock = zsock_new (ZMQ_REP);
  assert (cmd_sock);
  zsock_bind (cmd_sock, CMD_SOCK_EP);

  inp_sock = zsock_new (ZMQ_REP);
  assert (inp_sock);
  zsock_bind (inp_sock, INP_SOCK_EP);

}

int
control_init (void)
{
  int rc;
  pthread_t tid, event_forwarder_tid;

  pub_sock = zsock_new (ZMQ_PUB);
  assert (pub_sock);
  rc = zsock_bind (pub_sock, PUB_SOCK_EP);
  assert (rc == 0);

  pkt_sock = zsock_new (ZMQ_REP);
  assert (pkt_sock);
  zsock_bind (pkt_sock, PKT_SOCK_EP);
  assert (rc == 0);

  uint64_t hwm = 250;

  pub_arp_sock = zsock_new (ZMQ_PUB);
  assert (pub_arp_sock);
  rc = zsock_bind (pub_arp_sock, PUB_SOCK_ARP_EP);
  assert (rc == 0);

  pub_sflow_sock = zsock_new (ZMQ_PUB);
  assert (pub_sflow_sock);
  rc = zsock_bind (pub_sflow_sock, PUB_SOCK_SFLOW_EP);
  assert (rc == 0);

  pub_dhcp_sock = zsock_new (ZMQ_PUB);
  assert (pub_dhcp_sock);
  zsock_set_sndhwm(pub_dhcp_sock, hwm);
  zsock_set_rcvhwm(pub_dhcp_sock, hwm);
  rc = zsock_bind (pub_dhcp_sock, PUB_SOCK_DHCP_EP);
  assert (rc == 0);

  pub_stack_sock = zsock_new (ZMQ_PUB);
  assert (pub_stack_sock);
  rc = zsock_bind (pub_stack_sock, PUB_SOCK_STACK_MAIL_EP);
  assert (rc == 0);

  inp_pub_sock = zsock_new (ZMQ_PUB);
  assert (inp_pub_sock);
  rc = zsock_bind (inp_pub_sock, INP_PUB_SOCK_EP);
  assert (rc == 0);

  pthread_create (&tid, NULL, forwarder_thread, NULL);
  pthread_create (&event_forwarder_tid, NULL, event_forwarder_thread, NULL);

  evt_sock = zsock_new (ZMQ_SUB);
  assert (evt_sock);
  zsock_set_subscribe(evt_sock, "");
  rc = zsock_connect (evt_sock, EVENT_PUBSUB_EP);
  assert (rc == 0);

  sec_sock = zsock_new (ZMQ_SUB);
  assert (sec_sock);
  zsock_set_subscribe(sec_sock, "");
  rc = zsock_connect (sec_sock, SEC_PUBSUB_EP);
  assert (rc == 0);

  rtbd_sock = zsock_new (ZMQ_PULL);
  assert (rtbd_sock);
  rc = zsock_connect (rtbd_sock, RTBD_NOTIFY_EP);
  assert (rc == 0);

  arpd_sock = zsock_new (ZMQ_PULL);
  assert (arpd_sock);
  rc = zsock_bind (arpd_sock, ARPD_NOTIFY_EP);
  assert (rc == 0);

  fdb_sock = zsock_new (ZMQ_SUB);
  assert (fdb_sock);
  zsock_set_subscribe(fdb_sock, "");
  rc = zsock_connect (fdb_sock, FDB_PUBSUB_EP);
  assert (rc == 0);

  evtntf_sock = zsock_new (ZMQ_PUSH);
  assert (evtntf_sock);
  rc = zsock_connect (evtntf_sock, NOTIFY_QUEUE_EP);
  assert (rc == 0);

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

/*  also from here are sent ndp frame */
/*  ndp use frame: solicitation     - request
                   advertisement    - reply */
static inline void
notify_send_arp (zmsg_t **msg)
{
  DEBUG("notify_send_arp");
  zmsg_send (msg, pub_arp_sock);
}

static inline void
notify_send_sflow (zmsg_t **msg)
{
  zmsg_send (msg, pub_sflow_sock);
}

static inline void
notify_send_dhcp (zmsg_t **msg)
{
  zmsg_send (msg, pub_dhcp_sock);
}

static inline void
notify_send_stack (zmsg_t **msg)
{
  zmsg_send (msg, pub_stack_sock);
}

static inline void
put_port_id (zmsg_t *msg, port_id_t pid)
{
  zmsg_addmem (msg, &pid, sizeof (pid));
}

static inline void
put_vif_id (zmsg_t *msg, vif_id_t vifid)
{
  zmsg_addmem (msg, &vifid, sizeof (vifid));
}

static inline void
put_vlan_id (zmsg_t *msg, vid_t vid)
{
  zmsg_addmem (msg, &vid, sizeof (vid));
}

static inline void
put_pkt_info (zmsg_t *msg, struct pkt_info *info, notification_t type)
{
  switch (type)
  {
    case CN_OAMPDU:
      if (info->vif)
          put_vif_id (msg, info->vif);
      if (info->vid)
          put_vlan_id (msg, info->vid);
      put_port_id (msg, info->pid);
      break;
    default:
      zmsg_addmem (msg, info, sizeof( *info));
      break;
  }
}

static inline void
put_stp_id (zmsg_t *msg, stp_id_t stp_id)
{
  zmsg_addmem (msg, &stp_id, sizeof (stp_id));
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
  DEBUG ("%s",__FUNCTION__);
  if (pcl_source_guard_trap_enabled (pid)) {
    zmsg_t *sg_msg = make_notify_message (CN_SG_TRAP);
    put_vlan_id (sg_msg, frame->vid);
    put_port_id (sg_msg, pid);

    /* Src MAC: 6, 7, 8, 9, 10, 11 bytes */
    uint8_t src_mac[6];
    memcpy (src_mac, (frame->data) + 6, 6);
    zmsg_addmem (sg_msg, src_mac, 6);

    /* Src IP: 26, 27, 28, 29 bytes */
    uint8_t src_ip[4];
    uint8_t src_ip_offset = 26;

    if ( is_llc_snap_frame(frame->data, frame->len) ) {
      src_ip_offset += 8;
    }

    memcpy (src_ip, (frame->data) + src_ip_offset, 4);
    zmsg_addmem (sg_msg, src_ip, 4);

    DEBUG ("Trapped packet mac-address: %d:%d:%d:%d:%d:%d",
            src_mac[0],src_mac[1],src_mac[2],src_mac[3],src_mac[4],src_mac[5]);

    notify_trap_enabled (pid, frame->vid, src_mac, src_ip);

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
  notify_send_stack (&msg);
}

int
control_start (void)
{
  pthread_t tid, ptid;

  pthread_create (&tid, NULL, control_loop, NULL);
  pthread_create (&ptid, NULL, control_packet_loop, NULL);

  return 0;
}

DECLARE_HANDLER (CC_PORT_GET_STATE);
DECLARE_HANDLER (CC_PORT_GET_TYPE);
DECLARE_HANDLER (CC_PORT_SET_STP_STATE);
DECLARE_HANDLER (CC_PORT_GET_STP_STATE);
DECLARE_HANDLER (CC_VIF_SET_STP_STATE);
DECLARE_HANDLER (CC_PORT_SHUTDOWN);
DECLARE_HANDLER (CC_VIF_SHUTDOWN);
DECLARE_HANDLER (CC_PORT_BLOCK);
DECLARE_HANDLER (CC_VIF_BLOCK);
DECLARE_HANDLER (CC_PORT_FDB_FLUSH);
DECLARE_HANDLER (CC_VIF_FDB_FLUSH);
DECLARE_HANDLER (CC_PORT_SET_MODE);
DECLARE_HANDLER (CC_VIF_SET_MODE);
DECLARE_HANDLER (CC_PORT_SET_ACCESS_VLAN);
DECLARE_HANDLER (CC_VIF_SET_ACCESS_VLAN);
DECLARE_HANDLER (CC_PORT_SET_NATIVE_VLAN);
DECLARE_HANDLER (CC_VIF_SET_NATIVE_VLAN);
DECLARE_HANDLER (CC_PORT_SET_SPEED);
DECLARE_HANDLER (CC_VIF_SET_SPEED);
DECLARE_HANDLER (CC_PORT_SET_DUPLEX);
DECLARE_HANDLER (CC_VIF_SET_DUPLEX);
DECLARE_HANDLER (CC_PORT_SET_MDIX_AUTO);
DECLARE_HANDLER (CC_PORT_SET_FLOW_CONTROL);
DECLARE_HANDLER (CC_VIF_SET_FLOW_CONTROL);
DECLARE_HANDLER (CC_PORT_GET_STATS);
DECLARE_HANDLER (CC_PORT_CLEAR_STATS);
DECLARE_HANDLER (CC_PORT_SET_RATE_LIMIT);
DECLARE_HANDLER (CC_PORT_RATE_LIMIT_DROP_ENABLE);
DECLARE_HANDLER (CC_PORT_SET_TRAFFIC_SHAPE);
DECLARE_HANDLER (CC_PORT_SET_TRAFFIC_SHAPE_QUEUE);
DECLARE_HANDLER (CC_PORT_SET_PROTECTED);
DECLARE_HANDLER (CC_VIF_SET_PROTECTED);
DECLARE_HANDLER (CC_PORT_SET_IGMP_SNOOP);
DECLARE_HANDLER (CC_PORT_SET_SFP_MODE);
DECLARE_HANDLER (CC_PORT_SET_XG_SFP_MODE);
DECLARE_HANDLER (CC_PORT_SET_SOLICITED_CMD);
DECLARE_HANDLER (CC_PORT_ENABLE_SOLICITED);
DECLARE_HANDLER (CC_PORT_IS_XG_SFP_PRESENT);
DECLARE_HANDLER (CC_PORT_READ_XG_SFP_IDPROM);
DECLARE_HANDLER (CC_PORT_DUMP_PHY_REG);
DECLARE_HANDLER (CC_PORT_SET_PHY_REG);
DECLARE_HANDLER (CC_SET_FDB_MAP);
DECLARE_HANDLER (CC_VLAN_SET_STP_ID);
DECLARE_HANDLER (CC_VLAN_GET_STP_ID);
DECLARE_HANDLER (CC_VLAN_ADD);
DECLARE_HANDLER (CC_VLAN_DELETE);
DECLARE_HANDLER (CC_VLAN_SET_DOT1Q_TAG_NATIVE);
DECLARE_HANDLER (CC_VLAN_SET_CPU);
DECLARE_HANDLER (CC_VLAN_SET_MAC_ADDR);
DECLARE_HANDLER (CC_VLAN_DUMP);
DECLARE_HANDLER (CC_MAC_OP);
DECLARE_HANDLER (CC_MAC_OP_VIF);
DECLARE_HANDLER (CC_MAC_SET_AGING_TIME);
DECLARE_HANDLER (CC_MAC_LIST);
DECLARE_HANDLER (CC_MAC_LIST_VIF);
DECLARE_HANDLER (CC_MAC_FLUSH_DYNAMIC);
DECLARE_HANDLER (CC_MAC_FLUSH_DYNAMIC_VIF);
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
DECLARE_HANDLER (CC_MCG_ADD_VIF);
DECLARE_HANDLER (CC_MCG_DEL_VIF);
DECLARE_HANDLER (CC_MGMT_IP_ADD);
DECLARE_HANDLER (CC_MGMT_IP_DEL);
DECLARE_HANDLER (CC_INT_ROUTE_ADD_PREFIX);
DECLARE_HANDLER (CC_INT_ROUTE_DEL_PREFIX);
DECLARE_HANDLER (CC_INT_SPEC_FRAME_FORWARD);
DECLARE_HANDLER (CC_VLAN_SET_IP_ADDR);
DECLARE_HANDLER (CC_VLAN_DEL_IP_ADDR);
DECLARE_HANDLER (CC_ROUTE_SET_ROUTER_MAC_ADDR);
DECLARE_HANDLER (CC_PORT_SET_MRU);
DECLARE_HANDLER (CC_INT_RET_SET_MAC_ADDR);
DECLARE_HANDLER (CC_PORT_SET_PVE_DST);
DECLARE_HANDLER (CC_VIF_SET_PVE_DST);
DECLARE_HANDLER (CC_QOS_SET_PRIOQ_NUM);
DECLARE_HANDLER (CC_QOS_SET_WRR_QUEUE_WEIGHTS);
DECLARE_HANDLER (CC_QOS_SET_WRTD);
DECLARE_HANDLER (CC_PORT_TDR_TEST_START);
DECLARE_HANDLER (CC_PORT_TDR_TEST_GET_RESULT);
DECLARE_HANDLER (CC_PORT_SET_COMM);
DECLARE_HANDLER (CC_VIF_SET_COMM);
DECLARE_HANDLER (CC_MON_SESSION_ADD);
DECLARE_HANDLER (CC_MON_SESSION_ENABLE);
DECLARE_HANDLER (CC_MON_SESSION_DEL);
DECLARE_HANDLER (CC_PORT_SET_CUSTOMER_VLAN);
DECLARE_HANDLER (CC_VIF_SET_CUSTOMER_VLAN);
DECLARE_HANDLER (CC_MON_SESSION_SET_SRCS);
DECLARE_HANDLER (CC_MON_SESSION_SET_DST);
DECLARE_HANDLER (CC_DGASP_ENABLE);
DECLARE_HANDLER (CC_DGASP_ADD_PACKET);
DECLARE_HANDLER (CC_DGASP_CLEAR_PACKETS);
DECLARE_HANDLER (CC_DGASP_PORT_OP);
DECLARE_HANDLER (CC_DGASP_VIF_OP);
DECLARE_HANDLER (CC_DGASP_SEND);
DECLARE_HANDLER (CC_802_3_SP_RX_ENABLE);
DECLARE_HANDLER (CC_PORT_VLAN_TRANSLATE);
DECLARE_HANDLER (CC_VIF_VLAN_TRANSLATE);
DECLARE_HANDLER (CC_PORT_CLEAR_TRANSLATION);
DECLARE_HANDLER (CC_VLAN_SET_XLATE_TUNNEL);
DECLARE_HANDLER (CC_PORT_SET_TRUNK_VLANS);
DECLARE_HANDLER (CC_VIF_SET_TRUNK_VLANS);
DECLARE_HANDLER (CC_STACK_PORT_GET_STATE);
DECLARE_HANDLER (CC_STACK_SET_DEV_MAP);
DECLARE_HANDLER (CC_DIAG_REG_READ);
DECLARE_HANDLER (CC_DIAG_BDC_SET_MODE);
DECLARE_HANDLER (CC_DIAG_BDC_READ);
DECLARE_HANDLER (CC_DIAG_IPDC_SET_MODE);
DECLARE_HANDLER (CC_DIAG_IPDC_READ);
DECLARE_HANDLER (CC_DIAG_BIC_SET_MODE);
DECLARE_HANDLER (CC_DIAG_BIC_READ);
DECLARE_HANDLER (CC_DIAG_DESC_READ);
DECLARE_HANDLER (CC_DIAG_READ_RET_CNT);
DECLARE_HANDLER (CC_BC_LINK_STATE);
DECLARE_HANDLER (CC_STACK_TXEN);
DECLARE_HANDLER (CC_PORT_SET_VOICE_VLAN);
DECLARE_HANDLER (CC_VIF_SET_VOICE_VLAN);
DECLARE_HANDLER (CC_WNCT_ENABLE_PROTO);
DECLARE_HANDLER (CC_GET_HW_PORTS);
DECLARE_HANDLER (CC_SET_HW_PORTS);
DECLARE_HANDLER (CC_GET_VIF_PORTS);
DECLARE_HANDLER (CC_SET_VIF_PORTS);
DECLARE_HANDLER (CC_TRUNK_SET_MEMBERS);
DECLARE_HANDLER (CC_PORT_ENABLE_QUEUE);
DECLARE_HANDLER (CC_PORT_ENABLE_LBD);
DECLARE_HANDLER (CC_PORT_ENABLE_LLDP);
DECLARE_HANDLER (CC_PORT_ENABLE_LACP);
DECLARE_HANDLER (CC_PORT_ENABLE_EAPOL);
DECLARE_HANDLER (CC_VIF_ENABLE_EAPOL);
DECLARE_HANDLER (CC_PORT_EAPOL_AUTH);
DECLARE_HANDLER (CC_VIF_EAPOL_AUTH);
DECLARE_HANDLER (CC_PORT_FDB_NEW_ADDR_NOTIFY_ENABLE);
DECLARE_HANDLER (CC_PORT_FDB_ADDR_OP_NOTIFY_ENABLE);
DECLARE_HANDLER (CC_DHCP_TRAP_ENABLE);
DECLARE_HANDLER (CC_ROUTE_MC_ADD);
DECLARE_HANDLER (CC_ROUTE_MC_DEL);
DECLARE_HANDLER (CC_VLAN_IGMP_SNOOP);
DECLARE_HANDLER (CC_VLAN_SET_RSPAN);
DECLARE_HANDLER (CC_VLAN_MC_ROUTE);
DECLARE_HANDLER (CC_PSEC_SET_MODE);
DECLARE_HANDLER (CC_PSEC_SET_MAX_ADDRS);
DECLARE_HANDLER (CC_PSEC_ENABLE);
DECLARE_HANDLER (CC_PORT_GET_SERDES_CFG);
DECLARE_HANDLER (CC_PORT_SET_SERDES_CFG);
DECLARE_HANDLER (CC_GET_PORT_IP_SOURCEGUARD_RULE_START_IX);
DECLARE_HANDLER (CC_GET_PER_PORT_IP_SOURCEGUARD_RULES_COUNT);
DECLARE_HANDLER (CC_USER_ACL_SET);
DECLARE_HANDLER (CC_USER_ACL_RESET);
DECLARE_HANDLER (CC_USER_ACL_FAKE_MODE);
DECLARE_HANDLER (CC_USER_ACL_GET_COUNTER);
DECLARE_HANDLER (CC_USER_ACL_CLEAR_COUNTER);
DECLARE_HANDLER (CC_PCL_TEST_START);
DECLARE_HANDLER (CC_PCL_TEST_ITER);
DECLARE_HANDLER (CC_PCL_TEST_STOP);
DECLARE_HANDLER (CC_ARP_TRAP_ENABLE);
DECLARE_HANDLER (CC_PORT_SET_COMBO_PREFERRED_MEDIA);
DECLARE_HANDLER (CC_VRRP_SET_MAC);
DECLARE_HANDLER (CC_ARPD_SOCK_CONNECT);
DECLARE_HANDLER (CC_STACK_SET_MASTER);
DECLARE_HANDLER (CC_LOAD_BALANCE_MODE);
DECLARE_HANDLER (CC_INT_GET_RT_CMD);
DECLARE_HANDLER (CC_INT_GET_UDADDRS_CMD);
DECLARE_HANDLER (CC_GET_CH_REV);
DECLARE_HANDLER (CC_DIAG_DUMP_XG_PORT_QT2025_START);
DECLARE_HANDLER (CC_DIAG_DUMP_XG_PORT_QT2025_CHECK);
DECLARE_HANDLER (CC_DIAG_DUMP_XG_PORT_QT2025);
DECLARE_HANDLER (CC_SFLOW_SET_ENABLE);
DECLARE_HANDLER (CC_SFLOW_SET_INGRESS_COUNT_MODE);
DECLARE_HANDLER (CC_SFLOW_SET_RELOAD_MODE);
DECLARE_HANDLER (CC_SFLOW_SET_PORT_LIMIT);
DECLARE_HANDLER (CC_SFLOW_SET_DEFAULT);

DECLARE_HANDLER (SC_UPDATE_STACK_CONF);
DECLARE_HANDLER (SC_INT_RTBD_CMD);
DECLARE_HANDLER (SC_INT_NA_CMD);
DECLARE_HANDLER (SC_INT_NA_IPV6_CMD);
DECLARE_HANDLER (SC_INT_OPNA_CMD);
DECLARE_HANDLER (SC_INT_OPNA_IPV6_CMD);
DECLARE_HANDLER (SC_INT_UDT_CMD);
DECLARE_HANDLER (SC_INT_UDT_IPV6_CMD);
DECLARE_HANDLER (SC_INT_CLEAR_RT_CMD);
DECLARE_HANDLER (SC_INT_CLEAR_RE_CMD);
DECLARE_HANDLER (SC_INT_VIF_SET_STP_STATE);


static cmd_handler_t handlers[] = {
  HANDLER (CC_PORT_GET_STATE),
  HANDLER (CC_PORT_GET_TYPE),
  HANDLER (CC_PORT_SET_STP_STATE),
  HANDLER (CC_PORT_GET_STP_STATE),
  HANDLER (CC_VIF_SET_STP_STATE),
  HANDLER (CC_PORT_SHUTDOWN),
  HANDLER (CC_VIF_SHUTDOWN),
  HANDLER (CC_PORT_BLOCK),
  HANDLER (CC_VIF_BLOCK),
  HANDLER (CC_PORT_FDB_FLUSH),
  HANDLER (CC_VIF_FDB_FLUSH),
  HANDLER (CC_PORT_SET_MODE),
  HANDLER (CC_VIF_SET_MODE),
  HANDLER (CC_PORT_SET_ACCESS_VLAN),
  HANDLER (CC_VIF_SET_ACCESS_VLAN),
  HANDLER (CC_PORT_SET_NATIVE_VLAN),
  HANDLER (CC_VIF_SET_NATIVE_VLAN),
  HANDLER (CC_PORT_SET_SPEED),
  HANDLER (CC_VIF_SET_SPEED),
  HANDLER (CC_PORT_SET_DUPLEX),
  HANDLER (CC_VIF_SET_DUPLEX),
  HANDLER (CC_PORT_SET_MDIX_AUTO),
  HANDLER (CC_PORT_SET_FLOW_CONTROL),
  HANDLER (CC_VIF_SET_FLOW_CONTROL),
  HANDLER (CC_PORT_GET_STATS),
  HANDLER (CC_PORT_CLEAR_STATS),
  HANDLER (CC_PORT_SET_RATE_LIMIT),
  HANDLER (CC_PORT_RATE_LIMIT_DROP_ENABLE),
  HANDLER (CC_PORT_SET_TRAFFIC_SHAPE),
  HANDLER (CC_PORT_SET_TRAFFIC_SHAPE_QUEUE),
  HANDLER (CC_PORT_SET_PROTECTED),
  HANDLER (CC_VIF_SET_PROTECTED),
  HANDLER (CC_PORT_SET_IGMP_SNOOP),
  HANDLER (CC_PORT_SET_SFP_MODE),
  HANDLER (CC_PORT_SET_XG_SFP_MODE),
  HANDLER (CC_PORT_SET_SOLICITED_CMD),
  HANDLER (CC_PORT_ENABLE_SOLICITED),
  HANDLER (CC_PORT_IS_XG_SFP_PRESENT),
  HANDLER (CC_PORT_READ_XG_SFP_IDPROM),
  HANDLER (CC_PORT_DUMP_PHY_REG),
  HANDLER (CC_PORT_SET_PHY_REG),
  HANDLER (CC_SET_FDB_MAP),
  HANDLER (CC_VLAN_SET_STP_ID),
  HANDLER (CC_VLAN_GET_STP_ID),
  HANDLER (CC_VLAN_ADD),
  HANDLER (CC_VLAN_DELETE),
  HANDLER (CC_VLAN_SET_DOT1Q_TAG_NATIVE),
  HANDLER (CC_VLAN_SET_CPU),
  HANDLER (CC_VLAN_SET_MAC_ADDR),
  HANDLER (CC_VLAN_DUMP),
  HANDLER (CC_MAC_OP),
  HANDLER (CC_MAC_OP_VIF),
  HANDLER (CC_MAC_SET_AGING_TIME),
  HANDLER (CC_MAC_LIST),
  HANDLER (CC_MAC_LIST_VIF),
  HANDLER (CC_MAC_FLUSH_DYNAMIC),
  HANDLER (CC_MAC_FLUSH_DYNAMIC_VIF),
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
  HANDLER (CC_MCG_ADD_VIF),
  HANDLER (CC_MCG_DEL_VIF),
  HANDLER (CC_MGMT_IP_ADD),
  HANDLER (CC_MGMT_IP_DEL),
  HANDLER (CC_INT_ROUTE_ADD_PREFIX),
  HANDLER (CC_INT_ROUTE_DEL_PREFIX),
  HANDLER (CC_INT_SPEC_FRAME_FORWARD),
  HANDLER (CC_VLAN_SET_IP_ADDR),
  HANDLER (CC_VLAN_DEL_IP_ADDR),
  HANDLER (CC_ROUTE_SET_ROUTER_MAC_ADDR),
  HANDLER (CC_PORT_SET_MRU),
  HANDLER (CC_INT_RET_SET_MAC_ADDR),
  HANDLER (CC_PORT_SET_PVE_DST),
  HANDLER (CC_VIF_SET_PVE_DST),
  HANDLER (CC_QOS_SET_PRIOQ_NUM),
  HANDLER (CC_QOS_SET_WRR_QUEUE_WEIGHTS),
  HANDLER (CC_QOS_SET_WRTD),
  HANDLER (CC_PORT_TDR_TEST_START),
  HANDLER (CC_PORT_TDR_TEST_GET_RESULT),
  HANDLER (CC_PORT_SET_COMM),
  HANDLER (CC_VIF_SET_COMM),
  HANDLER (CC_MON_SESSION_ADD),
  HANDLER (CC_MON_SESSION_ENABLE),
  HANDLER (CC_MON_SESSION_DEL),
  HANDLER (CC_PORT_SET_CUSTOMER_VLAN),
  HANDLER (CC_VIF_SET_CUSTOMER_VLAN),
  HANDLER (CC_MON_SESSION_SET_SRCS),
  HANDLER (CC_MON_SESSION_SET_DST),
  HANDLER (CC_DGASP_ENABLE),
  HANDLER (CC_DGASP_ADD_PACKET),
  HANDLER (CC_DGASP_CLEAR_PACKETS),
  HANDLER (CC_DGASP_PORT_OP),
  HANDLER (CC_DGASP_VIF_OP),
  HANDLER (CC_DGASP_SEND),
  HANDLER (CC_802_3_SP_RX_ENABLE),
  HANDLER (CC_PORT_VLAN_TRANSLATE),
  HANDLER (CC_VIF_VLAN_TRANSLATE),
  HANDLER (CC_PORT_CLEAR_TRANSLATION),
  HANDLER (CC_VLAN_SET_XLATE_TUNNEL),
  HANDLER (CC_PORT_SET_TRUNK_VLANS),
  HANDLER (CC_VIF_SET_TRUNK_VLANS),
  HANDLER (CC_STACK_PORT_GET_STATE),
  HANDLER (CC_STACK_SET_DEV_MAP),
  HANDLER (CC_DIAG_REG_READ),
  HANDLER (CC_DIAG_BDC_SET_MODE),
  HANDLER (CC_DIAG_BDC_READ),
  HANDLER (CC_DIAG_IPDC_SET_MODE),
  HANDLER (CC_DIAG_IPDC_READ),
  HANDLER (CC_DIAG_BIC_SET_MODE),
  HANDLER (CC_DIAG_BIC_READ),
  HANDLER (CC_DIAG_DESC_READ),
  HANDLER (CC_DIAG_READ_RET_CNT),
  HANDLER (CC_BC_LINK_STATE),
  HANDLER (CC_STACK_TXEN),
  HANDLER (CC_PORT_SET_VOICE_VLAN),
  HANDLER (CC_VIF_SET_VOICE_VLAN),
  HANDLER (CC_WNCT_ENABLE_PROTO),
  HANDLER (CC_GET_HW_PORTS),
  HANDLER (CC_SET_HW_PORTS),
  HANDLER (CC_GET_VIF_PORTS),
  HANDLER (CC_SET_VIF_PORTS),
  HANDLER (CC_TRUNK_SET_MEMBERS),
  HANDLER (CC_PORT_ENABLE_QUEUE),
  HANDLER (CC_PORT_ENABLE_LBD),
  HANDLER (CC_PORT_ENABLE_LLDP),
  HANDLER (CC_PORT_ENABLE_LACP),
  HANDLER (CC_PORT_ENABLE_EAPOL),
  HANDLER (CC_VIF_ENABLE_EAPOL),
  HANDLER (CC_PORT_EAPOL_AUTH),
  HANDLER (CC_VIF_EAPOL_AUTH),
  HANDLER (CC_PORT_FDB_NEW_ADDR_NOTIFY_ENABLE),
  HANDLER (CC_PORT_FDB_ADDR_OP_NOTIFY_ENABLE),
  HANDLER (CC_DHCP_TRAP_ENABLE),
  HANDLER (CC_VLAN_MC_ROUTE),
  HANDLER (CC_ROUTE_MC_ADD),
  HANDLER (CC_ROUTE_MC_DEL),
  HANDLER (CC_VLAN_IGMP_SNOOP),
  HANDLER (CC_VLAN_SET_RSPAN),
  HANDLER (CC_VLAN_MC_ROUTE),
  HANDLER (CC_PSEC_SET_MODE),
  HANDLER (CC_PSEC_SET_MAX_ADDRS),
  HANDLER (CC_PSEC_ENABLE),
  HANDLER (CC_PORT_GET_SERDES_CFG),
  HANDLER (CC_PORT_SET_SERDES_CFG),
  HANDLER (CC_GET_PORT_IP_SOURCEGUARD_RULE_START_IX),
  HANDLER (CC_GET_PER_PORT_IP_SOURCEGUARD_RULES_COUNT),
  HANDLER (CC_USER_ACL_SET),
  HANDLER (CC_USER_ACL_RESET),
  HANDLER (CC_USER_ACL_FAKE_MODE),
  HANDLER (CC_USER_ACL_GET_COUNTER),
  HANDLER (CC_USER_ACL_CLEAR_COUNTER),
  HANDLER (CC_PCL_TEST_START),
  HANDLER (CC_PCL_TEST_ITER),
  HANDLER (CC_PCL_TEST_STOP),
  HANDLER (CC_ARP_TRAP_ENABLE),
  HANDLER (CC_PORT_SET_COMBO_PREFERRED_MEDIA),
  HANDLER (CC_VRRP_SET_MAC),
  HANDLER (CC_ARPD_SOCK_CONNECT),
  HANDLER (CC_STACK_SET_MASTER),
  HANDLER (CC_LOAD_BALANCE_MODE),
  HANDLER (CC_INT_GET_RT_CMD),
  HANDLER (CC_INT_GET_UDADDRS_CMD),
  HANDLER (CC_GET_CH_REV),
  HANDLER (CC_DIAG_DUMP_XG_PORT_QT2025_START),
  HANDLER (CC_DIAG_DUMP_XG_PORT_QT2025_CHECK),
  HANDLER (CC_DIAG_DUMP_XG_PORT_QT2025),
  HANDLER (CC_SFLOW_SET_ENABLE),
  HANDLER (CC_SFLOW_SET_INGRESS_COUNT_MODE),
  HANDLER (CC_SFLOW_SET_RELOAD_MODE),
  HANDLER (CC_SFLOW_SET_PORT_LIMIT),
  HANDLER (CC_SFLOW_SET_DEFAULT)
};

static cmd_handler_t stack_handlers[] = {
  HANDLER (SC_UPDATE_STACK_CONF),
  HANDLER (SC_INT_RTBD_CMD),
  HANDLER (SC_INT_NA_CMD),
  HANDLER (SC_INT_NA_IPV6_CMD),
  HANDLER (SC_INT_OPNA_CMD),
  HANDLER (SC_INT_OPNA_IPV6_CMD),
  HANDLER (SC_INT_UDT_CMD),
  HANDLER (SC_INT_UDT_IPV6_CMD),  
  HANDLER (SC_INT_CLEAR_RT_CMD),
  HANDLER (SC_INT_CLEAR_RE_CMD),
  HANDLER (SC_INT_VIF_SET_STP_STATE)
};

DECLARE_HANDLER (PC_PORT_SEND_FRAME);
DECLARE_HANDLER (PC_SEND_FRAME);
DECLARE_HANDLER (PC_MAIL_TO_NEIGHBOR);
DECLARE_HANDLER (PC_GIF_TX);
DECLARE_HANDLER (PC_VIF_TX);
DECLARE_HANDLER (PC_INJECT_FRAME);

static cmd_handler_t packet_handlers[] = {
  HANDLER (PC_PORT_SEND_FRAME),
  HANDLER (PC_SEND_FRAME),
  HANDLER (PC_MAIL_TO_NEIGHBOR),
  HANDLER (PC_GIF_TX),
  HANDLER (PC_VIF_TX),
  HANDLER (PC_INJECT_FRAME)
};

static int
evt_handler (zloop_t *loop, zsock_t* reader, void *dummy)
{
  zmsg_t *msg = zmsg_recv (evt_sock);
  notify_send (&msg);
  return 0;
}

__attribute__ ((unused))
static int
secbr_handler (zloop_t *loop, zsock_t* reader, void *dummy)
{
  zmsg_t *msg = zmsg_recv (sec_sock);
  notify_send (&msg);
  return 0;
}

__attribute__ ((unused))
static int
fdb_handler (zloop_t *loop, zsock_t* reader, void *dummy)
{
  zmsg_t *msg = zmsg_recv (fdb_sock);
  notify_send (&msg);
  return 0;
}

static int
rtbd_handler (zloop_t *loop, zsock_t* reader, void *dummy)
{
  zmsg_t *msg = zmsg_recv (rtbd_sock);

  zframe_t *frame = zmsg_first (msg);
  rtbd_notif_t notif = *((rtbd_notif_t *) zframe_data (frame));
  switch (notif) {
  case RCN_IP_ADDR:
    frame = zmsg_next (msg);
    struct rtbd_ip_addr_msg *am = (struct rtbd_ip_addr_msg *) zframe_data (frame);

    ip_addr_t addr;
    ip_addr_v6_t addr_v6;
    mac_op_rt(notif, am, sizeof(*am));
    switch (am->type){
      case AF_INET:
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

      case AF_INET6:
        memcpy (&addr_v6, &am->addr_v6, 16);
        switch (am->op) {
          case RIAO_ADD:
            DEBUG ("route_add_mgmt_ipv6\n");
            route_add_mgmt_ipv6 (addr_v6);
            break;
          case RIAO_DEL:
            DEBUG ("route_del_mgmt_ipv6\n");
            route_del_mgmt_ipv6 (addr_v6);
            break;
          default:
            break;
        }
        return 0;
        break;

      default:
        break;
    }
    break;

  case RCN_ROUTE:
    if (stack_id != master_id) {
    DEBUG("RTBD DROP!!!!\n");
      zmsg_destroy (&msg);
      return ST_OK;
    }

    frame = zmsg_next (msg);
    struct rtbd_route_msg *rm = (struct rtbd_route_msg *) zframe_data (frame);

    mac_op_rt(notif, rm, sizeof(*rm));

    struct route rt;
    rt.pfx.addr.u32Ip = rm->dst;
    // rt.pfx.addrv6.arIP = rm->dst_v6;
    memcpy (&rt.pfx.addrv6.arIP, &rm->dst_v6, 16);
    rt.pfx.alen = rm->dst_len;
    rt.gw.u32Ip = rm->gw;
    memcpy (&rt.gw_v6.arIP, &rm->gw_v6, 16);
    rt.vid = rm->vid;
    switch (rm->type)
    {
      case AF_INET:
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

      case AF_INET6:
        switch (rm->op) {
        case RRTO_ADD:
          DEBUG ("route_add_v6\n");
          route_add_v6 (&rt);
          break;
        case RRTO_DEL:
          DEBUG ("route_del_v6\n");
          route_del_v6 (&rt);
          break;
        default:
          break;
        }
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
arpd_handler (zloop_t *loop, zsock_t* reader, void *dummy)
{
  zmsg_t *msg = zmsg_recv (arpd_sock);

  zframe_t *frame = zmsg_first (msg);
  rtbd_notif_t notif = *((rtbd_notif_t *) zframe_data (frame));
  switch (notif) {
  case ARPD_CN_IP_ADDR:
    frame = zmsg_next (msg);
    struct arpd_ip_addr_msg *iam =
      (struct arpd_ip_addr_msg *) zframe_data (frame);

    mac_op_na(iam);

    arpc_set_mac_addr
      (iam->ip_addr, iam->vid, &iam->mac_addr[0], iam->vif_id);
    break;
  case NDPD_CN_IP_ADDR:
    frame = zmsg_next (msg);
    struct ndp_ip_addr_msg *iam_ndp =
      (struct ndp_ip_addr_msg *) zframe_data (frame);

    mac_op_na_ipv6(iam_ndp);

    ndpc_set_mac_addr
      (&iam_ndp->ip_addr[0], iam_ndp->vid, &iam_ndp->mac_addr[0], iam_ndp->vif_id);
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

  struct handler_data stack_cmd_hd = { stack_cmd_sock, stack_handlers, ARRAY_SIZE (stack_handlers) };
  zloop_reader (loop, stack_cmd_sock, control_handler, &stack_cmd_hd);

  struct handler_data cmd_hd = { cmd_sock, handlers, ARRAY_SIZE (handlers) };
  zloop_reader (loop, cmd_sock, control_handler, &cmd_hd);

  struct handler_data inp_hd = { inp_sock, handlers, ARRAY_SIZE (handlers) };
  zloop_reader (loop, inp_sock, control_handler, &inp_hd);

  zloop_reader (loop, evt_sock, evt_handler, NULL);

  zloop_reader (loop, sec_sock, secbr_handler, NULL);

  zloop_reader (loop, rtbd_sock, rtbd_handler, NULL);

  zloop_reader (loop, arpd_sock, arpd_handler, NULL);

  zloop_reader (loop, fdb_sock, fdb_handler, NULL);

  prctl(PR_SET_NAME, "ctl-loop", 0, 0, 0);

  zloop_start (loop);

  return NULL;
}

static void *
control_packet_loop (void *dummy)
{
  zloop_t *loop = zloop_new ();

  struct handler_data pkt_hd = { pkt_sock, packet_handlers, ARRAY_SIZE (packet_handlers) };
  zloop_reader (loop, pkt_sock, control_handler, &pkt_hd);

  prctl(PR_SET_NAME, "pktctl-loop", 0, 0, 0);

  zloop_start (loop);

  return NULL;
}

enum status
control_spec_frame (struct pdsa_spec_frame *frame) {
  enum status result;
  notification_t type;
  port_id_t pid;
  int put_vid = 0, put_vif = 0;
  uint16_t *etype;
  register int conform2stp_state = 0;
  int check_source_mac = 0;
  struct vif *vif;
  vif_id_t vifid;
  int is_vif_forwarding_on_vlan;
  vid_t vid = frame->vid;
  int put_direction = 0;
  sflow_type_t direction;
  int put_len = 0;
  uint16_t len;

  vif_rlock();

  vif = vif_by_hw(frame->dev, frame->port);
  if (!vif && frame->port != CPSS_CPU_PORT_NUM_CNS) {  /* TODO CPU port case */
    vif_unlock();
    DEBUG("!vif %d:%d\n", frame->dev, frame->port);
    result = ST_OK;
    goto out;
  }
  if (!vif->valid) {
    vif_unlock();
    result = ST_OK;
    goto out;
  }

  if (vif && vif->trunk)
    vif = vif->trunk;

  if (vid == 0) /* priority tagged */
    vid = vif_vid (vif);

  vifid = vif->id;
  is_vif_forwarding_on_vlan = vif_is_forwarding_on_vlan(vif, vid);

  vif_unlock();

  pid = port_id (frame->dev, frame->port);
  if (!pid && frame->port != CPSS_CPU_PORT_NUM_CNS) {
//DEBUG("!!!!!!pid: %d:%d\n", frame->dev, frame->port);
//DEBUG(MAC_FMT " <- " MAC_FMT "\n", MAC_ARG(((char*)frame->data)), MAC_ARG(((char*)frame->data + 6)));
    if (frame->dev != stack_id && frame->dev != stack_id + NEXTDEV_INC) {
      pid = port_id((frame->dev < 16)? stack_id : stack_id + NEXTDEV_INC, frame->port);
//DEBUG("!!!pid = %d\n ", pid);
    }
    if (!pid) {
      result = ST_OK;
//DEBUG("!!!pid outing\n ");
      goto out;
    }
  }

  result = ST_BAD_VALUE;

  switch (frame->code) {
  case CPU_CODE_IEEE_RES_MC_0_TM:
    switch (frame->data[5]) {
    case WNCT_STP:
      etype = (uint16_t *) &frame->data[12];
      switch (ntohs (*etype)) {
      case 0x88CC:
        DEBUG("LLDP shouldnt go there!!!!\n");
        result = ST_OK;
        goto out;
      default:
        tipc_notify_bpdu (vifid, pid, vid, frame->tagged, frame->len, frame->data);
        result = ST_OK;
        goto out;
      }
      break;
    case WNCT_802_3_SP:
      DEBUG("Slow Protocols shouldnt go there!!!!\n");
      result = ST_OK;
      goto out;

    case WNCT_802_3_SP_OAM:
      etype = (uint16_t *) &frame->data[12];
      switch (ntohs (*etype)) {
      case 0x888E:
        type = CN_EAPOL;
        conform2stp_state = 1;
        break;
      case 0x88CC:
        DEBUG("LLDP shouldnt go there!!!!\n");
        result = ST_OK;
        goto out;
      default:
        DEBUG ("Nearest Bridge ethertype %04X not supported\n", ntohs(*etype));
        goto out;
      }
      break;

    case WNCT_LLDP:
      DEBUG("LLDP shouldnt go there!!!!\n");
      result = ST_OK;
      goto out;
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

  case CPU_CODE_CISCO_MC_TM:
    /* frame des MAC is 01:00:0C:xx:xx:xx */
    if (frame->data[3] == 0xCC && frame->data[4] == 0xCC && frame->data[5] == 0xCC) {
      /* frame des MAC is 01:00:0C:CC:CC:CC */
      if (frame->data[20] == 0x01 && frame->data[21] == 0x04) {
        /* frame SNAP proto is 0x0104: Port Aggregation Protocol */
        DEBUG("Cisco Port Aggregation Protocol not supported\n");
        goto out;
      } else if (frame->data[20] == 0x01 && frame->data[21] == 0x11) {
        /* frame SNAP proto is 0x0111: Unidirectional Link Detection */
        type = CN_UDLD;
        put_vif = 1;
        conform2stp_state = 0;
        break;
      } else if (frame->data[20] == 0x20 && frame->data[21] == 0x00) {
        /* frame SNAP proto is 0x2000: Cisco Discovery Protocol */
        DEBUG("Cisco Discovery Protocol not supported\n");
        goto out;
      } else if (frame->data[20] == 0x20 && frame->data[21] == 0x04) {
        /* frame SNAP proto is 0x2004: Dynamic Trunking */
        DEBUG("Cisco Dynamic Trunking not supported\n");
        goto out;
      } else if (frame->data[20] == 0x20 && frame->data[21] == 0x03) {
        /* frame SNAP proto is 0x2003: VLAN Trunking */
        DEBUG("Cisco VLAN Trunking not supported\n");
        goto out;
      } else {
        DEBUG("Cisco 01:00:0C:CC:CC:CC SNAP protocol 0x%02X%02X not supported\n",
              frame->data[20], frame->data[21]);
        goto out;
      }
    } else if (frame->data[3] == 0xCC && frame->data[4] == 0xCC && frame->data[5] == 0xCD) {
      /* frame des MAC is 01:00:0C:CC:CC:CD */
      if (frame->data[20] == 0x01 && frame->data[21] == 0x0B) {
        /* frame SNAP proto is 0x010B: Spanning Tree PVSTP+ */
        tipc_notify_bpdu (vifid, pid, vid, frame->tagged, frame->len, frame->data);
        result = ST_OK;
        goto out;
      } else {
        DEBUG("Cisco 01:00:0C:CC:CC:CD SNAP protocol 0x%02X%02X not supported\n",
              frame->data[20], frame->data[21]);
        goto out;
      }
    } else if (frame->data[3] == 0xCC && frame->data[4] == 0xCC && frame->data[5] == 0xCE) {
      /* frame des MAC is 01:00:0C:CC:CC:CE */
      if (frame->data[20] == 0x01 && frame->data[21] == 0x0C) {
        /* frame SNAP proto is 0x010C: VLAN Bridge */
        DEBUG("Cisco VLAN Bridge not supported\n");
        goto out;
      } else {
        DEBUG("Cisco 01:00:0C:CC:CC:CE SNAP protocol 0x%02X%02X not supported\n",
              frame->data[20], frame->data[21]);
        goto out;
      }
    } else if (frame->data[3] == 0xCD && frame->data[4] == 0xCD && frame->data[5] == 0xCD) {
      /* frame des MAC is 01:00:0C:CD:CD:CD */
      if (frame->data[20] == 0x20 && frame->data[21] == 0x0A) {
        /* frame SNAP proto is 0x200A: STP Uplink Fast */
        DEBUG("Cisco STP Uplink Fast not supported\n");
        goto out;
      } else {
        DEBUG("Cisco 01:00:0C:CD:CD:CD SNAP protocol 0x%02X%02X not supported\n",
              frame->data[20], frame->data[21]);
        goto out;
      }
    } else if (frame->data[3] == 0x00 && frame->data[4] == 0x00 && frame->data[5] == 0x00) {
      DEBUG("Cisco Inter Switch Link not supported\n");
      goto out;
    } else {
      DEBUG("Cisco Multicast %02X:%02X:%02X:%02X:%02X:%02X not supported\n",
            frame->data[0], frame->data[1], frame->data[2],
            frame->data[3], frame->data[4], frame->data[5]);
      goto out;
    }
    break;

  case CPU_CODE_IPv4_IGMP_TM:
    type = CN_IPv4_IGMP_PDU;
    put_vif = 1;
    put_vid = 1;
    conform2stp_state = 1;
    break;

  case CPU_CODE_ARP_BC_TM:
    type = CN_ARP_BROADCAST;
    conform2stp_state = 1;
    check_source_mac = 1;
    put_vif = 1;
    put_vid = 1;
    break;

  case CPU_CODE_ARP_REPLY_TO_ME:
    type = CN_ARP_REPLY_TO_ME;
    conform2stp_state = 1;
    check_source_mac = 1;
    put_vif = 1;
    put_vid = 1;
    break;

  case CPU_CODE_IPV6_NEIGHBOR_SOLICITATION_E:
  // case CPU_CODE_USER_DEFINED (10):
    type = CN_NDP_SOLICITATION_IPV6;
    conform2stp_state = 1;
    check_source_mac = 1;
    put_vif = 1;
    put_vid = 1;
    goto out;
    break;
  
  /* FIX THIS */
  /* FIX THIS */
  case CPU_CODE_IPV6_UC_ROUTE_TM_0:
    result = ST_OK;
    #define ICMPV6 0x3a
    #define NEIGHBOR_ADVERTISEMENT 0x88

    DEBUG("icmpv6 4- %02x\n", frame->data[20]);
    DEBUG("NEIGHBOR_ADVERTISEMENT - %02x\n", frame->data[54]);

    // int i = 0;
    // for (i =0; i < 86; i++)
    // {
    //   DEBUG("i = %d, data[] = %d\n", i, frame->data[i]);
    // }

    if (frame->data[20] == ICMPV6 &&
        frame->data[54] == NEIGHBOR_ADVERTISEMENT)
    {
      DEBUG("WORK?\n");
      type = CN_NDP_ADVERTISEMENT_IPV6;
      conform2stp_state = 1;
      check_source_mac = 1;
      put_vif = 1;
      put_vid = 1;

      /* FIX THIS */
      /* FIX THIS */
      /* FIX THIS */

      zmsg_t *msg = make_notify_message (type);
      if (put_vif)
        put_vif_id (msg, vif->id);
      if (put_vid)
        put_vlan_id (msg, vid);
      put_port_id (msg, pid);

      zmsg_addmem (msg, frame->data, frame->len);
      
      zframe_t* tmp_frame = zmsg_first(msg);
      while(tmp_frame)
      {
        hexdump(zframe_data(tmp_frame), zframe_size(tmp_frame));
        tmp_frame = zmsg_next(msg);
      }

      notify_send_arp (&msg);
    }
    goto out;
    break;

  case CPU_CODE_IPV6_UC_ROUTE_TM_1:
 
    result = ST_OK;
    if (! vif_is_forwarding_on_vlan(vif, vid)) {
//DEBUG("REJECTED code: %d, vid: %d frame from vif: %x, pid: %d, dev %d, lport %d, ", frame->code, vid, vif->id, pid, frame->dev, frame->port);
//    if (! vlan_port_is_forwarding_on_vlan(pid, vid))
      goto out;
    }
    route_handle_ipv6_udt (frame->data, frame->len);
    goto out;
    break;

  case CPU_CODE_IP_LL_MC_0_TM:
    type = CN_VRRP;
    conform2stp_state = 1;
    put_vid = 1;
    break;

  case CPU_CODE_USER_DEFINED (0):
    type = CN_LBD_PDU;
    conform2stp_state = 1;
    break;

  case CPU_CODE_USER_DEFINED (1):
    type = CN_DHCP_TRAP;
    check_source_mac = 1;
    conform2stp_state = 1;
    put_vif = 1;
    put_vid = 1;
    break;

  case CPU_CODE_USER_DEFINED (2):
    result = ST_OK;
    if (! vlan_port_is_forwarding_on_vlan(pid, vid))
      goto out;
    control_notify_ip_sg_trap (pid, frame);
    goto out;

  case CPU_CODE_USER_DEFINED (3):
    type = CN_ARP;
    conform2stp_state = 1;
    check_source_mac = 1;
    put_vif = 1;
    put_vid = 1;
    break;

  case CPU_CODE_USER_DEFINED (4):
    type = CN_LLDP_MCAST;
    break;

  case CPU_CODE_USER_DEFINED (9):
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

  case CPU_CODE_USER_DEFINED (6):
    result = ST_OK;
    DEBUG("Packet on Port #%d trapped!\r\n", pid);
    goto out;

  case CPU_CODE_IPv4_UC_ROUTE_TM_1:
    result = ST_OK;
    if (! is_vif_forwarding_on_vlan) {
      DEBUG("REJECTED vid: %d frame from vif: %x, pid: %d, dev %d, lport %d, \n",
          vid, vifid, pid, frame->dev, frame->port);
      goto out;
    }
    route_handle_udt (frame->data, frame->len);
    goto out;

  case CPU_CODE_USER_DEFINED (7):
    stack_handle_mail (stack_pri_port->id, frame->data, frame->len);
    result = ST_OK;
    goto out;
  case CPU_CODE_USER_DEFINED (8):
    stack_handle_mail (stack_sec_port->id, frame->data, frame->len);
    result = ST_OK;
    goto out;
  case CPU_CODE_MAIL:
    stack_handle_mail (pid, frame->data, frame->len);
    result = ST_OK;
    goto out;

  case CPU_CODE_EGRESS_SAMPLED:
    type = CN_SAMPLED;
    direction = EGRESS;
    len = frame->len;
    put_direction = 1;
    put_len = 1;
    break;
  case CPU_CODE_INGRESS_SAMPLED:
    type = CN_SAMPLED;
    direction = INGRESS;
    len = frame->len;
    put_direction = 1;
    put_len = 1;
    break;

  default:
    DEBUG ("spec frame code %02X not supported\n", frame->code);
    goto out;
  }

  if (conform2stp_state)
    if (! is_vif_forwarding_on_vlan) {
      DEBUG("REJECTED vid: %d frame from vif: %x, pid: %d, dev %d, lport %d, \n",
          vid, vifid, pid, frame->dev, frame->port);
      result = ST_OK;
      goto out;
    }

  if (check_source_mac) {
    if (frame->len >= 12) {
      char *frame_source_mac = ((char*)frame->data) + 6;
      if (!memcmp(frame_source_mac, master_mac, 6)) {
        DEBUG("DROP frame: code: %d, vid: %d, vif: %x: source mac == master mac",
              frame->code, vid, vifid);
        result = ST_OK;
        goto out;
      }
    }
  }

  zmsg_t *msg = make_notify_message (type);

  struct pkt_info info = {
    .tagged = frame->tagged,
    .pcp = frame->tagged ? frame->up : 7,
    .cfi = frame->tagged ? frame->cfi : 0,
    .pid = pid,
    .vid = put_vid ? vid : 0,
    .vif = put_vif ? vif->id : 0
  };

  switch (type) {
    case CN_SAMPLED:
      if (put_vif)
        put_vif_id (msg, vif->id);
      if (put_vid)
        put_vlan_id (msg, vid);
      if (put_direction)
        zmsg_addmem (msg, &direction, sizeof direction);
      if (put_len)
        zmsg_addmem (msg, &len, sizeof len);
      put_port_id (msg, pid);
      break;
    default:
      put_pkt_info (msg, &info, type);
      zmsg_addmem (msg, frame->data, frame->len);
  }

  switch (type) {
    case CN_ARP_BROADCAST:
    case CN_ARP_REPLY_TO_ME:
    case CN_ARP:
    case CN_NDP_SOLICITATION_IPV6:
    case CN_NDP_ADVERTISEMENT_IPV6:
      notify_send_arp (&msg);
      break;
    case CN_DHCP_TRAP:
      notify_send_dhcp (&msg);
      break;
    case CN_SAMPLED:
      notify_send_sflow(&msg);
      break;
    default:
      notify_send (&msg);
      break;
  }

  result = ST_OK;

 out:
  return result;
}

/*
   Packet Control iface command handlers
*/

DEFINE_HANDLER (PC_PORT_SEND_FRAME)
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

DEFINE_HANDLER (PC_MAIL_TO_NEIGHBOR)
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

DEFINE_HANDLER (PC_GIF_TX)
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

DEFINE_HANDLER (PC_VIF_TX)
{
  zframe_t *frame;
  struct vif_id *id;
  struct vif_tx_opts *opts;
  enum status result = ST_BAD_FORMAT;

  frame = FIRST_ARG;
  if (zframe_size (frame) != sizeof (*id))
    goto out;
  id = (struct vif_id *) zframe_data (frame);

  frame = NEXT_ARG;
  if (zframe_size (frame) != sizeof (*opts))
    goto out;
  opts = (struct vif_tx_opts *) zframe_data (frame);

  frame = NEXT_ARG;
  result = vif_tx (id, opts, zframe_size (frame), zframe_data (frame));

 out:
  report_status (result);
}

DEFINE_HANDLER (PC_SEND_FRAME)
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

DEFINE_HANDLER (PC_INJECT_FRAME)
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


/*
 * Stack Async iface command handlers.
 */

DEFINE_HANDLER (SC_UPDATE_STACK_CONF) {

DEBUG("===SC_UPDATE_STACK_CONF\n");
  zframe_t *frame = FIRST_ARG;
  if (!frame)
    return;

  stack_update_conf(zframe_data(frame), zframe_size(frame));
}

DEFINE_HANDLER (SC_INT_RTBD_CMD) {

DEBUG("===SC_INT_RTBD_CMD\n");
  zframe_t *frame = FIRST_ARG;
  if (!frame)
    return;

  struct rtbd_ip_addr_msg *am;
  struct rtbd_route_msg *rm;
  rtbd_notif_t notif = *((rtbd_notif_t *) zframe_data (frame));
  switch (notif) {
  case RCN_IP_ADDR:
    am  = (struct rtbd_ip_addr_msg *) ((rtbd_notif_t *) zframe_data (frame) + 1);
    ip_addr_t addr; 
    ip_addr_v6_t addr_v6; 
    // mac_op_rt(notif, am, sizeof(*am));
    switch (am->type){
      case AF_INET:             
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
        
      case AF_INET6:   
        memcpy (&addr_v6, &am->addr_v6, 16);
        switch (am->op) {
          case RIAO_ADD:
            DEBUG ("route_add_mgmt_ipv6\n");
            route_add_mgmt_ipv6 (addr_v6);
            break;
          case RIAO_DEL:
            DEBUG ("route_del_mgmt_ipv6\n");
            route_del_mgmt_ipv6 (addr_v6);
            break;
          default:
            break;
        }
        return;
        break;

      default:
        break;
    }
    break;

  case RCN_ROUTE:
    rm = (struct rtbd_route_msg *) ((rtbd_notif_t *) zframe_data (frame) + 1);
    struct route rt;
    rt.pfx.addr.u32Ip = rm->dst;
    memcpy (&rt.pfx.addrv6.arIP, &rm->dst_v6, 16);
    rt.pfx.alen = rm->dst_len;
    rt.gw.u32Ip = rm->gw;
    memcpy (&rt.gw_v6.arIP, &rm->gw_v6, 16);
    rt.vid = rm->vid;
    switch (rm->type)
    {
      case AF_INET:
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

      case AF_INET6:
        switch (rm->op) {
        case RRTO_ADD:
          DEBUG ("route_add_v6\n");
          route_add_v6 (&rt);
          break;
        case RRTO_DEL:
          DEBUG ("route_del_v6\n");
          route_del_v6 (&rt);
          break;
        default:
          break;
        }
        break;  
      
      default:
        break;
    }
    break;

  default:
    break;
  }
}

DEFINE_HANDLER (SC_INT_NA_CMD) {
DEBUG("===SC_INT_NA_CMD\n");
  zframe_t *frame = FIRST_ARG;
  if (!frame)
    return;

  struct arpd_ip_addr_msg *iam =
    (struct arpd_ip_addr_msg *) zframe_data (frame);
  arpc_set_mac_addr
    (iam->ip_addr, iam->vid, &iam->mac_addr[0], iam->vif_id);
}

DEFINE_HANDLER (SC_INT_NA_IPV6_CMD) {
DEBUG("===SC_INT_NA_IPV6_CMD\n");
  zframe_t *frame = FIRST_ARG;
  if (!frame)
    return;

  struct ndp_ip_addr_msg *iam =
    (struct ndp_ip_addr_msg *) zframe_data (frame);
  ndpc_set_mac_addr
    (iam->ip_addr, iam->vid, &iam->mac_addr[0], iam->vif_id);
}

DEFINE_HANDLER (SC_INT_OPNA_CMD) {
DEBUG("===SC_INT_OPNA_CMD\n");
  zframe_t *frame = FIRST_ARG;
  if (!frame)
    return;

  arpd_command_t cmd = *((arpd_command_t *) zframe_data (frame));
  struct gw *gw  = (struct gw *) ((arpd_command_t *) zframe_data (frame) + 1);
  arpc_ip_addr_op (gw, cmd);
}

DEFINE_HANDLER (SC_INT_OPNA_IPV6_CMD) {
DEBUG("===SC_INT_OPNA_IPV6_CMD\n");
  zframe_t *frame = FIRST_ARG;
  if (!frame)
    return;

  arpd_command_t cmd = *((arpd_command_t *) zframe_data (frame));
  struct gw_v6 *gw  = (struct gw_v6 *) ((arpd_command_t *) zframe_data (frame) + 1);
  ndpc_ip_addr_op (gw, cmd);
}

DEFINE_HANDLER (SC_INT_UDT_CMD) {
DEBUG("===SC_INT_UDT_CMD\n");
  zframe_t *frame = FIRST_ARG;
  if (!frame)
    return;

  route_handle_udaddr (*(uint32_t*)zframe_data(frame));
}

DEFINE_HANDLER (SC_INT_UDT_IPV6_CMD) {
// DEBUG("===SC_INT_UDT_IPV6_CMD\n");
  zframe_t *frame = FIRST_ARG;
  if (!frame)
    return;

  route_handle_ipv6_udaddr (*(GT_IPV6ADDR*)zframe_data(frame));
}

DEFINE_HANDLER (SC_INT_CLEAR_RT_CMD) {
DEBUG("===SC_INT_CLEAR_RT_CMD\n");
  zframe_t *frame = FIRST_ARG;
  if (!frame)
    return;

  fib_clear_routing();
}

DEFINE_HANDLER (SC_INT_CLEAR_RE_CMD) {
DEBUG(">>>>DEFINE_HANDLER (SC_INT_CLEAR_RE_CMD)\n");
  devsbmp_t dbmp;
  enum status result;

  result = POP_ARG (&dbmp);
  if (result != ST_OK)
    return;

  ret_clear_devs_res(dbmp);
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

DEFINE_HANDLER (CC_PORT_GET_STP_STATE)
{
  port_id_t pid;
  stp_id_t stp_id;
  enum port_stp_state port_state;
  stp_state_t state;
  enum status result;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_OPT_ARG (&stp_id);
  if (result != ST_OK)
    goto out;

  result = port_get_stp_state (pid, stp_id, &port_state);
  if (result != ST_OK)
    goto out;

  state = (stp_state_t) port_state;

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &state, sizeof (state));
  send_reply (reply);
  return;

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VIF_SET_STP_STATE)
{
  vif_id_t vif;
  stp_id_t stp_id;
  stp_state_t state;
  enum status result;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&state);
  if (result != ST_OK)
    goto out;

  result = POP_OPT_ARG (&stp_id);
  switch (result) {
  case ST_OK:
    result = vif_set_stp_state (vif, stp_id, 0, state);
    break;
  case ST_DOES_NOT_EXIST:
    stp_id = ALL_STP_IDS;
    result = vif_set_stp_state (vif, 0, 1, state);
    break;
  default:
    break;
  }

 out:
  report_status (result);
}

DEFINE_HANDLER (SC_INT_VIF_SET_STP_STATE)
{
  zframe_t *frame = FIRST_ARG;

  if (!frame)
    return;

  struct mac_vif_set_stp_state_args *arg =
    (struct mac_vif_set_stp_state_args *) zframe_data (frame);

  vif_id_t vif      = arg->vifid;
  stp_id_t stp_id   = arg->stp_id;
  int all           = arg->all;
  stp_state_t state = arg->state;

  vif_set_stp_state (vif, stp_id, all, state);
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

DEFINE_HANDLER (CC_VIF_SHUTDOWN)
{
  enum status result;
  vif_id_t vif;
  bool_t shutdown;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&shutdown);
  if (result != ST_OK)
    goto out;

  result = vif_shutdown (vif, shutdown);

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

DEFINE_HANDLER (CC_VIF_BLOCK)
{
  enum status result;
  vif_id_t vif;
  struct port_block what;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&what);
  if (result != ST_OK)
    goto out;

  result = vif_block (vif, &what);

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
  arg.bmp_devs = LOCAL_DEV;
  result = mac_flush (&arg, GT_FALSE);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VIF_FDB_FLUSH)
{
  struct mac_age_arg_vif arg;
  vif_id_t vif;
  // stp_id_t stp_id;

  enum status result;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  arg.vifid = vif;
  arg.vid = ALL_VLANS;
  result = mac_flush_vif (&arg, GT_FALSE);

  // @todo:
  // if (POP_ARG(&stp_id) == ST_OK) {
  //   vid_t vid;
  //   for (vid = 1; vid <= NVLANS; vid++) {
  //     stp_id_t vlan_stp_id;
  //     if ((vlan_get_stp_id(vid, &vlan_stp_id) == ST_OK) &&
  //         (stp_id == vlan_stp_id)) {
  //       arg.vid = vid;
  //       result = mac_flush_vif (&arg, GT_FALSE);
  //     }
  //     if (result != ST_OK)
  //       goto out;
  //   }
  // } else {
  //   arg.vid = ALL_VLANS;
  //   result = mac_flush_vif (&arg, GT_FALSE);
  // }

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

DEFINE_HANDLER (CC_VLAN_SET_STP_ID)
{
  enum status result;
  vid_t    vid;
  stp_id_t stp_id;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&stp_id);
  if (result != ST_OK)
    goto out;

  result = vlan_set_stp_id (vid, stp_id);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VLAN_GET_STP_ID)
{
  enum status result;
  vid_t    vid;
  stp_id_t stp_id;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = vlan_get_stp_id (vid, &stp_id);
  if (result != ST_OK)
    goto out;

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &stp_id, sizeof (stp_id));
  send_reply (reply);
  return;

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

DEFINE_HANDLER (CC_MAC_OP_VIF)
{
  enum status result;
  struct mac_op_arg_vif op_arg;

  result = POP_ARG (&op_arg);
  if (result != ST_OK)
    goto out;

  result = mac_op_vif (&op_arg);

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

DEFINE_HANDLER (CC_VIF_SET_MODE)
{
  enum status result;
  vif_id_t vif;
  port_mode_t mode;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = vif_set_mode (vif, mode);

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

DEFINE_HANDLER (CC_VIF_SET_ACCESS_VLAN)
{
  enum status result;
  vif_id_t vif;
  vid_t vid;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = vif_set_access_vid (vif, vid);

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

DEFINE_HANDLER (CC_VIF_SET_NATIVE_VLAN)
{
  enum status result;
  vif_id_t vif;
  vid_t vid;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = vif_set_native_vid (vif, vid);

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

DEFINE_HANDLER (CC_VIF_SET_SPEED)
{
  enum status result;
  vif_id_t vif;
  struct port_speed_arg psa;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&psa);
  if (result != ST_OK)
    goto out;

  result = vif_set_speed (vif, &psa);

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

DEFINE_HANDLER (CC_VIF_SET_DUPLEX)
{
  enum status result;
  vif_id_t vif;
  port_duplex_t duplex;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&duplex);
  if (result != ST_OK)
    goto out;

  result = vif_set_duplex (vif, duplex);

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

DEFINE_HANDLER (CC_PORT_SET_SOLICITED_CMD)
{
  enum status result;
  solicited_cmd_t cmd;

  result = POP_ARG_SZ (&cmd, sizeof (cmd));
  if (result != ST_OK)
    goto err;

  result = route_set_solicited_cmd (cmd);
  if (result != ST_OK)
    goto err;

  zmsg_t *reply = make_reply (ST_OK);
  send_reply (reply);

  return;

 err:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_ENABLE_SOLICITED)
{
  enum status result;
  port_id_t pid;
  bool_t en;
  solicited_cmd_t cmd;

  result = POP_ARG_SZ (&pid, sizeof (pid));
  if (result != ST_OK)
    goto err;

  result = POP_ARG_SZ (&en, sizeof (en));
  if (result != ST_OK)
    goto err;

  result = POP_ARG_SZ (&cmd, sizeof (cmd));
  if (result != ST_OK)
    goto err;

  result = pcl_enable_solicited (pid, en, cmd);
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
  int i;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto err;

  result = POP_ARG (&page);
  if (result != ST_OK)
    goto err;

  result = POP_ARG (&reg);
  if (result != ST_OK)
    goto err;

if (page >= 3000) {   //TODO remove BEGIN
//  DEBUG("going mac_count(%hu)\n", reg);
//  mac_count(pid, page, reg);
  switch (page){
    case 4000:
      nht_dump();
      fib_dump();
      fib_ipv6_dump();
      ret_dump();
      route_dump();
      break;
    case 4001:
      for (i = 1; i < NPORTS; i++) {
        struct vif *vif = vif_get_by_pid(reg, i);
        if (!vif)
          continue;
        DEBUG("VIF: %x, stg:\n", vif->id);
        PRINTHexDump(vif->stg_state, 16);
      }
      break;
  }
  val = 0;
}   else { //  TODO remove END
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

DEFINE_HANDLER (CC_MAC_LIST_VIF)
{
  enum status result;
  vid_t vid;
  vif_id_t vifid;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto err;

  if (!(vid == ALL_VLANS || vlan_valid (vid))) {
    result = ST_BAD_VALUE;
    goto err;
  }

  result = POP_ARG (&vifid);
  if (result != ST_OK)
    goto err;

  if (!(vifid == ALL_VIFS || vif_getn(vifid))) {
    result = ST_BAD_VALUE;
    goto err;
  }

  zmsg_t *reply = make_reply (ST_OK);
  data_encode_fdb_addrs_vif (reply, vid, vifid);
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

DEFINE_HANDLER (CC_MAC_FLUSH_DYNAMIC_VIF)
{
  enum status result;
  struct mac_age_arg_vif aa;

  result = POP_ARG (&aa);
  if (result != ST_OK)
    goto out;

  result = mac_flush_vif (&aa, GT_FALSE);

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

DEFINE_HANDLER (CC_VIF_SET_FLOW_CONTROL)
{
  enum status result;
  vif_id_t vif;
  flow_control_t fc;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&fc);
  if (result != ST_OK)
    goto out;

  result = vif_set_flow_control (vif, fc);

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

DEFINE_HANDLER (CC_PORT_RATE_LIMIT_DROP_ENABLE)
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

  result = port_rate_limit_drop_enable (pid, enable);
  if (result != ST_OK)
    goto out;

 out:
  report_status (result);
}


DEFINE_HANDLER (CC_PORT_SET_TRAFFIC_SHAPE)
{
  enum status result;
  port_id_t pid;
  bool_t enable;
  bps_t rate;
  burst_t burst;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
    if (result != ST_OK)
      goto out;

  result = POP_ARG (&rate);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&burst);
  if (result != ST_OK)
    goto out;

  result = port_set_traffic_shape (pid, enable, rate, burst);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_SET_TRAFFIC_SHAPE_QUEUE)
{
  enum status result;
  port_id_t pid;
  bool_t enable;
  queueid_t qid;
  bps_t rate;
  burst_t burst;

  result = POP_ARG (&pid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
    if (result != ST_OK)
      goto out;

  result = POP_ARG (&qid);
    if (result != ST_OK)
      goto out;

  result = POP_ARG (&rate);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&burst);
  if (result != ST_OK)
    goto out;

  result = port_set_traffic_shape_queue (pid, enable, qid, rate, burst);

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

DEFINE_HANDLER (CC_VIF_SET_PROTECTED)
{
  enum status result;
  vif_id_t vif;
  bool_t protected;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&protected);
  if (result != ST_OK)
    goto out;

  result = vif_set_protected (vif, protected);

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

DEFINE_HANDLER (CC_MCG_ADD_VIF)
{
  enum status result;
  mcg_t mcg;
  vif_id_t vifid;

  result = POP_ARG (&mcg);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vifid);
  if (result != ST_OK)
    goto out;

  result = vif_mcg_add_vif (vifid, mcg);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_MCG_DEL_VIF)
{
  enum status result;
  mcg_t mcg;
  vif_id_t vifid;

  result = POP_ARG (&mcg);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vifid);
  if (result != ST_OK)
    goto out;

  result = vif_mcg_del_vif (vifid, mcg);

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

DEFINE_HANDLER (CC_INT_SPEC_FRAME_FORWARD)
{
  enum status result;
  struct pdsa_spec_frame *frame;
  notification_t type = 0;
  port_id_t pid;
  int put_vid = 0, put_vif = 0;
  uint16_t *etype;
  register int conform2stp_state = 0;
  int check_source_mac = 0;
  struct vif *vif;
  vid_t vid;
  int put_direction = 0;
  sflow_type_t direction;
  int put_len = 0;
  uint16_t len;

  if (ARGS_SIZE != 1) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  frame = (struct pdsa_spec_frame *) zframe_data (FIRST_ARG);
  vid = frame->vid;

  vif = vif_by_hw(frame->dev, frame->port);
  if (!vif && frame->port != CPSS_CPU_PORT_NUM_CNS) {  /* TODO CPU port case */
DEBUG("!vif %d:%d\n", frame->dev, frame->port);
    result = ST_OK;
    goto out;
  }
  if (!vif->valid) {
    result = ST_OK;
    goto out;
  }

  pid = port_id (frame->dev, frame->port);
  if (!pid && frame->port != CPSS_CPU_PORT_NUM_CNS) {
//DEBUG("!!!!!!pid: %d:%d\n", frame->dev, frame->port);
//DEBUG(MAC_FMT " <- " MAC_FMT "\n", MAC_ARG(((char*)frame->data)), MAC_ARG(((char*)frame->data + 6)));
    if (frame->dev != stack_id && frame->dev != stack_id + NEXTDEV_INC) {
      pid = port_id((frame->dev < 16)? stack_id : stack_id + NEXTDEV_INC, frame->port);
//DEBUG("!!!pid = %d\n ", pid);
    }
    if (!pid) {
      result = ST_OK;
//DEBUG("!!!pid outing\n ");
      goto out;
    }
  }

  if (vif && vif->trunk)
    vif = vif->trunk;

  if (vid == 0) /* priority tagged */
    vid = vif_vid (vif);

  result = ST_BAD_VALUE;

  switch (frame->code) {
  case CPU_CODE_IEEE_RES_MC_0_TM:
    switch (frame->data[5]) {
    case WNCT_STP:
      etype = (uint16_t *) &frame->data[12];
      switch (ntohs (*etype)) {
      case 0x88CC:
        DEBUG("LLDP shouldnt go there!!!!\n");
        result = ST_OK;
        goto out;
      default:
        tipc_notify_bpdu (vif->id, pid, vid, frame->tagged, frame->len, frame->data);
        result = ST_OK;
        goto out;
      }
      break;
    case WNCT_802_3_SP:
      DEBUG("Slow Protocols shouldnt go there!!!!\n");
      result = ST_OK;
      goto out;

    case WNCT_802_3_SP_OAM:
      etype = (uint16_t *) &frame->data[12];
      switch (ntohs (*etype)) {
      case 0x888E:
        type = CN_EAPOL;
        conform2stp_state = 1;
        break;
      case 0x88CC:
        DEBUG("LLDP shouldnt go there!!!!\n");
        result = ST_OK;
        goto out;
      default:
        DEBUG ("Nearest Bridge ethertype %04X not supported\n", ntohs(*etype));
        goto out;
      }
      break;

    case WNCT_LLDP:
      DEBUG("LLDP shouldnt go there!!!!\n");
      result = ST_OK;
      goto out;
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

  case CPU_CODE_CISCO_MC_TM:
    /* frame des MAC is 01:00:0C:xx:xx:xx */
    if (frame->data[3] == 0xCC && frame->data[4] == 0xCC && frame->data[5] == 0xCC) {
      /* frame des MAC is 01:00:0C:CC:CC:CC */
      if (frame->data[20] == 0x01 && frame->data[21] == 0x04) {
        /* frame SNAP proto is 0x0104: Port Aggregation Protocol */
        DEBUG("Cisco Port Aggregation Protocol not supported\n");
        goto out;
      } else if (frame->data[20] == 0x01 && frame->data[21] == 0x11) {
        /* frame SNAP proto is 0x0111: Unidirectional Link Detection */
        type = CN_UDLD;
        put_vif = 1;
        conform2stp_state = 0;
        break;
      } else if (frame->data[20] == 0x20 && frame->data[21] == 0x00) {
        /* frame SNAP proto is 0x2000: Cisco Discovery Protocol */
        DEBUG("Cisco Discovery Protocol not supported\n");
        goto out;
      } else if (frame->data[20] == 0x20 && frame->data[21] == 0x04) {
        /* frame SNAP proto is 0x2004: Dynamic Trunking */
        DEBUG("Cisco Dynamic Trunking not supported\n");
        goto out;
      } else if (frame->data[20] == 0x20 && frame->data[21] == 0x03) {
        /* frame SNAP proto is 0x2003: VLAN Trunking */
        DEBUG("Cisco VLAN Trunking not supported\n");
        goto out;
      } else {
        DEBUG("Cisco 01:00:0C:CC:CC:CC SNAP protocol 0x%02X%02X not supported\n",
              frame->data[20], frame->data[21]);
        goto out;
      }
    } else if (frame->data[3] == 0xCC && frame->data[4] == 0xCC && frame->data[5] == 0xCD) {
      /* frame des MAC is 01:00:0C:CC:CC:CD */
      if (frame->data[20] == 0x01 && frame->data[21] == 0x0B) {
        /* frame SNAP proto is 0x010B: Spanning Tree PVSTP+ */
        tipc_notify_bpdu (vif->id, pid, vid, frame->tagged, frame->len, frame->data);
        result = ST_OK;
        goto out;
      } else {
        DEBUG("Cisco 01:00:0C:CC:CC:CD SNAP protocol 0x%02X%02X not supported\n",
              frame->data[20], frame->data[21]);
        goto out;
      }
    } else if (frame->data[3] == 0xCC && frame->data[4] == 0xCC && frame->data[5] == 0xCE) {
      /* frame des MAC is 01:00:0C:CC:CC:CE */
      if (frame->data[20] == 0x01 && frame->data[21] == 0x0C) {
        /* frame SNAP proto is 0x010C: VLAN Bridge */
        DEBUG("Cisco VLAN Bridge not supported\n");
        goto out;
      } else {
        DEBUG("Cisco 01:00:0C:CC:CC:CE SNAP protocol 0x%02X%02X not supported\n",
              frame->data[20], frame->data[21]);
        goto out;
      }
    } else if (frame->data[3] == 0xCD && frame->data[4] == 0xCD && frame->data[5] == 0xCD) {
      /* frame des MAC is 01:00:0C:CD:CD:CD */
      if (frame->data[20] == 0x20 && frame->data[21] == 0x0A) {
        /* frame SNAP proto is 0x200A: STP Uplink Fast */
        DEBUG("Cisco STP Uplink Fast not supported\n");
        goto out;
      } else {
        DEBUG("Cisco 01:00:0C:CD:CD:CD SNAP protocol 0x%02X%02X not supported\n",
              frame->data[20], frame->data[21]);
        goto out;
      }
    } else if (frame->data[3] == 0x00 && frame->data[4] == 0x00 && frame->data[5] == 0x00) {
      DEBUG("Cisco Inter Switch Link not supported\n");
      goto out;
    } else {
      DEBUG("Cisco Multicast %02X:%02X:%02X:%02X:%02X:%02X not supported\n",
            frame->data[0], frame->data[1], frame->data[2],
            frame->data[3], frame->data[4], frame->data[5]);
      goto out;
    }
    break;

  case CPU_CODE_IPv4_IGMP_TM:
    type = CN_IPv4_IGMP_PDU;
    put_vif = 1;
    put_vid = 1;
    conform2stp_state = 1;
    break;

  case CPU_CODE_ARP_BC_TM:
    type = CN_ARP_BROADCAST;
    conform2stp_state = 1;
    check_source_mac = 1;
    put_vif = 1;
    put_vid = 1;
    break;

  case CPU_CODE_ARP_REPLY_TO_ME:
    type = CN_ARP_REPLY_TO_ME;
    conform2stp_state = 1;
    check_source_mac = 1;
    put_vif = 1;
    put_vid = 1;
    break;

  case CPU_CODE_IPV6_NEIGHBOR_SOLICITATION_E:
    type = CN_NDP_SOLICITATION_IPV6;
    conform2stp_state = 1;
    check_source_mac = 1;
    put_vif = 1;
    put_vid = 1;
    goto out;
    break;
  
  /* FIX THIS */
  /* FIX THIS */
  case CPU_CODE_IPV6_UC_ROUTE_TM_0:
    result = ST_OK;
    #define ICMPV6 0x3a
    #define NEIGHBOR_ADVERTISEMENT 0x88


    if (frame->data[20] == ICMPV6 &&
        frame->data[54] == NEIGHBOR_ADVERTISEMENT)
    {
      // DEBUG("WORK?\n");
      type = CN_NDP_ADVERTISEMENT_IPV6;
      conform2stp_state = 1;
      check_source_mac = 1;
      put_vif = 1;
      put_vid = 1;

      /* FIX THIS */
      /* FIX THIS */
      /* FIX THIS */
/*
      zmsg_t *msg = make_notify_message (type);
      if (put_vif)
        put_vif_id (msg, vif->id);
      if (put_vid)
        put_vlan_id (msg, vid);
      put_port_id (msg, pid);

      zmsg_addmem (msg, frame->data, frame->len);
      
      zframe_t* tmp_frame = zmsg_first(msg);
      while(tmp_frame)
      {
        tmp_frame = zmsg_next(msg);
      }

      notify_send_arp (&msg); */
    }
//    goto out;
    break;
  case CPU_CODE_IPV6_UC_ROUTE_TM_1:

 
    result = ST_OK;
    if (! vif_is_forwarding_on_vlan(vif, vid)) {
//DEBUG("REJECTED code: %d, vid: %d frame from vif: %x, pid: %d, dev %d, lport %d, ", frame->code, vid, vif->id, pid, frame->dev, frame->port);
//    if (! vlan_port_is_forwarding_on_vlan(pid, vid))
      goto out;
    }
    route_handle_ipv6_udt (frame->data, frame->len);
    goto out;
    break;

  case CPU_CODE_IP_LL_MC_0_TM:
    type = CN_VRRP;
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
    check_source_mac = 1;
    put_vif = 1;
    put_vid = 1;
    break;

  case CPU_CODE_USER_DEFINED (2):
    result = ST_OK;
    if (! vlan_port_is_forwarding_on_vlan(pid, vid))
      goto out;
    control_notify_ip_sg_trap (pid, frame);
    goto out;

  case CPU_CODE_USER_DEFINED (3):
    type = CN_ARP;
    conform2stp_state = 1;
    check_source_mac = 1;
    put_vif = 1;
    put_vid = 1;
    break;

  case CPU_CODE_USER_DEFINED (4):
    type = CN_LLDP_MCAST;
    break;

  case CPU_CODE_USER_DEFINED (9):
//    DEBUG("Got SP via PCL pid: %d vid: %d!\n", pid, vid);
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

  case CPU_CODE_USER_DEFINED (6):
    result = ST_OK;
//    DEBUG("Packet on Port #%d trapped!\r\n", pid);
    goto out;

  case CPU_CODE_IPv4_UC_ROUTE_TM_1:
//    DEBUG("sbelo CPU_CODE_IPv4_UC_ROUTE_TM_1 %d\n", vid);
//    DEBUG("sbelo CPU_CODE_IPv4_UC_ROUTE_TM_1 %d\n", vif->valid);
    result = ST_OK;
    if (! vif_is_forwarding_on_vlan(vif, vid)) {
//DEBUG("REJECTED code: %d, vid: %d frame from vif: %x, pid: %d, dev %d, lport %d, ", frame->code, vid, vif->id, pid, frame->dev, frame->port);
//    if (! vlan_port_is_forwarding_on_vlan(pid, vid))
      goto out;
    }
    route_handle_udt (frame->data, frame->len);
    goto out;

  case CPU_CODE_USER_DEFINED (7):
    stack_handle_mail (stack_pri_port->id, frame->data, frame->len);
    result = ST_OK;
    goto out;
  case CPU_CODE_USER_DEFINED (8):
    stack_handle_mail (stack_sec_port->id, frame->data, frame->len);
    result = ST_OK;
    goto out;
  case CPU_CODE_MAIL:
    stack_handle_mail (pid, frame->data, frame->len);
    result = ST_OK;
    goto out;

  case CPU_CODE_EGRESS_SAMPLED:
    type = CN_SAMPLED;
    direction = EGRESS;
    len = frame->len;
    put_direction = 1;
    put_len = 1;
    break;
  case CPU_CODE_INGRESS_SAMPLED:
    type = CN_SAMPLED;
    direction = INGRESS;
    len = frame->len;
    put_direction = 1;
    put_len = 1;
    break;

  default:
    DEBUG ("spec frame code %02X not supported\n", frame->code);
    goto out;
  }

  if (conform2stp_state)
    if (! vif_is_forwarding_on_vlan(vif, vid)) {
//DEBUG("REJECTED code: %d, vid: %d frame from vif: %x, pid: %d, dev %d, lport %d, ", frame->code, vid, vif->id, pid, frame->dev, frame->port);
      result = ST_OK;
      goto out;
    }

  if (check_source_mac) {
    if (frame->len >= 12) {
      char *frame_source_mac = ((char*)frame->data) + 6;
      if (!memcmp(frame_source_mac, master_mac, 6)) {
        DEBUG("DROP frame: code: %d, vid: %d, vif: %x: source mac == master mac",
              frame->code, vid, vif->id);
        result = ST_OK;
        goto out;
      }
    }
  }

  zmsg_t *msg = make_notify_message (type);

  struct pkt_info info = {
    .tagged = frame->tagged,
    .pcp = frame->tagged ? frame->up : 7,
    .cfi = frame->tagged ? frame->cfi : 0,
    .pid = pid,
    .vid = put_vid ? vid : 0,
    .vif = put_vif ? vif->id : 0
  };

  switch (type) {
    case CN_SAMPLED:
      if (put_vif)
        put_vif_id (msg, vif->id);
      if (put_vid)
        put_vlan_id (msg, vid);
      if (put_direction)
        zmsg_addmem (msg, &direction, sizeof direction);
      if (put_len)
        zmsg_addmem (msg, &len, sizeof len);
      put_port_id (msg, pid);
      break;
    default:
      put_pkt_info (msg, &info, type);
      zmsg_addmem (msg, frame->data, frame->len);
  }

  switch (type) {
    case CN_ARP_BROADCAST:
    case CN_ARP_REPLY_TO_ME:
    case CN_ARP:
    case CN_NDP_SOLICITATION_IPV6:
    case CN_NDP_ADVERTISEMENT_IPV6:
//      DEBUG("SBELO SEND type=%d", type);
      notify_send_arp (&msg);
      break;
    case CN_DHCP_TRAP:
      notify_send_dhcp (&msg);
      break;
    case CN_SAMPLED:
      notify_send_sflow(&msg);
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

  result = pop_size (addr, __args, 6, 0);
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

DEFINE_HANDLER (CC_VIF_SET_PVE_DST)
{
  enum status result;
  vif_id_t vif;
  vif_id_t dst;
  bool_t enable;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&dst);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;
  result = vif_set_pve_dst (vif, dst, enable);

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

DEFINE_HANDLER (CC_QOS_SET_WRTD)
{
  enum status result;
  bool_t enable;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = qos_set_wrtd (enable);

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

DEFINE_HANDLER (CC_VIF_SET_COMM)
{
  enum status result;
  vif_id_t vif;
  port_comm_t comm;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&comm);
  if (result != ST_OK)
    goto out;

  result = vif_set_comm (vif, comm);

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

DEFINE_HANDLER (CC_VIF_SET_CUSTOMER_VLAN)
{
  enum status result;
  vif_id_t vif;
  vid_t vid;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = vif_set_customer_vid (vif, vid);

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
  zframe_t *frame;
  vid_t vid;

  result = POP_ARG (&num);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;
  frame = FIRST_ARG;
  if (!frame || (zframe_size (frame) % sizeof (struct mon_if))) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  result = mon_session_set_dst (num, (struct mon_if*) zframe_data(frame), vid);

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

DEFINE_HANDLER (CC_DGASP_VIF_OP)
{
  enum status result;
  vif_id_t vif;
  bool_t add;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&add);
  if (result != ST_OK)
    goto out;

  result = vif_dgasp_op (vif, add);

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

DEFINE_HANDLER (CC_VIF_VLAN_TRANSLATE)
{
  enum status result;
  vif_id_t vif;
  vid_t from, to;
  bool_t enable;

  result = POP_ARG (&vif);
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

  result = vif_vlan_translate (vif, from, to, enable);

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

DEFINE_HANDLER (CC_VIF_SET_TRUNK_VLANS)
{
  enum status result;
  vif_id_t vif;
  zframe_t *frame;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  frame = zmsg_pop (__args);
  if (!frame || zframe_size (frame) != VLAN_BITMAP_SIZE) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  result = vif_set_trunk_vlans (vif, zframe_data (frame));

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

DEFINE_HANDLER (CC_DIAG_IPDC_SET_MODE)
{
  uint8_t mode;
  enum status result;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = diag_ipdc_set_mode (mode);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_DIAG_IPDC_READ)
{
  uint32_t val;
  enum status result;

  result = diag_ipdc_read (&val);
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

DEFINE_HANDLER (CC_DIAG_READ_RET_CNT)
{
  uint8_t n, i;
  uint32_t data[10];
  enum status result;

  result = POP_ARG (&n);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  result = diag_read_ret_cnt (n, data);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  zmsg_t *reply = make_reply (ST_OK);
  for (i = 0; i < 10; i++)
    zmsg_addmem (reply, &data[i], sizeof (data[i]));
  send_reply (reply);
}

DEFINE_HANDLER (CC_BC_LINK_STATE)
{
  zmsg_t *msg = zmsg_new ();
  assert (msg);
  enum event_notification en = EN_BC_LS;
  zmsg_addmem (msg, &en, sizeof (en));
  zmsg_send (&msg, evtntf_sock);

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

DEFINE_HANDLER (CC_VIF_SET_VOICE_VLAN)
{
  enum status result;
  vif_id_t vif;
  vid_t vid;

  result = POP_ARG (&vif);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = vif_set_voice_vid (vif, vid);

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

DEFINE_HANDLER (CC_GET_VIF_PORTS)
{
  static struct vif_def pd[NPORTS + 1];
  static int done = 0;
  enum status result = ST_OK;

  if (!done) {
    result = vif_get_hw_ports (pd);
    done = 1;
  }

  zmsg_t *reply = make_reply (result);
  if (result == ST_OK) {
    uint8_t n = NPORTS + 1;
    zmsg_addmem (reply, &n, sizeof (n));
    zmsg_addmem (reply, pd, sizeof (pd));
  }
  send_reply (reply);
}

DEFINE_HANDLER (CC_SET_VIF_PORTS)
{
  uint8_t d, n;
  struct vif_def *pd = NULL;
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
    pd = (struct vif_def *) zframe_data (frame);
  }

  result = vif_set_hw_ports (d, n, pd);

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
  DEBUG ("Trunk set members!!! \n");
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

  result = trunk_set_members (id, n, mem, evtntf_sock);

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

DEFINE_HANDLER (CC_PORT_ENABLE_LLDP)
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

  result = pcl_enable_lldp_trap (pid, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_ENABLE_LACP)
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

  result = pcl_enable_lacp_trap (pid, enable);

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

DEFINE_HANDLER (CC_VIF_ENABLE_EAPOL)
{
  enum status result;
  vif_id_t id;
  bool_t enable;

  result = POP_ARG (&id);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = vif_enable_eapol (id, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_FDB_NEW_ADDR_NOTIFY_ENABLE)
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

  result = port_fdb_new_addr_notify (pid, enable);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_VIF_EAPOL_AUTH)
{
  enum status result;
  vif_id_t id;
  vid_t vid;
  mac_addr_t mac;
  bool_t auth;

  result = POP_ARG (&id);
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

  result = vif_eapol_auth (id, vid, mac, auth);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PORT_FDB_ADDR_OP_NOTIFY_ENABLE)
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

  result = port_fdb_addr_op_notify (pid, enable);

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

DEFINE_HANDLER (CC_VLAN_SET_RSPAN)
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

  result = vlan_set_remote_span (vid, enable);

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

DEFINE_HANDLER (CC_GET_PORT_IP_SOURCEGUARD_RULE_START_IX)
{
  enum status result;
  port_id_t pid;

  if ((result = POP_ARG (&pid)) != ST_OK)
    goto out;

  uint16_t start_ix = get_port_ip_sourceguard_rule_start_ix(pid);

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &start_ix, sizeof (start_ix));
  send_reply (reply);
  return;

out:
  report_status (result);
}

DEFINE_HANDLER (CC_GET_PER_PORT_IP_SOURCEGUARD_RULES_COUNT)
{
  uint16_t count = get_per_port_ip_sourceguard_rules_count();

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &count, sizeof (count));
  send_reply (reply);
}

#define INIT_VAR(var) do {                          \
    if ((result = POP_ARG (&var)) != ST_OK) {       \
      DEBUG("%s: error: %s\n", __FUNCTION__, #var); \
      goto out;                                     \
    }                                               \
  } while (0)

#define INIT_PTR_SZ(ptr, size) do {                   \
    ptr = malloc(size);                               \
    assert(ptr);                                      \
    if ((result = POP_ARG_SZ (ptr, size)) != ST_OK) { \
      free(ptr);                                      \
      goto out;                                       \
    }                                                 \
  } while (0)

DEFINE_HANDLER (CC_USER_ACL_SET)
{
  enum status            result;
  struct pcl_interface   interface;
  pcl_dest_t             dest;
  uint16_t               rules_count;
  pcl_default_action_t   default_action;

  INIT_VAR(interface);
  INIT_VAR(dest);
  INIT_VAR(default_action);
  INIT_VAR(rules_count);

  int i;
  for (i = 0; i < rules_count; i++) {
    pcl_type_t        pcl_type;
    uint8_t           name_len;
    char              *name = NULL;
    pcl_rule_action_t rule_action;
    pcl_rule_num_t    rule_num;
    void              *rule_params = NULL;

    INIT_VAR(pcl_type);
    INIT_VAR(name_len);
    INIT_PTR_SZ(name, name_len);
    INIT_VAR(rule_action);
    INIT_VAR(rule_num);

    switch (pcl_type) {
      case PCL_TYPE_IP:
        INIT_PTR_SZ(rule_params, sizeof(struct ip_pcl_rule));
        result = pcl_ip_rule_set(name,
                                 name_len,
                                 rule_num,
                                 interface,
                                 dest,
                                 rule_action,
                                 rule_params);
        break;
      case PCL_TYPE_MAC:
        INIT_PTR_SZ(rule_params, sizeof(struct mac_pcl_rule));
        result = pcl_mac_rule_set(name,
                                  name_len,
                                  rule_num,
                                  interface,
                                  dest,
                                  rule_action,
                                  rule_params);
        break;
      case PCL_TYPE_IPV6:
        INIT_PTR_SZ(rule_params, sizeof(struct ipv6_pcl_rule));
        result = pcl_ipv6_rule_set(name,
                                   name_len,
                                   rule_num,
                                   interface,
                                   dest,
                                   rule_action,
                                   rule_params);
        break;
      default:
        result = ST_BAD_VALUE;
    };

    free(name);
    free(rule_params);

    if (result != ST_OK) {
      goto out;
    }
  }

  switch (default_action) {
    case PCL_DEFAULT_ACTION_DENY:
      // pcl_default_rule_set(interface, dest, default_action);
      break;
    case PCL_DEFAULT_ACTION_PERMIT:
      // pcl_default_rule_set(interface, dest, default_action);
      break;
    default:
      result = ST_BAD_VALUE;
  };

out:
  report_status (result);
}

DEFINE_HANDLER (CC_USER_ACL_RESET)
{
  enum status          result;
  struct pcl_interface interface;
  pcl_dest_t           dest;

  INIT_VAR(interface);
  INIT_VAR(dest);

  pcl_reset_rules(interface, dest);

out:
  report_status (ST_OK);
}

DEFINE_HANDLER (CC_USER_ACL_FAKE_MODE)
{
  enum status result;
  bool_t      fake_mode;

  INIT_VAR(fake_mode);

  pcl_set_fake_mode_enabled(fake_mode);
out:
  report_status (result);
}

DEFINE_HANDLER (CC_USER_ACL_GET_COUNTER)
{
  enum status          result;
  struct pcl_interface interface;
  pcl_dest_t           dest;
  uint8_t              name_len;
  char                 *name = NULL;
  pcl_rule_num_t       rule_num;

  uint64_t             counter;

  INIT_VAR(interface);
  INIT_VAR(dest);
  INIT_VAR(name_len);
  INIT_PTR_SZ(name, name_len);
  INIT_VAR(rule_num);

  result = pcl_get_counter(interface, dest, name, name_len, rule_num, &counter);

  free(name);

  if (result != ST_OK) {
    goto out;
  }

  zmsg_t *reply = make_reply (ST_OK);
  zmsg_addmem (reply, &counter, sizeof(counter));
  send_reply (reply);
  return;

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_USER_ACL_CLEAR_COUNTER)
{
  enum status          result;
  struct pcl_interface interface;
  pcl_dest_t           dest;
  uint8_t              name_len;
  char                 *name = NULL;
  pcl_rule_num_t       rule_num;

  INIT_VAR(interface);
  INIT_VAR(dest);
  INIT_VAR(name_len);
  INIT_PTR_SZ(name, name_len);
  INIT_VAR(rule_num);

  result = pcl_clear_counter(interface, dest, name, name_len, rule_num);

  free(name);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_PCL_TEST_START)
{
  enum status result = ST_OK;
  uint16_t pcl_id;
  uint16_t rule_ix;

  INIT_VAR(pcl_id);
  INIT_VAR(rule_ix);

  pcl_test_start(pcl_id, rule_ix);

out:
  report_status (result);
}

DEFINE_HANDLER (CC_PCL_TEST_ITER)
{
  pcl_test_iter();
  report_status (ST_OK);
}

DEFINE_HANDLER (CC_PCL_TEST_STOP)
{
  pcl_test_stop();
  report_status (ST_OK);
}

#undef INIT_VAR
#undef INIT_PTR_SZ

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

DEFINE_HANDLER (CC_VRRP_SET_MAC)
{
  enum status result;
  vid_t vid;
  mac_addr_t addr;
  bool_t set;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&addr);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&set);
  if (result != ST_OK)
    goto out;

  result = mac_op_own (vid, addr, set);

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_ARPD_SOCK_CONNECT)
{
  arpc_connect();

  report_status (ST_OK);
}

DEFINE_HANDLER (CC_STACK_SET_MASTER)
{
  enum status result;
  zframe_t *frame;
  uint8_t master;
  serial_t serial;
  devsbmp_t devsbmp;

  result = POP_ARG (&master);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&serial);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&devsbmp);
  if (result != ST_OK)
    goto out;

  frame = FIRST_ARG;
  if (!frame || zframe_size (frame) != 6) {
    result = ST_BAD_FORMAT;
    goto out;
  }

  result = stack_set_master (master, serial, devsbmp, zframe_data (frame));

 out:
  report_status (result);
}

DEFINE_HANDLER (CC_LOAD_BALANCE_MODE)
{
  enum status result;
  traffic_balance_mode_t mode;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = trunk_set_balance_mode (mode);

  out:
    report_status (result);
}

DEFINE_HANDLER (CC_INT_GET_RT_CMD) {
DEBUG(">>>>DEFINE_HANDLER (CC_INT_GET_RT_CMD)\n");
  void *r = NULL;
  uint32_t dummy;
  enum status result;
  zmsg_t *reply;

  result = POP_ARG (&dummy);
  if (result != ST_OK)
    goto out;

  r = fib_get_routes();
DEBUG("====DEFINE_HANDLER (CC_INT_GET_RT_CMD) == %p\n", r);

  out:

  reply = make_reply (ST_OK);
  zmsg_addmem (reply, &r, sizeof (r));
  send_reply (reply);
}

DEFINE_HANDLER (CC_INT_GET_UDADDRS_CMD) {
DEBUG(">>>>DEFINE_HANDLER (CC_INT_GET_UDADDRS_CMD)\n");
  void *r = NULL;
  uint32_t dummy;
  enum status result;
  zmsg_t *reply;

  result = POP_ARG (&dummy);
  if (result != ST_OK)
    goto out;

  r = route_get_udaddrs();
DEBUG("====DEFINE_HANDLER (CC_INT_GET_UDADDRS_CMD) == %p\n", r);

  out:

  reply = make_reply (ST_OK);
  zmsg_addmem (reply, &r, sizeof (r));
  send_reply (reply);
}

DEFINE_HANDLER (CC_GET_CH_REV)
{
  zmsg_t *reply;

  reply = make_reply(ST_OK);

  int d;
  uint8_t ndevs = NDEVS;

  zmsg_addmem(reply, &ndevs, sizeof(uint8_t));

  for_each_dev(d) {
    const char *revision = get_rev_str(d);
    zmsg_addmem(reply, revision, strlen(revision));
  }

  send_reply(reply);
}

DEFINE_HANDLER (CC_DIAG_DUMP_XG_PORT_QT2025_START)
{
  zmsg_t *reply;
  enum status result;
  port_id_t pid;
  uint8_t strlen;
  char *filename = NULL;

  result = POP_ARG(&pid);

  if (result != ST_OK) {
    goto out;
  }

  result = POP_ARG(&strlen);

  if (result != ST_OK) {
    goto out;
  }

  filename = malloc(strlen + 1);

  if (filename == NULL) {
    result = ST_MALLOC_ERROR;
    goto out;
  }

  memset(filename, 0, strlen + 1);

  result = POP_ARG_SZ(filename, strlen);

  if (result != ST_OK) {
    goto free;
  }

  result = diag_dump_xg_port_qt2025_start(pid, filename);

free:
  if (filename) {
    free(filename);
  }

out:
  reply = make_reply(result);
  send_reply(reply);
}

DEFINE_HANDLER (CC_DIAG_DUMP_XG_PORT_QT2025_CHECK)
{
  zmsg_t *reply;
  enum status result;
  bool_t in_progress;

  result = diag_dump_xg_port_qt2025_check(&in_progress);
  reply = make_reply(result);
  zmsg_addmem(reply, &in_progress, sizeof(in_progress));
  send_reply(reply);
}

DEFINE_HANDLER (CC_DIAG_DUMP_XG_PORT_QT2025)
{
  zmsg_t *reply;
  enum status result;
  port_id_t pid;
  uint32_t phy;
  uint32_t reg;
  uint16_t val;

  result = POP_ARG(&pid);

  if (result != ST_OK) {
    goto out;
  }

  result = POP_ARG(&phy);

  if (result != ST_OK) {
    goto out;
  }

  result = POP_ARG(&reg);

  if (result != ST_OK) {
    goto out;
  }

  result = diag_dump_xg_port_qt2025(pid, phy, reg, &val);

out:
  reply = make_reply(result);
  zmsg_addmem(reply, &val, sizeof(val));
  send_reply(reply);
}

/** @name sFlow functions */
///@{
/**
 * @brief Global enable/disable flow sampling
 *
 * @param[in] type sampling direction, type: sflow_type_t
 * @param[in] enable true - enable, false - disable, type: bool_t
 */
DEFINE_HANDLER (CC_SFLOW_SET_ENABLE)
{
  DEBUG("%s\n",__FUNCTION__);

  bool_t enable;
  enum status result;
  sflow_type_t type;

  result = POP_ARG (&type);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&enable);
  if (result != ST_OK)
    goto out;

  result = sflow_set_enable(type, enable);

out:
  report_status (result);
}

/**
 * @brief Set sFlow ingress count mode
 *
 * @param[in] mode count mode, type: sflow_count_mode_t
 */
DEFINE_HANDLER (CC_SFLOW_SET_INGRESS_COUNT_MODE)
{
  DEBUG("%s\n",__FUNCTION__);

  enum status result;
  sflow_count_mode_t mode;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = sflow_set_ingress_count_mode(mode);

out:
  report_status (result);
}

/**
 * @brief Set sFlow reload mode
 *
 * @param[in] type sampling direction, type: sflow_type_t
 * @param[in] mode reload mode, type: sflow_count_reload_mode_t
 */
DEFINE_HANDLER (CC_SFLOW_SET_RELOAD_MODE)
{
  DEBUG("%s\n",__FUNCTION__);

  enum status result;
  sflow_type_t type;
  sflow_count_reload_mode_t mode;

  result = POP_ARG (&type);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&mode);
  if (result != ST_OK)
    goto out;

  result = sflow_set_reload_mode(type, mode);

out:
  report_status (result);
}

/**
 * @brief Set sFlow configuration on port
 *
 * @param[in] params port configuration, type: struct sflow_port_limit_info
 */
DEFINE_HANDLER (CC_SFLOW_SET_PORT_LIMIT)
{
  DEBUG("%s\n",__FUNCTION__);

  enum status result;
  struct sflow_port_limit_info params;

  result = POP_ARG (& params);
  if (result != ST_OK)
    goto out;

  result = sflow_set_port_limit(
      params.pid,
      params.direction,
      params.rate,
      false);

out:
  report_status (result);
}

/**
 * @brief Set the default sFlow settngs
 *
 */
DEFINE_HANDLER (CC_SFLOW_SET_DEFAULT)
{
  DEBUG("%s\n",__FUNCTION__);

  enum status result;

  result = sflow_set_enable(BOTH, false);
  if (result != ST_OK)
    goto out;

  result = sflow_set_reload_mode(BOTH, RELOAD_CONTINUOUS);
  if (result != ST_OK)
    goto out;

  port_id_t pid;
  for (pid = 1; pid <= NPORTS; pid++) {
    result = sflow_set_port_limit(pid, BOTH, 0, true);
    if (result != ST_OK)
      goto out;
  }

out:
  report_status (result);
}
///@} /* End sFlow functions. */
