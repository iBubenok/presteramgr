#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <presteramgr.h>
#include <debug.h>
#include <sysdeps.h>
#include <variant.h>
#include <port.h>
#include <control.h>
#include <data.h>
#include <vlan.h>
#include <qos.h>
#include <utils.h>
#include <env.h>
#include <pcl.h>

#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortStat.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortTx.h>
#include <cpss/dxCh/dxChxGen/phy/cpssDxChPhySmi.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgStp.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgEgrFlt.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgGen.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgPrvEdgeVlan.h>
#include <cpss/dxCh/dxChxGen/cos/cpssDxChCos.h>
#include <cpss/dxCh/dxChxGen/cscd/cpssDxChCscd.h>
#include <cpss/dxCh/dxChxGen/nst/cpssDxChNstPortIsolation.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIpCtrl.h>
#include <cpss/generic/cscd/cpssGenCscd.h>
#include <cpss/generic/port/cpssPortTx.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>
#include <cpss/generic/smi/cpssGenSmi.h>
#include <cpss/generic/phy/cpssGenPhyVct.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgNestVlan.h>

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>


DECLSHOW (CPSS_PORT_SPEED_ENT);
DECLSHOW (CPSS_PORT_DUPLEX_ENT);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

struct port *ports = NULL;
int nports = 0;

static port_id_t *port_ids;

static CPSS_PORTS_BMP_STC all_ports_bmp;

static enum status port_set_speed_fe (struct port *, const struct port_speed_arg *);
static enum status port_set_duplex_fe (struct port *, enum port_duplex);
static enum status port_update_sd_fe (struct port *);
static enum status __port_shutdown_fe (GT_U8, GT_U8, int);
static enum status port_shutdown_fe (struct port *, int);
static enum status port_set_mdix_auto_fe (struct port *, int);
static enum status __port_setup_fe (GT_U8, GT_U8);
static enum status port_setup_fe (struct port *);
static enum status port_set_speed_ge (struct port *, const struct port_speed_arg *);
static enum status port_set_duplex_ge (struct port *, enum port_duplex);
static enum status port_update_sd_ge (struct port *);
static enum status port_shutdown_ge (struct port *, int);
static enum status port_set_mdix_auto_ge (struct port *, int);
static enum status port_setup_ge (struct port *);
static enum status port_set_speed_xg (struct port *, const struct port_speed_arg *);
static enum status port_set_duplex_xg (struct port *, enum port_duplex);
static enum status port_update_sd_xg (struct port *);
static enum status port_shutdown_xg (struct port *, int);
static enum status port_set_mdix_auto_xg (struct port *, int);
static enum status port_setup_xg (struct port *);

static inline void
port_lock (void)
{
  pthread_mutex_lock (&lock);
}

static inline void
port_unlock (void)
{
  pthread_mutex_unlock (&lock);
}

int
port_id (GT_U8 ldev, GT_U8 lport)
{
  if (!((ldev < NDEVS) &&
        (lport < CPSS_MAX_PORTS_NUM_CNS))) {
    ERR ("ldev = %d, lport = %d\r\n", ldev, lport);
    return 0;
  }

  return port_ids[ldev * CPSS_MAX_PORTS_NUM_CNS + lport];
}

static void
port_set_vid (struct port *port)
{
  vid_t vid = 0; /* Make the compiler happy. */

  switch (port->mode) {
  case PM_ACCESS:   vid = port->access_vid;   break;
  case PM_TRUNK:    vid = port->native_vid;   break;
  case PM_CUSTOMER: vid = port->customer_vid; break;
  }
  CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
}

enum status
port_update_qos_trust (const struct port *port)
{
  GT_STATUS rc;
  CPSS_QOS_PORT_TRUST_MODE_ENT trust;

  if (mls_qos_trust) {
    if (port->trust_cos && port->trust_dscp)
      trust = CPSS_QOS_PORT_TRUST_L2_L3_E;
    else if (port->trust_cos)
      trust = CPSS_QOS_PORT_TRUST_L2_E;
    else if (port->trust_dscp)
      trust = CPSS_QOS_PORT_TRUST_L3_E;
    else
      trust = CPSS_QOS_PORT_NO_TRUST_E;
  } else
    trust = CPSS_QOS_PORT_NO_TRUST_E;

  rc = CRP (cpssDxChCosPortQosTrustModeSet (port->ldev, port->lport, trust));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}

static void
port_setup_stats (GT_U8 ldev, GT_U8 lport)
{
  CPSS_PORT_MAC_COUNTER_SET_STC stats;

  CRP (cpssDxChPortMacCountersEnable (ldev, lport, GT_TRUE));
  CRP (cpssDxChPortMacCountersClearOnReadSet (ldev, lport, GT_TRUE));
  CRP (cpssDxChPortMacCountersOnPortGet (ldev, lport, &stats));
  CRP (cpssDxChPortMacCountersClearOnReadSet (ldev, lport, GT_FALSE));
  CRP (cpssDxChPortMacCountersEnable (ldev, lport, GT_TRUE));
}

int
port_init (void)
{
  DECLARE_PORT_MAP (pmap);
  int i;

  port_ids = calloc (NDEVS * CPSS_MAX_PORTS_NUM_CNS, sizeof (port_id_t));
  assert (port_ids);

  ports = calloc (NPORTS, sizeof (struct port));
  assert (ports);

  memset (&all_ports_bmp, 0, sizeof (all_ports_bmp));

  for (i = 0; i < NPORTS; i++) {
    ports[i].id = i + 1;
    ports[i].ldev = 0;
    ports[i].lport = pmap[i];
    ports[i].mode = PM_ACCESS;
    ports[i].access_vid = 1;
    ports[i].native_vid = 1;
    ports[i].trust_cos = 0;
    ports[i].trust_dscp = 0;
    ports[i].c_speed = PORT_SPEED_AUTO;
    ports[i].c_speed_auto = 1;
    ports[i].c_duplex = PORT_DUPLEX_AUTO;
    ports[i].c_shutdown = 0;
    ports[i].c_protected = 0;
    ports[i].c_prot_comm = 0;
    ports[i].tdr_test_in_progress = 0;
    CPSS_PORTS_BMP_PORT_SET_MAC (&all_ports_bmp, pmap[i]);
    if (IS_FE_PORT (i)) {
      ports[i].max_speed = PORT_SPEED_100;
      ports[i].set_speed = port_set_speed_fe;
      ports[i].set_duplex = port_set_duplex_fe;
      ports[i].update_sd = port_update_sd_fe;
      ports[i].shutdown = port_shutdown_fe;
      ports[i].set_mdix_auto = port_set_mdix_auto_fe;
      ports[i].setup = port_setup_fe;
    } else if (IS_GE_PORT (i)) {
      ports[i].max_speed = PORT_SPEED_1000;
      ports[i].set_speed = port_set_speed_ge;
      ports[i].set_duplex = port_set_duplex_ge;
      ports[i].update_sd = port_update_sd_ge;
      ports[i].shutdown = port_shutdown_ge;
      ports[i].set_mdix_auto = port_set_mdix_auto_ge;
      ports[i].setup = port_setup_ge;
    } else if (IS_XG_PORT (i)) {
      ports[i].max_speed = PORT_SPEED_10000;
      ports[i].set_speed = port_set_speed_xg;
      ports[i].set_duplex = port_set_duplex_xg;
      ports[i].update_sd = port_update_sd_xg;
      ports[i].shutdown = port_shutdown_xg;
      ports[i].set_mdix_auto = port_set_mdix_auto_xg;
      ports[i].setup = port_setup_xg;
    } else {
      /* We should never get here. */
      EMERG ("Port specification error at %d, aborting", i);
      abort ();
    }
    port_ids[ports[i].ldev * CPSS_MAX_PORTS_NUM_CNS + ports[i].lport] =
      ports[i].id;
  }

  nports = NPORTS;

  return 0;
}

static void
port_set_iso_bmp (struct port *port)
{
  if (port->iso_bmp_changed) {
    CPSS_INTERFACE_INFO_STC iface = {
      .type = CPSS_INTERFACE_PORT_E,
      .devPort = {
        .devNum = port->ldev,
        .portNum = port->lport
      }
    };

    CRP (cpssDxChNstPortIsolationTableEntrySet
         (port->ldev,
          CPSS_DXCH_NST_PORT_ISOLATION_TRAFFIC_TYPE_L2_E,
          iface,
          GT_TRUE,
          &port->iso_bmp));
    CRP (cpssDxChNstPortIsolationTableEntrySet
         (port->ldev,
          CPSS_DXCH_NST_PORT_ISOLATION_TRAFFIC_TYPE_L3_E,
          iface,
          GT_TRUE,
          &port->iso_bmp));
    port->iso_bmp_changed = 0;
  }
}

static int
__port_set_comm (struct port *t, uint8_t comm)
{
  int i;

  if (t->c_prot_comm == comm)
    return 0;

  if (t->c_protected) {
    for (i = 0; i < nports; i++) {
      struct port *o = &ports[i];

      if (!o->c_protected)
        continue;

      if (o->id == t->id)
        continue;

      if (t->c_prot_comm && t->c_prot_comm == o->c_prot_comm) {
        CPSS_PORTS_BMP_PORT_CLEAR_MAC (&t->iso_bmp, o->lport);
        t->iso_bmp_changed = 1;
        CPSS_PORTS_BMP_PORT_CLEAR_MAC (&o->iso_bmp, t->lport);
        o->iso_bmp_changed = 1;
      }

      if (comm && comm == o->c_prot_comm) {
        CPSS_PORTS_BMP_PORT_SET_MAC (&t->iso_bmp, o->lport);
        t->iso_bmp_changed = 1;
        CPSS_PORTS_BMP_PORT_SET_MAC (&o->iso_bmp, t->lport);
        o->iso_bmp_changed = 1;
      }
    }
  }

  t->c_prot_comm = comm;
  return t->c_protected;
}

static int
__port_set_prot (struct port *t, int prot)
{
  int i;

  if (t->c_protected == prot)
    return 0;

  for (i = 0; i < nports; i++) {
    struct port *o = &ports[i];

    if (o->id == t->id)
      continue;

    if (prot) {
      if (o->c_protected) {
        if (t->c_prot_comm && o->c_prot_comm == t->c_prot_comm) {
          CPSS_PORTS_BMP_PORT_SET_MAC (&t->iso_bmp, o->lport);
          t->iso_bmp_changed = 1;
          CPSS_PORTS_BMP_PORT_SET_MAC (&o->iso_bmp, t->lport);
          o->iso_bmp_changed = 1;
        } else {
          CPSS_PORTS_BMP_PORT_CLEAR_MAC (&t->iso_bmp, o->lport);
          t->iso_bmp_changed = 1;
          CPSS_PORTS_BMP_PORT_CLEAR_MAC (&o->iso_bmp, t->lport);
          o->iso_bmp_changed = 1;
        }
      } else {
        CPSS_PORTS_BMP_PORT_SET_MAC (&t->iso_bmp, o->lport);
        t->iso_bmp_changed = 1;
        CPSS_PORTS_BMP_PORT_SET_MAC (&o->iso_bmp, t->lport);
        o->iso_bmp_changed = 1;
      }
    } else {
      CPSS_PORTS_BMP_PORT_SET_MAC (&t->iso_bmp, o->lport);
      t->iso_bmp_changed = 1;
      CPSS_PORTS_BMP_PORT_SET_MAC (&o->iso_bmp, t->lport);
      o->iso_bmp_changed = 1;
    }
  }

  t->c_protected = prot;
  return 1;
}

enum status
port_start (void)
{
  int i;

#if defined (VARIANT_FE)
  CRP (cpssDxChPhyAutoPollNumOfPortsSet
       (0, CPSS_DXCH_PHY_SMI_AUTO_POLL_NUM_OF_PORTS_16_E,
        CPSS_DXCH_PHY_SMI_AUTO_POLL_NUM_OF_PORTS_8_E));
#endif /* VARIANT_FE */

  for (i = 0; i < PRV_CPSS_PP_MAC (0)->numOfPorts; i++)
    if (PRV_CPSS_PP_MAC (0)->phyPortInfoArray[i].portType !=
        PRV_CPSS_PORT_NOT_EXISTS_E)
    CRP (cpssDxChPortEnableSet (0, i, GT_FALSE));

#if defined (VARIANT_SM_12F)
  /* Shut down unused PHYs. */
  for (i = 8; i < 12; i++) {
    __port_setup_fe (0, i);
    __port_shutdown_fe (0, i, 1);
  }
#endif /* VARIANT_SM_12F */

  CRP (cpssDxChNstPortIsolationEnableSet (0, GT_TRUE));
  CRP (cpssDxChBrgVlanEgressFilteringEnable (0, GT_TRUE));

  for (i = 0; i < nports; i++) {
    struct port *port = &ports[i];

    port->setup (port);
    CRP (cpssDxChBrgVlanPortIngFltEnable (port->ldev, port->lport, GT_TRUE));
    port_set_vid (port);
    port_update_qos_trust (port);
    port_setup_stats (port->ldev, port->lport);
    CRP (cpssDxChBrgFdbNaToCpuPerPortSet (port->ldev, port->lport, GT_FALSE));
    CRP (cpssDxChBrgGenPortIeeeReservedMcastProfileIndexSet
         (port->ldev, port->lport, 0));
    CRP (cpssDxChIpPortRoutingEnable
         (port->ldev, port->lport,
          CPSS_IP_UNICAST_E, CPSS_IP_PROTOCOL_IPV4_E,
          GT_TRUE));
    CRP (cpssDxChPortTxBindPortToSchedulerProfileSet
         (port->ldev, port->lport, CPSS_PORT_TX_SCHEDULER_PROFILE_1_E));

    /* QoS initial setup. */
    CRP (cpssDxChCosPortReMapDSCPSet (port->ldev, port->lport, GT_FALSE));
    CRP (cpssDxChPortDefaultUPSet (port->ldev, port->lport, 0));
    CPSS_QOS_ENTRY_STC qe = {
      .qosProfileId = 0,
      .assignPrecedence = CPSS_PACKET_ATTRIBUTE_ASSIGN_PRECEDENCE_HARD_E,
      .enableModifyUp = CPSS_PACKET_ATTRIBUTE_MODIFY_DISABLE_E,
      .enableModifyDscp = CPSS_PACKET_ATTRIBUTE_MODIFY_DISABLE_E
    };
    CRP (cpssDxChCosPortQosConfigSet (port->ldev, port->lport, &qe));
    CRP (cpssDxChCosL2TrustModeVlanTagSelectSet
         (port->ldev, port->lport, CPSS_VLAN_TAG0_E));

    CRP (cpssDxChPortTxByteCountChangeValueSet (port->ldev, port->lport, 12));

    port->iso_bmp = all_ports_bmp;
    port->iso_bmp_changed = 1;
    port_set_iso_bmp (port);

    CRP (cpssDxChBrgVlanPortEgressTpidSet
         (port->ldev, port->lport,
          CPSS_VLAN_ETHERTYPE0_E, VLAN_TPID_IDX));
    CRP (cpssDxChBrgVlanPortEgressTpidSet
         (port->ldev, port->lport,
          CPSS_VLAN_ETHERTYPE1_E, VLAN_TPID_IDX));

#ifdef PRESTERAMGR_FUTURE_LION
    CRP (cpssDxChPortTxShaperModeSet
         (ports->ldev, port->lport,
          CPSS_PORT_TX_DROP_SHAPER_BYTE_MODE_E));
#endif /* PRESTERAMGR_FUTURE_LION */

    pcl_port_setup (port->id);
    pcl_enable_port (port->id, 1);
    pcl_enable_lbd_trap (port->id, 1);
  }

  port_set_mru (1526);
  port_setup_stats (0, CPSS_CPU_PORT_NUM_CNS);
  CRP (cpssDxChCscdPortTypeSet
       (0, CPSS_CPU_PORT_NUM_CNS,
        CPSS_CSCD_PORT_DSA_MODE_EXTEND_E));
  CRP (cpssDxChPortMruSet (0, CPSS_CPU_PORT_NUM_CNS, 10000));
  CRP (cpssDxChBrgFdbNaToCpuPerPortSet (0, CPSS_CPU_PORT_NUM_CNS, GT_FALSE));

  CRP (cpssDxChBrgPrvEdgeVlanEnable (0, GT_TRUE));

  CRP (cpssDxChPortTxShaperGlobalParamsSet (0, 15, 15, 1));
  CRP (cpssDxChPortTxByteCountChangeEnableSet
       (0, CPSS_DXCH_PORT_TX_BC_CHANGE_ENABLE_SHAPER_ONLY_E));

  for (i = 0; i < nports; i++) {
    struct port *port = &ports[i];

    CRP (cpssDxChBrgStpStateSet
         (port->ldev, port->lport, 0, CPSS_STP_BLCK_LSTN_E));
    CRP (cpssDxChPortEnableSet (port->ldev, port->lport, GT_TRUE));
    port->shutdown (port, 0);
    port->update_sd (port);
  };

  CRP (cpssDxChPortTxToCpuShaperModeSet
       (0, CPSS_PORT_TX_DROP_SHAPER_PACKET_MODE_E));

  GT_U32 rate = 5000;
  CRP (cpssDxChPortTxShaperProfileSet (0, CPSS_CPU_PORT_NUM_CNS, 1, &rate));
  CRP (cpssDxChPortTxShaperEnableSet (0, CPSS_CPU_PORT_NUM_CNS, GT_TRUE));

  CRP (cpssDxChPortEnableSet (0, CPSS_CPU_PORT_NUM_CNS, GT_TRUE));

  return ST_OK;
}

#if defined (VARIANT_FE)
static GT_STATUS
port_set_sgmii_mode (const struct port *port)
{
  CRPR (cpssDxChPortInterfaceModeSet
        (port->ldev, port->lport, CPSS_PORT_INTERFACE_MODE_SGMII_E));
  CRPR (cpssDxChPortSpeedSet
        (port->ldev, port->lport, CPSS_PORT_SPEED_1000_E));
  CRPR (cpssDxChPortSerdesPowerStatusSet
        (port->ldev, port->lport, CPSS_PORT_DIRECTION_BOTH_E, 0x01, GT_TRUE));
  CRPR (cpssDxChPortInbandAutoNegEnableSet (port->ldev, port->lport, GT_TRUE));

  return GT_OK;
}
#endif /* VARIANT_FE */

int
port_exists (GT_U8 dev, GT_U8 port)
{
  return CPSS_PORTS_BMP_IS_PORT_SET_MAC
    (&(PRV_CPSS_PP_MAC (dev)->existingPorts), port);
}

enum status
port_handle_link_change (GT_U8 ldev, GT_U8 lport, port_id_t *pid, CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  struct port *port;
  GT_STATUS rc;

  *pid = port_id (ldev, lport);
  port = port_ptr (*pid);
  if (!port)
    return ST_DOES_NOT_EXIST;

  rc = CRP (cpssDxChPortAttributesOnPortGet (port->ldev, port->lport, attrs));
  if (rc != GT_OK)
    return ST_HEX;

  port_lock ();

  if (attrs->portLinkUp    != port->state.attrs.portLinkUp ||
      attrs->portSpeed     != port->state.attrs.portSpeed  ||
      attrs->portDuplexity != port->state.attrs.portDuplexity) {
    port->state.attrs = *attrs;
#ifdef DEBUG_STATE
    if (attrs->portLinkUp)
      osPrintSync ("port %2d link up at %s, %s\n", n,
                   SHOW (CPSS_PORT_SPEED_ENT, attrs->portSpeed),
                   SHOW (CPSS_PORT_DUPLEX_ENT, attrs->portDuplexity));
    else
      osPrintSync ("port %2d link down\n", n);
#endif /* DEBUG_STATE */
  }

  port_unlock ();

  return ST_OK;
}

enum status
port_get_state (port_id_t pid, struct port_link_state *state)
{
  enum status result = ST_BAD_VALUE;
  struct port *port = port_ptr (pid);

  port_lock ();

  if (!port)
    goto out;

  data_encode_port_state (state, &port->state.attrs);
  result = ST_OK;

 out:
  port_unlock ();
  return result;
}

enum status
port_set_stp_state (port_id_t pid, stp_id_t stp_id,
                    int all, enum port_stp_state state)
{
  CPSS_STP_STATE_ENT cs;
  enum status result;
  struct port *port;

  if (!(port = port_ptr (pid)) || stp_id > 255)
    return ST_BAD_VALUE;

  result = data_decode_stp_state (&cs, state);
  if (result != ST_OK)
    return result;

  if (all) {
    stp_id_t stg;
    /* FIXME: suboptimal code. */
    for (stg = 0; stg < 256; stg++)
      if (stg_is_active (stg))
        CRP (cpssDxChBrgStpStateSet (port->ldev, port->lport, stg, cs));
  } else
    CRP (cpssDxChBrgStpStateSet (port->ldev, port->lport, stp_id, cs));

  return ST_OK;
}


enum status
port_set_access_vid (port_id_t pid, vid_t vid)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc = GT_OK;

  if (!(port && vlan_valid (vid)))
    return ST_BAD_VALUE;

  if (port->mode == PM_ACCESS) {
    rc = CRP (cpssDxChBrgVlanPortDelete
              (port->ldev,
               port->access_vid,
               port->lport));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChBrgVlanMemberSet
              (port->ldev,
               vid,
               port->lport,
               GT_TRUE,
               GT_FALSE,
               CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
    if (rc != GT_OK)
      goto out;

    cn_port_vid_set (pid, vid);
  }

  port->access_vid = vid;

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_update_trunk_vlan (struct port *port, vid_t vid)
{
  CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT cmd;
  GT_BOOL tag, mem;
  GT_STATUS rc;
  int vid_ix = vid - 1;

  if (vlans[vid_ix].state == VS_DELETED)
    return ST_OK;

  mem = GT_TRUE;
  tag = GT_TRUE;
  cmd = CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E;

  if (port->vlan_conf[vid_ix].refc) {
    /* VLAN translation source port. */
    cmd = vlan_xlate_tunnel
      ? CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E
      : CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
  } else if (vid == port->native_vid && !vlan_dot1q_tag_native) {
    /* Trunk native VLAN. */
    tag = GT_FALSE;
    cmd = CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
  } else if (!port->vlan_conf[vid_ix].tallow) {
    /* Not a member. */
    mem = GT_FALSE;
  } else if (vlans[vid_ix].vt_refc && vlan_xlate_tunnel) {
    cmd = CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
  }

  rc = CRP (cpssDxChBrgVlanMemberSet
            (port->ldev,
             vid,
             port->lport,
             mem,
             tag,
             cmd));

  switch (rc) {
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

void
port_update_trunk_vlan_all_ports (vid_t vid)
{
  int i;

  for (i = 0; i < nports; i++)
    if (ports[i].mode == PM_TRUNK)
      port_update_trunk_vlan (&ports[i], vid);
}

enum status
port_set_native_vid (port_id_t pid, vid_t vid)
{
  struct port *port = port_ptr (pid);
  vid_t old;

  if (!(port && vlan_valid (vid)))
    return ST_BAD_VALUE;

  old = port->native_vid;
  port->native_vid = vid;

  if (port->mode == PM_TRUNK) {
    port_update_trunk_vlan (port, old);
    port_update_trunk_vlan (port, vid);
    CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
    cn_port_vid_set (pid, vid);
  }

  port->native_vid = vid;

  return ST_OK;
}

enum status
port_set_customer_vid (port_id_t pid, vid_t vid)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc = GT_OK;

  if (!(port && vlan_valid (vid)))
    return ST_BAD_VALUE;

  if (port->mode == PM_CUSTOMER) {
    rc = CRP (cpssDxChBrgVlanPortDelete
              (port->ldev,
               port->customer_vid,
               port->lport));
    ON_GT_ERROR (rc) goto out;

    rc = CRP (cpssDxChBrgVlanMemberSet
              (port->ldev,
               vid,
               port->lport,
               GT_TRUE,
               GT_TRUE,
               CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E));
    ON_GT_ERROR (rc) goto out;

    rc = CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
    ON_GT_ERROR (rc) goto out;

    cn_port_vid_set (pid, vid);
  }

  port->customer_vid = vid;

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_vlan_bulk_op (struct port *port,
                   vid_t vid,
                   GT_BOOL vid_tag,
                   CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT vid_tag_cmd,
                   GT_BOOL force_pvid,
                   int tpid_idx,
                   GT_BOOL rest_member,
                   GT_BOOL rest_tag,
                   CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT rest_tag_cmd)
{
  GT_STATUS rc;
  int i;

  for (i = 0; i < NVLANS; i++) {
    if (vlans[i].state == VS_DELETED || vid == i + 1)
      continue;

    rc = CRP (cpssDxChBrgVlanMemberSet
              (port->ldev,
               i + 1,
               port->lport,
               rest_member,
               rest_tag,
               rest_tag_cmd));
    ON_GT_ERROR (rc) goto err;
  }

  rc = CRP (cpssDxChBrgVlanMemberSet
            (port->ldev,
             vid,
             port->lport,
             GT_TRUE,
             vid_tag,
             vid_tag_cmd));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanPortVidSet
            (port->ldev,
             port->lport,
             vid));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanForcePvidEnable
            (port->ldev,
             port->lport,
             force_pvid));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanPortIngressTpidSet
            (port->ldev, port->lport,
             CPSS_VLAN_ETHERTYPE0_E, 1 << tpid_idx));
  ON_GT_ERROR (rc) goto err;
  rc = CRP (cpssDxChBrgVlanPortIngressTpidSet
            (port->ldev, port->lport,
             CPSS_VLAN_ETHERTYPE1_E, 1 << tpid_idx));
  ON_GT_ERROR (rc) goto err;

  cn_port_vid_set (port->id, vid);

  return ST_OK;

 err:
  switch (rc) {
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}


static enum status
port_set_trunk_mode (struct port *port)
{
  GT_STATUS rc;
  int i;

  for (i = 0; i < NVLANS; i++)
    port_update_trunk_vlan (port, i + 1);

  rc = CRP (cpssDxChBrgVlanPortVidSet
            (port->ldev,
             port->lport,
             port->native_vid));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanForcePvidEnable
            (port->ldev,
             port->lport,
             GT_FALSE));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanPortIngressTpidSet
            (port->ldev, port->lport,
             CPSS_VLAN_ETHERTYPE0_E, 1 << VLAN_TPID_IDX));
  ON_GT_ERROR (rc) goto err;
  rc = CRP (cpssDxChBrgVlanPortIngressTpidSet
            (port->ldev, port->lport,
             CPSS_VLAN_ETHERTYPE1_E, 1 << VLAN_TPID_IDX));
  ON_GT_ERROR (rc) goto err;

  cn_port_vid_set (port->id, port->native_vid);

  return ST_OK;

 err:
  switch (rc) {
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_set_access_mode (struct port *port)
{
  return port_vlan_bulk_op (port,
                            port->access_vid,
                            GT_FALSE,
                            CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E,
                            GT_FALSE,
                            VLAN_TPID_IDX,
                            GT_FALSE,
                            GT_FALSE,
                            CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E);
}

static enum status
port_set_customer_mode (struct port *port)
{
  port_vlan_bulk_op (port,
                     port->customer_vid,
                     GT_TRUE,
                     CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E,
                     GT_TRUE,
                     FAKE_TPID_IDX,
                     GT_FALSE,
                     GT_FALSE,
                     CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E);

  return ST_OK;
}

enum status
port_set_mode (port_id_t pid, enum port_mode mode)
{
  struct port *port = port_ptr (pid);
  enum status result;

  if (!port)
    return ST_BAD_VALUE;

  if (port->mode == mode)
    return ST_OK;

  switch (mode) {
  case PM_ACCESS:
    result = port_set_access_mode (port);
    break;
  case PM_TRUNK:
    result = port_set_trunk_mode (port);
    break;
  case PM_CUSTOMER:
    result = port_set_customer_mode (port);
    break;
  default:
    result = ST_BAD_VALUE;
  }

  if (result == ST_OK) {
    pcl_port_enable_vt (pid, mode == PM_TRUNK);
    port->mode = mode;
  }

  return result;
}

enum status
port_shutdown (port_id_t pid, int shutdown)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  shutdown = !!shutdown;

  if (port->c_shutdown == shutdown)
    return ST_OK;

  port->c_shutdown = shutdown;

  return port->shutdown (port, shutdown);
}

enum status
port_block (port_id_t pid, const struct port_block *what)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  switch (what->type) {
  case TT_UNICAST:
    rc = CRP (cpssDxChBrgPortEgrFltUnkEnable
              (port->ldev,
               port->lport,
               !!what->block));
    break;

  case TT_MULTICAST:
    rc = CRP (cpssDxChBrgPortEgrFltUregMcastEnable
              (port->ldev,
               port->lport,
               !!what->block));
    break;

  case TT_BROADCAST:
    rc = CRP (cpssDxChBrgPortEgrFltUregBcEnable
              (port->ldev,
               port->lport,
               !!what->block));
    break;

  default:
    return ST_BAD_VALUE;
  }

  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_update_sd_ge (struct port *port)
{
  GT_STATUS rc;
  GT_U16 reg, reg1;

  if (port->c_speed_auto || port->c_duplex == PORT_DUPLEX_AUTO) {
    /* Speed or duplex is AUTO. */

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x04, &reg));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x09, &reg1));
    if (rc != GT_OK)
      goto out;

    reg |= 0x01E0;

    switch (port->c_speed) {
    case PORT_SPEED_AUTO:
      reg1 |= 1 << 9;
      break;
    case PORT_SPEED_10:
      reg &= ~((1 << 7) | (1 << 8));
      reg1 &= ~(1 << 9);
      break;
    case PORT_SPEED_100:
      reg &= ~((1 << 5) | (1 << 6));
      reg1 &= ~(1 << 9);
      break;
    case PORT_SPEED_1000:
      reg &= ~0x01E0;
      reg1 |= 1 << 9;
      break;
    default:
      /* We should never get here. */
      return ST_BAD_VALUE;
    }

    switch (port->c_duplex) {
    case PORT_DUPLEX_FULL:
      reg &= ~((1 << 5) | (1 << 7));
      break;
    case PORT_DUPLEX_HALF:
      reg &= ~((1 << 6) | (1 << 8));
      break;
    default:
      break;
    }

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x04, reg));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x09, reg1));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x00, &reg));
    if (rc != GT_OK)
      goto out;

    reg |= (1 << 12) | (1 << 15);

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x00, reg));
  } else {
    /* Everything is set the hard way. */

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x00, &reg));
    if (rc != GT_OK)
      goto out;

    if (port->c_duplex == PORT_DUPLEX_FULL)
      reg |= 1 << 8;
    else
      reg &= ~(1 << 8);

    switch (port->c_speed) {
    case PORT_SPEED_10:
      reg &= ~((1 << 6) | (1 << 13));
      break;
    case PORT_SPEED_100:
      reg &= ~(1 << 6);
      reg |= 1 << 13;
      break;
    case PORT_SPEED_1000:
      reg |= (1 << 6) | (1 << 13);
      break;
    default:
      /* We should never get here. */
      return ST_BAD_VALUE;
    }

    reg |= 1 << 15;
    reg &= ~(1 << 12);

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x00, reg));

  }

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}


static enum status
port_update_sd_fe (struct port *port)
{
  GT_STATUS rc;
  GT_U16 reg;

  if (port->c_speed_auto || port->c_duplex == PORT_DUPLEX_AUTO) {
    /* Speed or duplex is AUTO. */

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x04, &reg));
    if (rc != GT_OK)
      goto out;

    reg |= 0x01E0;

    switch (port->c_speed) {
    case PORT_SPEED_10:
      reg &= ~((1 << 7) | (1 << 8));
      break;
    case PORT_SPEED_100:
      reg &= ~((1 << 5) | (1 << 6));
      break;
    default:
      break;
    }

    switch (port->c_duplex) {
    case PORT_DUPLEX_FULL:
      reg &= ~((1 << 5) | (1 << 7));
      break;
    case PORT_DUPLEX_HALF:
      reg &= ~((1 << 6) | (1 << 8));
      break;
    default:
      break;
    }

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x04, reg));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x00, &reg));
    if (rc != GT_OK)
      goto out;

    reg |= (1 << 12) | (1 << 15);

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x00, reg));
  } else {
    /* Everything is set the hard way. */

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x00, &reg));
    if (rc != GT_OK)
      goto out;

    if (port->c_duplex == PORT_DUPLEX_FULL)
      reg |= 1 << 8;
    else
      reg &= ~(1 << 8);

    if (port->c_speed == PORT_SPEED_100)
      reg |= 1 << 13;
    else
      reg &= ~(1 << 13);

    reg |= 1 << 15;
    reg &= ~(1 << 12);

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x00, reg));

  }

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_set_speed_fe (struct port *port, const struct port_speed_arg *psa)
{
  if (psa->speed > PORT_SPEED_100)
    return ST_BAD_VALUE;

  port->c_speed = psa->speed;
  port->c_speed_auto = psa->speed_auto;

  return port_update_sd_fe (port);
}

static enum status
port_set_duplex_fe (struct port *port, enum port_duplex duplex)
{
  port->c_duplex = duplex;
  return port_update_sd_fe (port);
}

static enum status
__port_shutdown_fe (GT_U8 dev, GT_U8 port, int shutdown)
{
  GT_STATUS rc;
  GT_U16 reg;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead (dev, port, 0x00, &reg));
  if (rc != GT_OK)
    goto out;

  if (shutdown)
    reg |= (1 << 11);
  else
    reg &= ~(1 << 11);

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite (dev, port, 0x00, reg));

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_shutdown_fe (struct port *port, int shutdown)
{
  return __port_shutdown_fe (port->ldev, port->lport, shutdown);
}

static enum status
__port_setup_fe (GT_U8 dev, GT_U8 port)
{
  CRP (cpssDxChPhyPortAddrSet (dev, port, (GT_U8) (port % 16)));
  return ST_OK;
}

static enum status
port_setup_fe (struct port *port)
{
  return __port_setup_fe (port->ldev, port->lport);
}

static enum status
port_set_speed_ge (struct port *port, const struct port_speed_arg *psa)
{
  if (psa->speed > PORT_SPEED_1000)
    return ST_BAD_VALUE;

  port->c_speed = psa->speed;
  port->c_speed_auto = psa->speed_auto;

  return port_update_sd_ge (port);
}

static enum status
port_set_duplex_ge (struct port *port, enum port_duplex duplex)
{
  port->c_duplex = duplex;
  return port_update_sd_ge (port);
}

static enum status
port_shutdown_ge (struct port *port, int shutdown)
{
  GT_STATUS rc;
  GT_U16 reg;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x00, &reg));
  if (rc != GT_OK)
    goto out;

  if (shutdown)
    reg |= (1 << 11);
  else
    reg &= ~(1 << 11);

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x00, reg));

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_set_speed_xg (struct port *port, const struct port_speed_arg *psa)
{
  DEBUG ("%s(): STUB!", __PRETTY_FUNCTION__);
  return ST_OK;
}

static enum status
port_set_duplex_xg (struct port *port, enum port_duplex duplex)
{
  DEBUG ("%s(): STUB!", __PRETTY_FUNCTION__);
  return ST_OK;
}

static enum status
port_update_sd_xg (struct port *port)
{
  DEBUG ("%s(): STUB!", __PRETTY_FUNCTION__);
  return ST_OK;
}

static enum status
port_shutdown_xg (struct port *port, int shutdown)
{
  DEBUG ("%s(): STUB!", __PRETTY_FUNCTION__);
  return ST_OK;
}

enum status
port_set_speed (port_id_t pid, const struct port_speed_arg *psa)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (psa->speed == PORT_SPEED_AUTO && !psa->speed_auto)
    return ST_BAD_VALUE;

  return port->set_speed (port, psa);
}

enum status
port_set_duplex (port_id_t pid, port_duplex_t duplex)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (duplex >= __PORT_DUPLEX_MAX)
    return ST_BAD_VALUE;

  return port->set_duplex (port, duplex);
}

enum status
port_dump_phy_reg (port_id_t pid, uint16_t page, uint16_t reg, uint16_t *val)
{
  GT_STATUS rc;
  GT_U16 pg;
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x16, &pg));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x16, page));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, reg, val));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x16, pg));
  if (rc != GT_OK)
    goto out;

 out:
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

static enum status
port_set_mdix_auto_fe (struct port *port, int mdix_auto)
{
  GT_STATUS rc;
  GT_U16 val;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x10, &val));
  if (rc != GT_OK)
    goto out;

  if (mdix_auto)
    val |= 3 << 4;
  else {
    val &= ~(1 << 5);
    val |= 1 << 4;
  }

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x10, val));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x00, &val));
  if (rc != GT_OK)
    goto out;

  val |= 1 << 15;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x00, val));

 out:
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

static enum status
port_set_mdix_auto_ge (struct port *port, int mdix_auto)
{
  GT_STATUS rc;
  GT_U16 val;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x10, &val));
  if (rc != GT_OK)
    goto out;

  if (mdix_auto)
    val |= 3 << 5;
  else {
    val &= ~(1 << 6);
    val |= 1 << 5;
  }

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x10, val));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x00, &val));
  if (rc != GT_OK)
    goto out;

  val |= 1 << 15;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x00, val));

 out:
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

static enum status
port_set_mdix_auto_xg (struct port *port, int mdix_auto)
{
  DEBUG ("%s(): STUB!", __PRETTY_FUNCTION__);
  return ST_OK;
}

enum status
port_set_mdix_auto (port_id_t pid, int mdix_auto)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  return port->set_mdix_auto (port, mdix_auto);
}

enum status
port_set_flow_control (port_id_t pid, flow_control_t fc)
{
  struct port *port = port_ptr (pid);
  CPSS_PORT_FLOW_CONTROL_ENT type;
  GT_BOOL aneg;
  GT_U16 val;
  GT_STATUS rc;


  if (!port)
    return ST_BAD_VALUE;

  switch (fc) {
  case FC_DESIRED:
    aneg = GT_TRUE;
    type = CPSS_PORT_FLOW_CONTROL_RX_TX_E;
    break;
  case FC_ON:
    aneg = GT_FALSE;
    type = CPSS_PORT_FLOW_CONTROL_RX_TX_E;
    break;
  case FC_OFF:
    aneg = GT_FALSE;
    type = CPSS_PORT_FLOW_CONTROL_DISABLE_E;
    break;
  default:
    return ST_BAD_VALUE;
  }

  rc = CRP (cpssDxChPortFlowCntrlAutoNegEnableSet
            (port->ldev, port->lport, aneg, GT_FALSE));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPortFlowControlEnableSet
            (port->ldev, port->lport, type));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x04, &val));
  if (rc != GT_OK)
    goto out;

  val |= 1 << 10;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x04, val));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x00, &val));
  if (rc != GT_OK)
    goto out;

  val |= 1 << 15;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x00, val));

 out:
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

enum status
port_get_stats (port_id_t pid, void *stats)
{
  GT_U8 ldev, lport;
  GT_STATUS rc;

  if (pid == CPU_PORT) {
    ldev = 0;
    lport = CPSS_CPU_PORT_NUM_CNS;
  } else  {
    struct port *port = port_ptr (pid);

    if (!port)
      return ST_BAD_VALUE;

    ldev = port->ldev;
    lport = port->lport;
  }

  rc = CRP (cpssDxChPortMacCountersOnPortGet (ldev, lport, stats));
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static uint64_t
max_bps (enum port_speed ps)
{
  switch (ps) {
  case PORT_SPEED_10:    return 10000000ULL;
  case PORT_SPEED_100:   return 100000000ULL;
  case PORT_SPEED_1000:  return 1000000000ULL;
  case PORT_SPEED_10000: return 10000000000ULL;
  case PORT_SPEED_12000: return 12000000000ULL;
  case PORT_SPEED_2500:  return 2500000000ULL;
  case PORT_SPEED_5000:  return 5000000000ULL;
  case PORT_SPEED_13600: return 13600000000ULL;
  case PORT_SPEED_20000: return 20000000000ULL;
  case PORT_SPEED_40000: return 40000000000ULL;
  case PORT_SPEED_16000: return 16000000000ULL;
  default:               return 0ULL;
  }
}

enum status
port_set_rate_limit (port_id_t pid, const struct rate_limit *rl)
{
  struct port *port = port_ptr (pid);
  uint32_t div;
  GT_STATUS rc;
  CPSS_DXCH_BRG_GEN_RATE_LIMIT_PORT_STC cfg = {
    .enableMcReg = GT_FALSE,
    .enableUcKnown = GT_FALSE
  };

  if (!port)
    return ST_BAD_VALUE;

  if (rl->type >= __TT_MAX)
    return ST_BAD_VALUE;

  if (rl->limit) {
    switch (rl->type) {
    case TT_UNICAST:
      cfg.enableBc = GT_TRUE;
      cfg.enableMc = GT_TRUE;
      cfg.enableUcUnk = GT_TRUE;
      break;

    case TT_MULTICAST:
      cfg.enableBc = GT_TRUE;
      cfg.enableMc = GT_TRUE;
      cfg.enableUcUnk = GT_FALSE;
      break;

    case TT_BROADCAST:
      cfg.enableBc = GT_TRUE;
      cfg.enableMc = GT_FALSE;
      cfg.enableUcUnk = GT_FALSE;
    }

    div = IS_XG_PORT (pid - 1) ? 5120 : 51200;
    cfg.rateLimit = (rl->limit / div) ? : 1;
  } else {
    cfg.enableBc = GT_FALSE;
    cfg.enableMc = GT_FALSE;
    cfg.enableUcUnk = GT_FALSE;
    cfg.rateLimit = 0;
  }

  rc = CRP (cpssDxChBrgGenPortRateLimitSet (port->ldev, port->lport, &cfg));
  switch (rc) {
  case GT_OK:           return ST_OK;
  case GT_HW_ERROR:     return ST_HW_ERROR;
  case GT_OUT_OF_RANGE: return ST_NOT_SUPPORTED;
  default:              return ST_HEX;
  }
}

enum status
port_set_bandwidth_limit (port_id_t pid, bps_t limit)
{
  struct port *port = port_ptr (pid);
  uint64_t max;
  GT_STATUS rc;
  GT_U32 rate, zero = 0;

  if (!port)
    return ST_BAD_VALUE;

  max = max_bps (port->max_speed);
  if (max == 0)
    return ST_HEX;

  if (limit > max)
    return ST_BAD_VALUE;

  rate = limit / 1000;

  if (limit == 0)
    rc = CRP (cpssDxChPortTxShaperEnableSet
              (port->ldev, port->lport, GT_FALSE));
  else {
    CRP (cpssDxChPortTxShaperProfileSet
         (port->ldev, port->lport, 1, &zero));
    rc = CRP (cpssDxChPortTxShaperProfileSet
              (port->ldev, port->lport, 1, &rate));
    ON_GT_ERROR (rc) goto out;

    rc = CRP (cpssDxChPortTxShaperEnableSet
              (port->ldev, port->lport, GT_TRUE));
  }

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

#if defined (VARIANT_FE)
static enum status
port_setup_ge (struct port *port)
{
  GT_U16 val;

  CRP (cpssDxChPhyPortSmiInterfaceSet
       (port->ldev, port->lport, CPSS_PHY_SMI_INTERFACE_1_E));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x10 + (port->lport - 24) * 2));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x6));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x14, 0x8205));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x4));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x1A, 0xB002));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x1B, 0x7C03));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x1));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x11 + (port->lport - 24) * 2));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x6));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x14, 0x8207));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x4));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0xFF));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x18, 0x2800));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x17, 0x2001));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x18, 0x1F70));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x17, 0x2004));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x3));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x10, 0x1AA7));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x0));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x1A, 0x9040));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x10 + (port->lport - 24) * 2));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x2));
  CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x10, &val));
  val &= ~(1 << 3);
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x10, val));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x0));
  CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x00, &val));
  val |= 1 << 15;
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, val));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x11 + (port->lport - 24) * 2));

  port_set_sgmii_mode (port);

  return ST_OK;
}
#elif defined (VARIANT_GE)
static enum status
port_setup_ge (struct port *port)
{
  int is_fiber;

  CRP (cpssDxChPhyPortSmiInterfaceSet
       (port->ldev, port->lport,
        (port->lport < 12)
        ? CPSS_PHY_SMI_INTERFACE_0_E
        : CPSS_PHY_SMI_INTERFACE_1_E));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x04 + (port->lport % 12)));

  switch (env_hw_subtype ()) {
  case HWST_ARLAN_3424GE_F:
  case HWST_ARLAN_3424GE_F_S:
    is_fiber = 1;
    break;
  case HWST_ARLAN_3424GE_U:
    is_fiber = port->id > 12;
    break;
  default:
    is_fiber = 0;
  }

  if (is_fiber) {
    DEBUG ("port %d is fiber\n", port->id);
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0006));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x14, 0x8202));
    usleep (10000);
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x00FD));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x11, 0x2299));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x00FF));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x18, 0x2800));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x00FF));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x17, 0x2001));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x1D, 0x0010));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x1E, 0x4018));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x1D, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x1D, 0x0003));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x1E, 0x0002));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x1D, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0003));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x11, 0x8840));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0001));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x04, 0x0061));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00, 0x9140));
  } else {
    DEBUG ("port %d is copper\n", port->id);
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 6));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0));
  }

  return ST_OK;
}
#endif /* VARIANT_* */

static void __attribute__ ((unused))
dump_xg_reg (const struct port *port, GT_U32 dev, GT_U32 reg)
{
  GT_U16 val;

  CRP (cpssXsmiPortGroupRegisterRead
       (port->ldev, 1, 0x18 + port->lport - 24, reg, dev, &val));
  DEBUG ("port %d (%d) reg %d.%04X: 0x%04X\n",
         port->id, port->lport, dev, reg, val);
}

static enum status
port_setup_xg (struct port *port)
{
  CRP (cpssDxChPortInterfaceModeSet
       (port->ldev, port->lport, CPSS_PORT_INTERFACE_MODE_XGMII_E));
  CRP (cpssDxChPortSpeedSet
       (port->ldev, port->lport, CPSS_PORT_SPEED_10000_E));
  CRP (cpssDxChPortSerdesPowerStatusSet
       (port->ldev, port->lport, CPSS_PORT_DIRECTION_BOTH_E, 0x0F, GT_TRUE));
  CRP (cpssXsmiPortGroupRegisterWrite
       (port->ldev, 1, 0x18 + port->lport - 24, 0xD70D, 3, 0x0020));
  /*
  dump_xg_reg (port, 1, 0xC319);
  dump_xg_reg (port, 1, 0xC31A);
  dump_xg_reg (port, 3, 0x0026);
  dump_xg_reg (port, 3, 0x0027);
  dump_xg_reg (port, 3, 0x0028);
  dump_xg_reg (port, 3, 0x0029);
  */
  return ST_OK;
}

static void
port_update_iso (void)
{
  int i;

  for (i = 0; i < nports; i++)
    port_set_iso_bmp (&ports[i]);
}

enum status
port_set_protected (port_id_t pid, bool_t protected)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (__port_set_prot (port, !!protected))
    port_update_iso ();

  return ST_OK;
}

enum status
port_set_comm (port_id_t pid, port_comm_t comm)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (__port_set_comm (port, comm))
    port_update_iso ();

  return ST_OK;
}

enum status
port_set_igmp_snoop (port_id_t pid, bool_t enable)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  rc = CRP (cpssDxChBrgGenIgmpSnoopEnable
            (port->ldev, port->lport, !!enable));
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
port_set_mru (uint16_t mru)
{
  int i;

  if (mru > 16382 || mru % 2)
    return ST_BAD_VALUE;

  for (i = 0; i < NPORTS; i++)
    CRP (cpssDxChPortMruSet (ports[i].ldev, ports[i].lport, mru));

  return ST_OK;
}

enum status
port_set_pve_dst (port_id_t spid, port_id_t dpid, int enable)
{
  struct port *src = port_ptr (spid);
  GT_STATUS rc;

  if (!src)
    return ST_BAD_VALUE;

  if (enable) {
    struct port *dst = port_ptr (dpid);

    if (!dst)
      return ST_BAD_VALUE;

    rc = CRP (cpssDxChBrgPrvEdgeVlanPortEnable
              (src->ldev, src->lport, !!enable,
               dst->lport, dst->ldev, GT_FALSE));
  } else {
    rc = CRP (cpssDxChBrgPrvEdgeVlanPortEnable
              (src->ldev, src->lport, !!enable, 0, 0, GT_FALSE));
  }

  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
port_tdr_test_start (port_id_t pid)
{
  struct port *port = port_ptr (pid);
  CPSS_VCT_CABLE_STATUS_STC st;
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  if (port->tdr_test_in_progress)
    return ST_NOT_READY;

  if (port->c_shutdown)
    return ST_BAD_STATE;

  rc = cpssVctCableStatusGet
    (port->ldev, port->lport, CPSS_VCT_START_E, &st);
  switch (rc) {
  case GT_OK:
    port->tdr_test_in_progress = 1;
    return ST_OK;
  case GT_HW_ERROR:
    return ST_HW_ERROR;
  default:
    CRP (rc);
    return ST_HEX;
  }
}

enum status
port_tdr_test_get_result (port_id_t pid, struct vct_cable_status *cs)
{
  struct port *port = port_ptr (pid);
  CPSS_VCT_CABLE_STATUS_STC st;
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  if (!port->tdr_test_in_progress)
    return ST_BAD_STATE;
  else if (port->c_shutdown)
    return ST_NOT_READY;

  rc = cpssVctCableStatusGet (port->ldev, port->lport,
                              CPSS_VCT_GET_RES_E, &st);
  switch (rc) {
  case GT_OK:
    port->tdr_test_in_progress = 0;
    data_encode_vct_cable_status (cs, &st, IS_FE_PORT (pid - 1));
    return ST_OK;
  case GT_NOT_READY:
    return ST_NOT_READY;
  case GT_HW_ERROR:
    return ST_HW_ERROR;
  default:
    CRP (rc);
    return ST_HEX;
  }
}

static void
__port_clear_translation (struct port *port)
{
  int i, trunk = port->mode == PM_TRUNK;

  for (i = 0; i < 4094; i++) {
    if (trunk && port->vlan_conf[i].xlate) {
      vid_t to = port->vlan_conf[i].map_to;

      if (port->vlan_conf[to - 1].refc) {
        vlans[to - 1].vt_refc -= port->vlan_conf[to - 1].refc;
        port->vlan_conf[to - 1].refc = 0;
        port_update_trunk_vlan (port, to);
      }

      port->vlan_conf[i].xlate  = 0;
      port->vlan_conf[i].map_to = 0;
    }
  }

  port->def_xlate  = 0;
  port->def_map_to = 0;
}

enum status
port_clear_translation (port_id_t pid)
{
  if (pid == ALL_PORTS) {
    int i;

    for (i = 0; i < nports; i++)
      __port_clear_translation (&ports[i]);
  } else {
    struct port *port = port_ptr (pid);

    if (!port)
      return ST_BAD_VALUE;

    __port_clear_translation (port);
  }

  pcl_port_clear_vt (pid);

  return ST_OK;
}

enum status
port_vlan_translate (port_id_t pid, vid_t from, vid_t to, int add)
{
  struct port *port = port_ptr (pid);
  int f_ix, t_ix;
  vid_t vids_to_upd[2];
  int n_to_upd = 0, i;
  int trunk;

  if (!port)
    return ST_BAD_VALUE;

  if (from != ALL_VLANS && !vlan_valid (from))
    return ST_BAD_VALUE;

  if (to != ALL_VLANS && !vlan_valid (to))
    return ST_BAD_VALUE;

  /* Default translation rules are allowed only when tunneling. */
  if (from == ALL_VLANS && to != ALL_VLANS && !vlan_xlate_tunnel)
    return ST_BAD_VALUE;

  f_ix = from - 1;

  if (add) {
    t_ix = to - 1;

    if (from) {
      /* Specific VLAN. */
      int map_to = port->vlan_conf[f_ix].map_to;

      if (map_to == to)
        return ST_OK;

      if (vlan_valid (map_to)) {
        --vlans[map_to - 1].vt_refc;
        --port->vlan_conf[map_to - 1].refc;
        vids_to_upd[n_to_upd++] = map_to;
      }

      port->vlan_conf[f_ix].xlate = 1;
      port->vlan_conf[f_ix].map_to = to;
      port->vlan_conf[t_ix].refc++;
      vlans[t_ix].vt_refc++;
      vids_to_upd[n_to_upd++] = to;
    } else {
      /* Default. */
      if (port->def_xlate) {
        if (port->def_map_to == to)
          return ST_OK;

        if (vlan_valid (port->def_map_to)) {
          vlans[port->def_map_to - 1].vt_refc--;
          port->vlan_conf[port->def_map_to - 1].refc--;
          vids_to_upd[n_to_upd++] = port->def_map_to;
        }
      }

      port->def_xlate = 1;
      port->def_map_to = to;
      if (vlan_valid (to)) {
        port->vlan_conf[t_ix].refc++;
        vlans[t_ix].vt_refc++;
        vids_to_upd[n_to_upd++] = to;
      }
    }
  } else {
    /* Disable translation. */
    if (from && port->vlan_conf[f_ix].xlate) {
      /* Specific VLAN. */
      to = port->vlan_conf[f_ix].map_to;
      t_ix = to - 1;
      port->vlan_conf[f_ix].xlate = 0;
      port->vlan_conf[f_ix].map_to = 0;
      port->vlan_conf[t_ix].refc--;
      vlans[t_ix].vt_refc--;
      vids_to_upd[n_to_upd++] = to;
    } else if (port->def_xlate) {
      /* Default. */
      if (port->def_map_to) {
        port->vlan_conf[port->def_map_to - 1].refc--;
        vlans[port->def_map_to - 1].vt_refc--;
        vids_to_upd[n_to_upd++] = port->def_map_to;
      }
      port->def_xlate = 0;
      port->def_map_to = 0;
    } else {
      /* Nothing to do. */
      return ST_OK;
    }
  }

  DEBUG ("we're here\r\n");

  /* Now apply changes if needed. */
  trunk = port->mode == PM_TRUNK;

  if (add) {
    enum status r = pcl_setup_vt (pid, from, to, vlan_xlate_tunnel, trunk);
    DEBUG ("PCL returned %d\r\n", r);
  } else
    pcl_remove_vt (pid, from, vlan_xlate_tunnel);

  if (trunk)
    for (i = 0; i < n_to_upd; i++)
      port_update_trunk_vlan_all_ports (vids_to_upd[i]);

  return ST_OK;
}

enum status
port_set_trunk_vlans (port_id_t pid, const uint8_t *bmp)
{
  struct port *port = port_ptr (pid);
  int i, trunk, allow;

  if (!port)
    return ST_BAD_VALUE;

  trunk = port->mode == PM_TRUNK;
  for (i = 1; i < 4095; i++) {
    allow = !!(bmp[i / 8] & (1 << (7 - (i % 8))));
    if (port->vlan_conf[i - 1].tallow != allow) {
      port->vlan_conf[i - 1].tallow = allow;
      if (trunk)
        port_update_trunk_vlan (port, i);
    }
  }

  return ST_OK;
}
