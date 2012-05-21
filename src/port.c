#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <presteramgr.h>
#include <debug.h>
#include <sysdeps.h>
#include <port.h>
#include <control.h>
#include <data.h>
#include <vlan.h>
#include <qos.h>
#include <utils.h>

#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortStat.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortTx.h>
#include <cpss/dxCh/dxChxGen/phy/cpssDxChPhySmi.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgStp.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgEgrFlt.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgGen.h>
#include <cpss/dxCh/dxChxGen/cos/cpssDxChCos.h>
#include <cpss/dxCh/dxChxGen/cscd/cpssDxChCscd.h>
#include <cpss/dxCh/dxChxGen/nst/cpssDxChNstPortIsolation.h>
#include <cpss/generic/cscd/cpssGenCscd.h>
#include <cpss/generic/port/cpssPortTx.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>


DECLSHOW (CPSS_PORT_SPEED_ENT);
DECLSHOW (CPSS_PORT_DUPLEX_ENT);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

struct port *ports = NULL;
int nports = 0;

static port_id_t *port_ids;

static enum status port_set_speed_fe (struct port *, const struct port_speed_arg *);
static enum status port_set_duplex_fe (struct port *, enum port_duplex);
static enum status port_shutdown_fe (struct port *, int);
static enum status port_set_mdix_auto_fe (struct port *, int);
static enum status port_setup_fe (struct port *);
static enum status port_set_speed_ge (struct port *, const struct port_speed_arg *);
static enum status port_set_duplex_ge (struct port *, enum port_duplex);
static enum status port_shutdown_ge (struct port *, int);
static enum status port_set_mdix_auto_ge (struct port *, int);
static enum status port_setup_ge (struct port *);

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
  assert (ldev < NDEVS);
  assert (lport < CPSS_MAX_PORTS_NUM_CNS);
  assert (port_ids);
  return port_ids[ldev * CPSS_MAX_PORTS_NUM_CNS + lport];
}

static void
port_set_vid (struct port *port)
{
  vid_t vid = 0; /* Make the compiler happy. */

  switch (port->mode) {
  case PM_ACCESS: vid = port->access_vid; break;
  case PM_TRUNK:  vid = port->native_vid; break;
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
  int i;
  int pmap[NPORTS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    27, 26, 25, 24
  };

  port_ids = calloc (NDEVS * CPSS_MAX_PORTS_NUM_CNS, sizeof (port_id_t));
  assert (port_ids);

  ports = calloc (NPORTS, sizeof (struct port));
  assert (ports);
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
    ports[i].c_protected = 0;
    if (i < 24) {
      ports[i].max_speed = PORT_SPEED_100;
      ports[i].set_speed = port_set_speed_fe;
      ports[i].set_duplex = port_set_duplex_fe;
      ports[i].shutdown = port_shutdown_fe;
      ports[i].set_mdix_auto = port_set_mdix_auto_fe;
      ports[i].setup = port_setup_fe;
    } else {
      ports[i].max_speed = PORT_SPEED_1000;
      ports[i].set_speed = port_set_speed_ge;
      ports[i].set_duplex = port_set_duplex_ge;
      ports[i].shutdown = port_shutdown_ge;
      ports[i].set_mdix_auto = port_set_mdix_auto_ge;
      ports[i].setup = port_setup_ge;
    }
    port_ids[ports[i].ldev * CPSS_MAX_PORTS_NUM_CNS + ports[i].lport] =
      ports[i].id;
  }

  nports = NPORTS;

  return 0;
}

static enum status
port_update_isolation (const struct port *port)
{
  CPSS_INTERFACE_INFO_STC t_iface = {
    .type = CPSS_INTERFACE_PORT_E,
    .devPort = {
      .devNum = port->ldev,
      .portNum = port->lport
    }
  };
  GT_STATUS (*isolation_op)
    (GT_U8,
     CPSS_DXCH_NST_PORT_ISOLATION_TRAFFIC_TYPE_ENT,
     CPSS_INTERFACE_INFO_STC,
     GT_U8) = port->c_protected
    ? cpssDxChNstPortIsolationPortDelete
    : cpssDxChNstPortIsolationPortAdd;
  int i;

  for (i = 0; i < nports; i++) {
    CPSS_INTERFACE_INFO_STC o_iface = {
      .type = CPSS_INTERFACE_PORT_E,
      .devPort = {
        .devNum = ports[i].ldev,
        .portNum = ports[i].lport
      }
    };

    if (ports[i].id == port->id || !ports[i].c_protected)
      continue;

    CRP (isolation_op
         (port->ldev,
          CPSS_DXCH_NST_PORT_ISOLATION_TRAFFIC_TYPE_L2_E,
          o_iface,
          port->lport));
    CRP (isolation_op
         (ports[i].ldev,
          CPSS_DXCH_NST_PORT_ISOLATION_TRAFFIC_TYPE_L2_E,
          t_iface,
          ports[i].lport));
  }

  return ST_OK;
}

static enum status
port_setup_isolation (void)
{
  int i;
  CPSS_PORTS_BMP_STC pbmp;

  CRP (cpssDxChNstPortIsolationEnableSet (0, GT_TRUE));

  memset (&pbmp, 0, sizeof (pbmp));
  for (i = 0; i < nports; i++)
    CPSS_PORTS_BMP_PORT_SET_MAC (&pbmp, ports[i].lport);

  for (i = 0; i < nports; i++) {
    CPSS_INTERFACE_INFO_STC iface = {
      .type = CPSS_INTERFACE_PORT_E,
      .devPort = {
        .devNum = ports[i].ldev,
        .portNum = ports[i].lport
      }
    };

    CRP (cpssDxChNstPortIsolationTableEntrySet
         (ports[i].ldev,
          CPSS_DXCH_NST_PORT_ISOLATION_TRAFFIC_TYPE_L2_E,
          iface,
          GT_TRUE,
          &pbmp));
  }

  return ST_OK;
}

enum status
port_start (void)
{
  int i;

  CRP (cpssDxChPhyAutoPollNumOfPortsSet
       (0, CPSS_DXCH_PHY_SMI_AUTO_POLL_NUM_OF_PORTS_16_E,
        CPSS_DXCH_PHY_SMI_AUTO_POLL_NUM_OF_PORTS_8_E));

  for (i = 0; i < nports; i++) {
    struct port *port = &ports[i];

    port->setup (port);
    port_set_vid (port);
    port_update_qos_trust (port);
    port_setup_stats (port->ldev, port->lport);
    CRP (cpssDxChBrgGenPortIeeeReservedMcastProfileIndexSet
         (port->ldev, port->lport, 0));
#ifdef PRESTERAMGR_FUTURE_LION
    CRP (cpssDxChPortTxShaperModeSet
         (ports->ldev, port->lport,
          CPSS_PORT_TX_DROP_SHAPER_BYTE_MODE_E));
#endif /* PRESTERAMGR_FUTURE_LION */
  }

  port_setup_isolation ();
  port_setup_stats (0, CPSS_CPU_PORT_NUM_CNS);
  CRP (cpssDxChCscdPortTypeSet
       (0, CPSS_CPU_PORT_NUM_CNS,
        CPSS_CSCD_PORT_DSA_MODE_EXTEND_E));

  for (i = 0; i < nports; i++) {
    struct port *port = &ports[i];

    CRP (cpssDxChBrgStpStateSet
         (port->ldev, port->lport, 0, CPSS_STP_BLCK_LSTN_E));
    CRP (cpssDxChPortEnableSet (port->ldev, port->lport, GT_TRUE));
  };
  CRP (cpssDxChPortEnableSet (0, CPSS_CPU_PORT_NUM_CNS, GT_TRUE));

  return ST_OK;
}

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

int
port_exists (GT_U8 dev, GT_U8 port)
{
  return CPSS_PORTS_BMP_IS_PORT_SET_MAC
    (&(PRV_CPSS_PP_MAC (dev)->existingPorts), port);
}

void
port_handle_link_change (GT_U8 ldev, GT_U8 lport)
{
  CPSS_PORT_ATTRIBUTES_STC attrs;
  GT_STATUS rc;
  port_id_t pid = port_id (ldev, lport);
  struct port *port = port_ptr (pid);

  if (!port)
    return;

  rc = CRP (cpssDxChPortAttributesOnPortGet (port->ldev, port->lport, &attrs));
  if (rc != GT_OK)
    return;

  port_lock ();

  if (attrs.portLinkUp    != port->state.attrs.portLinkUp ||
      attrs.portSpeed     != port->state.attrs.portSpeed  ||
      attrs.portDuplexity != port->state.attrs.portDuplexity) {
    control_notify_port_state (pid, &attrs);
    port->state.attrs = attrs;
#ifdef DEBUG_STATE
    if (attrs.portLinkUp)
      osPrintSync ("port %2d link up at %s, %s\n", n,
                   SHOW (CPSS_PORT_SPEED_ENT, attrs.portSpeed),
                   SHOW (CPSS_PORT_DUPLEX_ENT, attrs.portDuplexity));
    else
      osPrintSync ("port %2d link down\n", n);
#endif /* DEBUG_STATE */
  }

  port_unlock ();
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
                    enum port_stp_state state)
{
  CPSS_STP_STATE_ENT cs;
  GT_STATUS rc;
  enum status result;
  struct port *port;

  if (!(port = port_ptr (pid)) || stp_id > 255)
    return ST_BAD_VALUE;

  result = data_decode_stp_state (&cs, state);
  if (result != ST_OK)
    return result;

  rc = CRP (cpssDxChBrgStpStateSet (port->ldev, port->lport, stp_id, cs));
  switch (rc) {
  case GT_OK:                    return ST_OK;
  case GT_HW_ERROR:              return ST_HW_ERROR;
  case GT_BAD_PARAM:             return ST_BAD_VALUE;
  case GT_NOT_APPLICABLE_DEVICE: return ST_NOT_SUPPORTED;
  default:                       return ST_HEX;
  }
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
  }

  port->access_vid = vid;

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
port_set_native_vid (port_id_t pid, vid_t vid)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc = GT_OK;

  if (!(port && vlan_valid (vid)))
    return ST_BAD_VALUE;

  if (port->mode == PM_TRUNK) {
    GT_BOOL tag_native;
    CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT cmd;

    if (vlan_dot1q_tag_native) {
      tag_native = GT_TRUE;
      cmd = CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
    } else {
      tag_native = GT_FALSE;
      cmd = CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
    }

    rc = CRP (cpssDxChBrgVlanMemberSet
              (port->ldev,
               port->native_vid,
               port->lport,
               GT_TRUE,
               GT_TRUE,
               CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChBrgVlanMemberSet
              (port->ldev,
               vid,
               port->lport,
               GT_TRUE,
               tag_native,
               cmd));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
    if (rc != GT_OK)
      goto out;
  }

  port->native_vid = vid;

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
                   GT_BOOL rest_member,
                   GT_BOOL rest_tag,
                   CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT rest_tag_cmd)

{
  GT_STATUS rc;
  int i;

  for (i = 0; i < 4095; i++) {
    if (vlans[i].state == VS_DELETED || vid == i + 1)
      continue;

    rc = CRP (cpssDxChBrgVlanMemberSet
              (port->ldev,
               i + 1,
               port->lport,
               rest_member,
               rest_tag,
               rest_tag_cmd));
    if (rc != GT_OK)
      goto out;
  }

  rc = CRP (cpssDxChBrgVlanMemberSet
            (port->ldev,
             vid,
             port->lport,
             GT_TRUE,
             vid_tag,
             vid_tag_cmd));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChBrgVlanPortVidSet
            (port->ldev,
             port->lport,
             vid));
 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_set_trunk_mode (struct port *port)
{
  GT_BOOL tag;
  CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT cmd;

  if (vlan_dot1q_tag_native) {
    tag = GT_TRUE;
    cmd = CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
  } else {
    tag = GT_FALSE;
    cmd = CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
  }

  return port_vlan_bulk_op (port,
                            port->native_vid,
                            tag,
                            cmd,
                            GT_TRUE,
                            GT_TRUE,
                            CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E);
}

static enum status
port_set_access_mode (struct port *port)
{
  return port_vlan_bulk_op (port,
                            port->access_vid,
                            GT_FALSE,
                            CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E,
                            GT_FALSE,
                            GT_FALSE,
                            CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E);
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

  default:
    result = ST_BAD_VALUE;
  }

  if (result == ST_OK)
    port->mode = mode;

  return result;
}

enum status
port_shutdown (port_id_t pid, int shutdown)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

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
port_update_sd_ge (const struct port *port)
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
port_update_sd_fe (const struct port *port)
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
port_dump_phy_reg (port_id_t pid, uint16_t reg)
{
  GT_STATUS rc;
  GT_U16 val;
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, reg, &val));
  if (rc != GT_OK)
    goto out;

  fprintf (stderr, "%04X\r\n", val);

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

    cfg.rateLimit = (rl->limit / 512000) ? : 1;
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
  GT_U32 rate;

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
    rc = CRP (cpssDxChPortTxShaperProfileSet
              (port->ldev, port->lport, 1, &rate));
    ON_GT_ERROR (rc, goto out);

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

enum status
port_set_protected (port_id_t pid, bool_t protected)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  protected = !!protected;
  if (port->c_protected == protected)
    return ST_OK;

  port->c_protected = protected;
  return port_update_isolation (port);
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
