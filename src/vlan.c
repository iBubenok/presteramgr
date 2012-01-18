#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>

#include <presteramgr.h>
#include <debug.h>
#include <string.h>
#include <vlan.h>
#include <port.h>
#include <pdsa.h>
#include <control-proto.h>

struct vlan vlans[4095];

DECLSHOW (GT_BOOL);
DECLSHOW (CPSS_PACKET_CMD_ENT);
DECLSHOW (CPSS_DXCH_BRG_IP_CTRL_TYPE_ENT);
DECLSHOW (CPSS_BRG_IPM_MODE_ENT);
DECLSHOW (CPSS_IP_SITE_ID_ENT);
DECLSHOW (CPSS_DXCH_BRG_VLAN_FLOOD_VIDX_MODE_ENT);
DECLSHOW (CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_CMD_ENT);
DECLSHOW (CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT);

static void
vlan_print_info (GT_U16 vid, const CPSS_DXCH_BRG_VLAN_INFO_STC *info)
{
  osPrintSync
    ("VLAN %d\r\n"
     "  unkSrcAddrSecBreach   = %s\r\n"
     "  unregNonIpMcastCmd    = %s\r\n"
     "  unregIpv4McastCmd     = %s\r\n"
     "  unregIpv6McastCmd     = %s\r\n"
     "  unkUcastCmd           = %s\r\n"
     "  unregIpv4BcastCmd     = %s\r\n"
     "  unregNonIpv4BcastCmd  = %s\r\n"
     "  ipv4IgmpToCpuEn       = %s\r\n"
     "  mirrToRxAnalyzerEn    = %s\r\n"
     "  ipv6IcmpToCpuEn       = %s\r\n"
     "  ipCtrlToCpuEn         = %s\r\n"
     "  ipv4IpmBrgMode        = %s\r\n"
     "  ipv6IpmBrgMode        = %s\r\n"
     "  ipv4IpmBrgEn          = %s\r\n"
     "  ipv6IpmBrgEn          = %s\r\n"
     "  ipv6SiteIdMode        = %s\r\n"
     "  ipv4UcastRouteEn      = %s\r\n"
     "  ipv4McastRouteEn      = %s\r\n"
     "  ipv6UcastRouteEn      = %s\r\n"
     "  ipv6McastRouteEn      = %s\r\n"
     "  stgId                 = %d\r\n"
     "  autoLearnDisable      = %s\r\n"
     "  naMsgToCpuEn          = %s\r\n"
     "  mruIdx                = %d\r\n"
     "  bcastUdpTrapMirrEn    = %s\r\n"
     "  vrfId                 = %d\r\n"
     "  floodVidx             = %d\r\n"
     "  floodVidxMode         = %s\r\n"
     "  portIsolationMode     = %s\r\n"
     "  ucastLocalSwitchingEn = %s\r\n"
     "  mcastLocalSwitchingEn = %s\r\n",
     vid,
     SHOW (GT_BOOL, info->unkSrcAddrSecBreach),
     SHOW (CPSS_PACKET_CMD_ENT, info->unregNonIpMcastCmd),
     SHOW (CPSS_PACKET_CMD_ENT, info->unregIpv4McastCmd),
     SHOW (CPSS_PACKET_CMD_ENT, info->unregIpv6McastCmd),
     SHOW (CPSS_PACKET_CMD_ENT, info->unkUcastCmd),
     SHOW (CPSS_PACKET_CMD_ENT, info->unregIpv4BcastCmd),
     SHOW (CPSS_PACKET_CMD_ENT, info->unregNonIpv4BcastCmd),
     SHOW (GT_BOOL, info->ipv4IgmpToCpuEn),
     SHOW (GT_BOOL, info->mirrToRxAnalyzerEn),
     SHOW (GT_BOOL, info->ipv6IcmpToCpuEn),
     SHOW (CPSS_DXCH_BRG_IP_CTRL_TYPE_ENT, info->ipCtrlToCpuEn),
     SHOW (CPSS_BRG_IPM_MODE_ENT, info->ipv4IpmBrgMode),
     SHOW (CPSS_BRG_IPM_MODE_ENT, info->ipv6IpmBrgMode),
     SHOW (GT_BOOL, info->ipv4IpmBrgEn),
     SHOW (GT_BOOL, info->ipv6IpmBrgEn),
     SHOW (CPSS_IP_SITE_ID_ENT, info->ipv6SiteIdMode),
     SHOW (GT_BOOL, info->ipv4UcastRouteEn),
     SHOW (GT_BOOL, info->ipv4McastRouteEn),
     SHOW (GT_BOOL, info->ipv6UcastRouteEn),
     SHOW (GT_BOOL, info->ipv6McastRouteEn),
     info->stgId,
     SHOW (GT_BOOL, info->autoLearnDisable),
     SHOW (GT_BOOL, info->naMsgToCpuEn),
     info->mruIdx,
     SHOW (GT_BOOL, info->bcastUdpTrapMirrEn),
     info->vrfId,
     info->floodVidx,
     SHOW (CPSS_DXCH_BRG_VLAN_FLOOD_VIDX_MODE_ENT, info->floodVidxMode),
     SHOW (CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_CMD_ENT, info->portIsolationMode),
     SHOW (GT_BOOL, info->ucastLocalSwitchingEn),
     SHOW (GT_BOOL, info->mcastLocalSwitchingEn));
}

static void
print_port_bmp (const CPSS_PORTS_BMP_STC *ports)
{
  int i;

  for (i = 0; i < CPSS_MAX_PORTS_NUM_CNS; ++i)
    if (CPSS_PORTS_BMP_IS_PORT_SET_MAC (ports, i))
      osPrintSync ("%d ", i);
}

enum status
vlan_dump (vid_t vid)
{
  CPSS_PORTS_BMP_STC members, tagging = { .ports = {0, 0} };
  CPSS_DXCH_BRG_VLAN_INFO_STC vlan_info;
  CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC tagging_cmd;
  GT_BOOL valid;
  GT_STATUS result;
  int i;

  result = CRP (cpssDxChBrgVlanEntryRead (0, vid, &members, &tagging, &vlan_info, &valid, &tagging_cmd));
  if (result != GT_OK)
    return ST_HEX; /* FIXME: return meaningful values. */

  vlan_print_info (vid, &vlan_info);
  osPrintSync ("\r\n  members: ");
  print_port_bmp (&members);
  osPrintSync ("\r\n  tagging: ");
  print_port_bmp (&tagging);
  osPrintSync ("\r\n  valid:   %s", SHOW (GT_BOOL, valid));
  osPrintSync ("\r\n  tagging cmd:\r\n");
  for (i = 0; i < CPSS_MAX_PORTS_NUM_CNS; ++i)
    osPrintSync ("    %2d: %s\r\n", i,
                 SHOW (CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT,
                       tagging_cmd.portsCmd[i]));

  return ST_OK;
}

int vlan_dot1q_tag_native = 0;

static void
setup_tagging (vid_t vid,
               CPSS_PORTS_BMP_STC *members,
               CPSS_PORTS_BMP_STC *tagging,
               CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC *tagging_cmd)
{
  int i;

  memset (members, 0, sizeof (*members));
  memset (tagging, 0, sizeof (*tagging));
  memset (tagging_cmd, 0, sizeof (*tagging_cmd));

  for (i = 0; i < nports; i++) {
    struct port *port = port_ptr (i + 1);

    switch (port->mode) {
    case PM_ACCESS:
      if (port->access_vid == vid) {
        CPSS_PORTS_BMP_PORT_SET_MAC (members, i);
        tagging_cmd->portsCmd[i] = CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
      }
      break;

    case PM_TRUNK:
      CPSS_PORTS_BMP_PORT_SET_MAC (members, i);
      if (port->native_vid != vid || vlan_dot1q_tag_native) {
        CPSS_PORTS_BMP_PORT_SET_MAC (tagging, i);
        tagging_cmd->portsCmd[i] = CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
      } else
        tagging_cmd->portsCmd[i] = CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
    }
  }
}

enum status
vlan_add (vid_t vid)
{
  CPSS_PORTS_BMP_STC members, tagging;
  CPSS_DXCH_BRG_VLAN_INFO_STC vlan_info;
  CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC tagging_cmd;
  GT_STATUS rc;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  memset (&vlan_info, 0, sizeof (vlan_info));
  vlan_info.unkSrcAddrSecBreach   = GT_FALSE;
  vlan_info.unregNonIpMcastCmd    = CPSS_PACKET_CMD_FORWARD_E;
  vlan_info.unregIpv4McastCmd     = CPSS_PACKET_CMD_FORWARD_E;
  vlan_info.unregIpv6McastCmd     = CPSS_PACKET_CMD_FORWARD_E;
  vlan_info.unkUcastCmd           = CPSS_PACKET_CMD_FORWARD_E;
  vlan_info.unregIpv4BcastCmd     = CPSS_PACKET_CMD_FORWARD_E;
  vlan_info.unregNonIpv4BcastCmd  = CPSS_PACKET_CMD_FORWARD_E;
  vlan_info.ipv4IgmpToCpuEn       = GT_FALSE;
  vlan_info.mirrToRxAnalyzerEn    = GT_FALSE;
  vlan_info.ipv6IcmpToCpuEn       = GT_FALSE;
  vlan_info.ipCtrlToCpuEn         = CPSS_DXCH_BRG_IP_CTRL_NONE_E;
  vlan_info.ipv4IpmBrgMode        = CPSS_BRG_IPM_SGV_E;
  vlan_info.ipv6IpmBrgMode        = CPSS_BRG_IPM_SGV_E;
  vlan_info.ipv4IpmBrgEn          = GT_FALSE;
  vlan_info.ipv6IpmBrgEn          = GT_FALSE;
  vlan_info.ipv6SiteIdMode        = CPSS_IP_SITE_ID_INTERNAL_E;
  vlan_info.ipv4UcastRouteEn      = GT_FALSE;
  vlan_info.ipv4McastRouteEn      = GT_FALSE;
  vlan_info.ipv6UcastRouteEn      = GT_FALSE;
  vlan_info.ipv6McastRouteEn      = GT_FALSE;
  vlan_info.stgId                 = 0;
  vlan_info.autoLearnDisable      = GT_FALSE;
  vlan_info.naMsgToCpuEn          = GT_TRUE;
  vlan_info.mruIdx                = 0;
  vlan_info.bcastUdpTrapMirrEn    = GT_FALSE;
  vlan_info.vrfId                 = 0;
  vlan_info.floodVidx             = 4095;
  vlan_info.floodVidxMode         = CPSS_DXCH_BRG_VLAN_FLOOD_VIDX_MODE_UNREG_MC_E;
  vlan_info.portIsolationMode     = CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_L2_CMD_E;
  vlan_info.ucastLocalSwitchingEn = GT_FALSE;
  vlan_info.mcastLocalSwitchingEn = GT_FALSE;

  setup_tagging (vid, &members, &tagging, &tagging_cmd);

  rc = CRP (cpssDxChBrgVlanEntryWrite (0, vid, &members,
                                       &tagging, &vlan_info,
                                       &tagging_cmd));
  switch (rc) {
  case GT_OK:
    vlans[vid - 1].state = VS_ACTIVE;
    return ST_OK;
  case GT_HW_ERROR:
    return ST_HW_ERROR;
  default:
    return ST_HEX;
  }
}

enum status
vlan_delete (vid_t vid)
{
  GT_STATUS rc;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  rc = CRP (cpssDxChBrgVlanEntryInvalidate (0, vid));
  switch (rc) {
  case GT_OK:
    vlans[vid - 1].state = VS_DELETED;
    return ST_OK;
  default:
    return ST_HEX;
  }
}

int
vlan_init (void)
{
  GT_STATUS rc;
  int i;

  for (i = 0; i < 4095; i++)
    vlans[i].state = VS_DELETED;

  rc = CRP (cpssDxChBrgVlanBridgingModeSet (0, CPSS_BRG_MODE_802_1Q_E));
  vlan_add (1);

  return rc != GT_OK;
}

GT_STATUS
vlan_set_mac_addr (GT_U16 vid, const unsigned char *addr)
{
  CPSS_MAC_ENTRY_EXT_STC mac_entry;
  GT_STATUS rc;

  rc = CRP (cpssDxChBrgVlanIpCntlToCpuSet
            (0, vid, CPSS_DXCH_BRG_IP_CTRL_IPV4_IPV6_E));
  if (rc != GT_OK)
    return rc;

  memset (&mac_entry, 0, sizeof (mac_entry));
  mac_entry.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  mac_entry.key.key.macVlan.vlanId = vid;
  memcpy (mac_entry.key.key.macVlan.macAddr.arEther, addr, 6);
  mac_entry.dstInterface.type = CPSS_INTERFACE_PORT_E;
  mac_entry.dstInterface.devPort.devNum = 0;
  mac_entry.dstInterface.devPort.portNum = 63;
  mac_entry.isStatic = GT_TRUE;
  mac_entry.daCommand = CPSS_MAC_TABLE_CNTL_E;
  mac_entry.saCommand = CPSS_MAC_TABLE_FRWRD_E;
  return CRP (cpssDxChBrgFdbMacEntrySet (0, &mac_entry));
}

enum status
vlan_set_dot1q_tag_native (int value)
{
  GT_STATUS rc = GT_OK;

  if (value != vlan_dot1q_tag_native) {
    GT_BOOL tag;
    CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT cmd;
    int i;

    if (value) {
      tag = GT_TRUE;
      cmd = CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
    } else {
      tag = GT_FALSE;
      cmd = CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
    }

    for (i = 0; i < nports; i++) {
      struct port *port = port_ptr (i + 1);

      if (port->mode == PM_TRUNK) {
        rc = CRP (cpssDxChBrgVlanMemberSet
                  (port->ldev,
                   port->native_vid,
                   port->lport,
                   GT_TRUE,
                   tag,
                   cmd));
        if (rc != GT_OK)
          break;
      }
    }
  }

  switch (rc) {
  case GT_OK:
    vlan_dot1q_tag_native = value;
    return ST_OK;
  case GT_HW_ERROR:
    return ST_HW_ERROR;
  default:
    return ST_HEX;
  }
}

enum status
vlan_set_cpu (vid_t vid, bool_t cpu)
{
  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  return pdsa_vlan_if_op (vid, cpu);
}
