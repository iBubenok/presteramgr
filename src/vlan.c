#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>

#include <presteramgr.h>
#include <debug.h>
#include <string.h>

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
    ("VLAN %d\n"
     "  unkSrcAddrSecBreach   = %s\n"
     "  unregNonIpMcastCmd    = %s\n"
     "  unregIpv4McastCmd     = %s\n"
     "  unregIpv6McastCmd     = %s\n"
     "  unkUcastCmd           = %s\n"
     "  unregIpv4BcastCmd     = %s\n"
     "  unregNonIpv4BcastCmd  = %s\n"
     "  ipv4IgmpToCpuEn       = %s\n"
     "  mirrToRxAnalyzerEn    = %s\n"
     "  ipv6IcmpToCpuEn       = %s\n"
     "  ipCtrlToCpuEn         = %s\n"
     "  ipv4IpmBrgMode        = %s\n"
     "  ipv6IpmBrgMode        = %s\n"
     "  ipv4IpmBrgEn          = %s\n"
     "  ipv6IpmBrgEn          = %s\n"
     "  ipv6SiteIdMode        = %s\n"
     "  ipv4UcastRouteEn      = %s\n"
     "  ipv4McastRouteEn      = %s\n"
     "  ipv6UcastRouteEn      = %s\n"
     "  ipv6McastRouteEn      = %s\n"
     "  stgId                 = %d\n"
     "  autoLearnDisable      = %s\n"
     "  naMsgToCpuEn          = %s\n"
     "  mruIdx                = %d\n"
     "  bcastUdpTrapMirrEn    = %s\n"
     "  vrfId                 = %d\n"
     "  floodVidx             = %d\n"
     "  floodVidxMode         = %s\n"
     "  portIsolationMode     = %s\n"
     "  ucastLocalSwitchingEn = %s\n"
     "  mcastLocalSwitchingEn = %s\n",
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

static void
vlan_read_1 (void)
{
  CPSS_PORTS_BMP_STC members, tagging = { .ports = {0, 0} };
  CPSS_DXCH_BRG_VLAN_INFO_STC vlan_info;
  CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC tagging_cmd;
  GT_BOOL valid;
  int i;

  CRP (cpssDxChBrgVlanEntryRead (0, 1, &members, &tagging, &vlan_info, &valid, &tagging_cmd));
  vlan_print_info (1, &vlan_info);
  osPrintSync ("\n  members: ");
  print_port_bmp (&members);
  osPrintSync ("\n  tagging: ");
  print_port_bmp (&tagging);
  osPrintSync ("\n  valid:   %s", SHOW (GT_BOOL, valid));
  osPrintSync ("\n  tagging cmd:\n");
  for (i = 0; i < CPSS_MAX_PORTS_NUM_CNS; ++i)
    osPrintSync ("    %2d: %s\n", i,
                 SHOW (CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT,
                       tagging_cmd.portsCmd[i]));
}

int
vlan_add (GT_U16 vid)
{
  CPSS_PORTS_BMP_STC members, tagging;
  CPSS_DXCH_BRG_VLAN_INFO_STC vlan_info;
  CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC tagging_cmd;
  GT_STATUS rc;
  int i;

  memset (&members, 0, sizeof (members));
  for (i = 0; i < CPSS_MAX_PORTS_NUM_CNS; ++i)
    if (PRV_CPSS_PP_MAC (0)->phyPortInfoArray [i].portType
        != PRV_CPSS_PORT_NOT_EXISTS_E)
      CPSS_PORTS_BMP_PORT_SET_MAC (&members, i);
  CPSS_PORTS_BMP_PORT_SET_MAC (&members, CPSS_CPU_PORT_NUM_CNS);

  memset (&tagging, 0, sizeof (tagging));

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
  vlan_info.portIsolationMode     = CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_DISABLE_CMD_E;
  vlan_info.ucastLocalSwitchingEn = GT_FALSE;
  vlan_info.mcastLocalSwitchingEn = GT_FALSE;

  memset (&tagging_cmd, 0, sizeof (tagging_cmd));
  for (i = 0; i < CPSS_MAX_PORTS_NUM_CNS; ++i)
    tagging_cmd.portsCmd[i] = CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;

  rc = CRP (cpssDxChBrgVlanEntryWrite (0, vid, &members,
                                       &tagging, &vlan_info, &tagging_cmd));

  return rc != GT_OK;
}

int
vlan_init (void)
{
  GT_STATUS rc;

  rc = CRP (cpssDxChBrgVlanBridgingModeSet (0, CPSS_BRG_MODE_802_1Q_E));
  vlan_add (1);
  vlan_read_1 ();

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
