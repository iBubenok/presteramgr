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

#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgStp.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>


DECLSHOW (CPSS_PORT_SPEED_ENT);
DECLSHOW (CPSS_PORT_DUPLEX_ENT);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

struct port *ports = NULL;
int nports = 0;

static int *port_nums;

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
port_num (GT_U8 ldev, GT_U8 lport)
{
  assert (ldev < NDEVS);
  assert (lport < CPSS_MAX_PORTS_NUM_CNS);
  assert (port_nums);
  return port_nums[ldev * CPSS_MAX_PORTS_NUM_CNS + lport];
}

static void
port_set_vid (port_num_t n)
{
  vid_t vid = 0; /* Make the compiler happy. */
  struct port *port;

  port = &ports[n];

  switch (port->mode) {
  case PM_ACCESS: vid = port->access_vid; break;
  case PM_TRUNK:  vid = port->native_vid; break;
  }
  CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
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

  port_nums = malloc (NDEVS * CPSS_MAX_PORTS_NUM_CNS * sizeof (int));
  assert (port_nums);
  for (i = 0; i < NDEVS * CPSS_MAX_PORTS_NUM_CNS; i++)
    port_nums[i] = -1;

  ports = calloc (NPORTS, sizeof (struct port));
  assert (ports);
  for (i = 0; i < NPORTS; i++) {
    ports[i].ldev = 0;
    ports[i].lport = pmap[i];
    ports[i].mode = PM_ACCESS;
    ports[i].access_vid = 1;
    ports[i].native_vid = 1;
    port_nums[ports[i].ldev * CPSS_MAX_PORTS_NUM_CNS + ports[i].lport] = i;

    port_set_vid (i);
  }

  nports = NPORTS;

  return 0;
}

GT_STATUS
port_set_sgmii_mode (int n)
{
  GT_U8 dev, port;

  assert (port_valid (n));
  dev = ports[n].ldev;
  port = ports[n].lport;

  CRPR (cpssDxChPortInterfaceModeSet
        (dev, port, CPSS_PORT_INTERFACE_MODE_SGMII_E));
  CRPR (cpssDxChPortSpeedSet (dev, port, CPSS_PORT_SPEED_1000_E));
  CRPR (cpssDxChPortSerdesPowerStatusSet
        (dev, port, CPSS_PORT_DIRECTION_BOTH_E, 0x01, GT_TRUE));
  CRPR (cpssDxChPortInbandAutoNegEnableSet (dev, port, GT_TRUE));

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
  int p;
  struct port *port;

  if ((p = port_num (ldev, lport)) < 0)
    return;
  port = &ports[p];

  rc = CRP (cpssDxChPortAttributesOnPortGet (ldev, lport, &attrs));
  if (rc != GT_OK)
    return;

  port_lock ();

  if (attrs.portLinkUp    != port->state.attrs.portLinkUp ||
      attrs.portSpeed     != port->state.attrs.portSpeed  ||
      attrs.portDuplexity != port->state.attrs.portDuplexity) {
    control_notify_port_state (p, &attrs);
    port->state.attrs = attrs;
#ifdef DEBUG_STATE
    if (attrs.portLinkUp)
      osPrintSync ("port %2d link up at %s, %s\n", p,
                   SHOW (CPSS_PORT_SPEED_ENT, attrs.portSpeed),
                   SHOW (CPSS_PORT_DUPLEX_ENT, attrs.portDuplexity));
    else
      osPrintSync ("port %2d link down\n", p);
#endif /* DEBUG_STATE */
  }

  port_unlock ();
}

enum status
port_get_state (port_num_t port, struct port_link_state *state)
{
  enum status result = ST_BAD_VALUE;

  port_lock ();

  if (!port_valid (port))
    goto out;

  data_encode_port_state (state, &ports[port].state.attrs);
  result = ST_OK;

 out:
  port_unlock ();
  return result;
}

enum status
port_set_stp_state (port_num_t port, stp_id_t stp_id,
                    enum port_stp_state state)
{
  CPSS_STP_STATE_ENT cs;
  GT_STATUS rc;
  enum status result;

  if (!port_valid (port) || stp_id > 255)
    return ST_BAD_VALUE;

  result = data_decode_stp_state (&cs, state);
  if (result != ST_OK)
    return result;

  rc = CRP (cpssDxChBrgStpStateSet (ports[port].ldev, ports[port].lport,
                                    stp_id, cs));
  switch (rc) {
  case GT_OK:
    return ST_OK;

  case GT_HW_ERROR:
    return ST_HW_ERROR;

  case GT_BAD_PARAM:
    return ST_BAD_VALUE;

  case GT_NOT_APPLICABLE_DEVICE:
    return ST_NOT_SUPPORTED;

  default:
    return ST_HEX;
  }
}


enum status
port_set_access_vid (port_num_t n, vid_t vid)
{
  struct port *port;
  GT_STATUS rc = GT_OK;

  if (!(port_valid (n) && vlan_valid (vid)))
    return ST_BAD_VALUE;

  port = &ports[n];

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
port_set_native_vid (port_num_t n, vid_t vid)
{
  struct port *port;
  GT_STATUS rc = GT_OK;

  if (!(port_valid (n) && vlan_valid (vid)))
    return ST_BAD_VALUE;

  port = &ports[n];

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
port_set_mode (port_num_t n, enum port_mode mode)
{
  struct port *port;
  enum status result;

  if (!port_valid (n))
    return ST_BAD_VALUE;

  port = &ports[n];
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
port_shutdown (port_num_t n, int shutdown)
{
  struct port *port;
  GT_STATUS rc;

  if (!port_valid (n))
    return ST_BAD_VALUE;

  port = &ports[n];

  rc = CRP (cpssDxChPortEnableSet (port->ldev, port->lport, !shutdown));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPortForceLinkDownEnableSet
            (port->ldev,
             port->lport,
             !!shutdown));

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}
