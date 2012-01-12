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

#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortStat.h>
#include <cpss/dxCh/dxChxGen/phy/cpssDxChPhySmi.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgStp.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgEgrFlt.h>
#include <cpss/dxCh/dxChxGen/cos/cpssDxChCos.h>
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
static enum status port_set_speed_ge (struct port *, const struct port_speed_arg *);
static enum status port_set_duplex_ge (struct port *, enum port_duplex);
static enum status port_shutdown_ge (struct port *, int);
static enum status port_set_mdix_auto_ge (struct port *, int);

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
    if (i < 24) {
      ports[i].set_speed = port_set_speed_fe;
      ports[i].set_duplex = port_set_duplex_fe;
      ports[i].shutdown = port_shutdown_fe;
      ports[i].set_mdix_auto = port_set_mdix_auto_fe;
    } else {
      ports[i].set_speed = port_set_speed_ge;
      ports[i].set_duplex = port_set_duplex_ge;
      ports[i].shutdown = port_shutdown_ge;
      ports[i].set_mdix_auto = port_set_mdix_auto_ge;
    }
    port_ids[ports[i].ldev * CPSS_MAX_PORTS_NUM_CNS + ports[i].lport] = i + 1;

    port_set_vid (&ports[i]);
    port_update_qos_trust (&ports[i]);
    port_setup_stats (ports[i].ldev, ports[i].lport);
  }

  port_setup_stats (0, CPSS_CPU_PORT_NUM_CNS);

  nports = NPORTS;

  return 0;
}

GT_STATUS
port_set_sgmii_mode (port_id_t pid)
{
  struct port *port = port_ptr (pid);

  assert (port);

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
port_shutdown_fe (struct port *port, int shutdown)
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
