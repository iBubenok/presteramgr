#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdbHash.h>

#include <presteramgr.h>
#include <debug.h>
#include <string.h>
#include <vlan.h>
#include <port.h>
#include <pdsa.h>
#include <route.h>
#include <dev.h>
#include <control-proto.h>

struct vlan vlans[NVLANS];

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

static uint32_t stgs[256 / sizeof (uint32_t)];
#define STG_IDX(stg) (stg / sizeof (uint32_t))
#define STG_BIT(stg) (1 << (stg % sizeof (uint32_t)))

static inline void
stgs_clear (void)
{
  memset (stgs, 0, sizeof (stgs));
}

int
stg_is_active (stp_id_t stg)
{
  return stgs[STG_IDX (stg)] & STG_BIT (stg);
}

static inline void
stg_set_active (stp_id_t stg)
{
  stgs[STG_IDX (stg)] |= STG_BIT (stg);
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

    if (is_stack_port (port)) {
      CPSS_PORTS_BMP_PORT_SET_MAC (members, port->lport);
      tagging_cmd->portsCmd[port->lport] =
        CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E;
      continue;
    }

    switch (port->mode) {
    case PM_ACCESS:
      if (port->access_vid == vid) {
        CPSS_PORTS_BMP_PORT_SET_MAC (members, port->lport);
        tagging_cmd->portsCmd[port->lport] =
          CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
      }
      break;

    case PM_TRUNK:
      CPSS_PORTS_BMP_PORT_SET_MAC (members, port->lport);
      if (port->native_vid != vid || vlan_dot1q_tag_native) {
        CPSS_PORTS_BMP_PORT_SET_MAC (tagging, port->lport);
        tagging_cmd->portsCmd[port->lport] =
          CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E;
      } else
        tagging_cmd->portsCmd[port->lport] =
          CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
      break;

    case PM_CUSTOMER:
      if (port->customer_vid == vid) {
        CPSS_PORTS_BMP_PORT_SET_MAC (members, port->lport);
        tagging_cmd->portsCmd[port->lport] =
          CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E;
      }
      break;
    }
  }
}



static enum status
__vlan_add (vid_t vid)
{
  CPSS_PORTS_BMP_STC members, tagging;
  CPSS_DXCH_BRG_VLAN_INFO_STC vlan_info;
  CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC tagging_cmd;
  GT_STATUS rc;

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
  vlan_info.ipv4IpmBrgMode        = CPSS_BRG_IPM_GV_E;
  vlan_info.ipv6IpmBrgMode        = CPSS_BRG_IPM_SGV_E;
  vlan_info.ipv4IpmBrgEn          = GT_TRUE;
  vlan_info.ipv6IpmBrgEn          = GT_FALSE;
  vlan_info.ipv6SiteIdMode        = CPSS_IP_SITE_ID_INTERNAL_E;
  vlan_info.ipv4UcastRouteEn      = GT_TRUE;
  vlan_info.ipv4McastRouteEn      = GT_FALSE;
  vlan_info.ipv6UcastRouteEn      = GT_FALSE;
  vlan_info.ipv6McastRouteEn      = GT_FALSE;
  vlan_info.stgId                 = vlans[vid - 1].stp_id;
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
vlan_add (vid_t vid)
{
  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  return __vlan_add (vid);
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
    return vlan_set_cpu (vid, 0);
  default:
    return ST_HEX;
  }
}

int
vlan_init (void)
{
  GT_STATUS rc;
  int i;

  for (i = 0; i < NVLANS; i++) {
    vlans[i].vid = i + 1;
    vlans[i].stp_id = 0;
    vlans[i].c_cpu = 0;
    vlans[i].mac_addr_set = 0;
    vlans[i].state = VS_DELETED;
  }

  CRP (cpssDxChBrgVlanTableInvalidate (0));
  CRP (cpssDxChBrgVlanMruProfileValueSet (0, 0, 10000));
  rc = CRP (cpssDxChBrgVlanBridgingModeSet (0, CPSS_BRG_MODE_802_1Q_E));
  CRP (cpssDxChBrgVlanRemoveVlanTag1IfZeroModeSet
       (0, CPSS_DXCH_BRG_VLAN_REMOVE_TAG1_IF_ZERO_E));

  CRP (cpssDxChBrgVlanTpidEntrySet
       (0, CPSS_DIRECTION_INGRESS_E, VLAN_TPID_IDX, VLAN_TPID));
  CRP (cpssDxChBrgVlanTpidEntrySet
       (0, CPSS_DIRECTION_EGRESS_E, VLAN_TPID_IDX, VLAN_TPID));

  CRP (cpssDxChBrgVlanTpidEntrySet
       (0, CPSS_DIRECTION_INGRESS_E, FAKE_TPID_IDX, FAKE_TPID));
  CRP (cpssDxChBrgVlanTpidEntrySet
       (0, CPSS_DIRECTION_EGRESS_E, FAKE_TPID_IDX, FAKE_TPID));

  vlan_add (1);

  static stp_id_t ids[NVLANS];
  memset (ids, 0, sizeof (ids));
  vlan_set_fdb_map (ids);

  return rc != GT_OK;
}

static void
vlan_clear_mac_addr (struct vlan *vlan)
{
  if (vlan->mac_addr_set) {
    DEBUG ("invalidate FDB entry at %d", vlan->mac_idx);
    CRP (cpssDxChBrgFdbMacEntryInvalidate (0, vlan->mac_idx));
    vlan->mac_idx = 0;
    vlan->mac_addr_set = 0;
  }
}

#define VLAN_MAC_ENTRY 1

GT_STATUS
vlan_set_mac_addr (GT_U16 vid, const unsigned char *addr)
{
  CPSS_MAC_ENTRY_EXT_STC mac_entry;
  GT_STATUS rc;
  GT_U32 idx, best_idx, i, score;

  vlan_clear_mac_addr (&vlans[vid - 1]);

  memset (&mac_entry, 0, sizeof (mac_entry));
  mac_entry.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  mac_entry.key.key.macVlan.vlanId = vid;
  memcpy (mac_entry.key.key.macVlan.macAddr.arEther, addr, 6);
  mac_entry.dstInterface.type = CPSS_INTERFACE_PORT_E;
  mac_entry.dstInterface.devPort.devNum = phys_dev (0);
  mac_entry.dstInterface.devPort.portNum = 63;
  mac_entry.appSpecificCpuCode = GT_TRUE;
  mac_entry.isStatic = GT_TRUE;
  mac_entry.daCommand = CPSS_MAC_TABLE_FRWRD_E;
  mac_entry.saCommand = CPSS_MAC_TABLE_FRWRD_E;
  mac_entry.daRoute = GT_TRUE;
  mac_entry.userDefined = VLAN_MAC_ENTRY;

  rc = CRP (cpssDxChBrgFdbHashCalc (0, &mac_entry.key, &idx));
  if (rc != GT_OK)
    goto out;

  best_idx = idx;
  score = 10;
  DEBUG ("searching FDB from %lu to %lu, initial score %lu",
         idx, idx + 3, score);
  for (i = 0; i < 4; i++) {
    GT_BOOL valid, skip, aged;
    GT_U8 adev;
    CPSS_MAC_ENTRY_EXT_STC tmp;

    CRP (cpssDxChBrgFdbMacEntryRead
         (0, idx + i, &valid, &skip, &aged, &adev, &tmp));

    if (skip || !valid) {
      DEBUG ("found free FDB entry (score 0) at %lu", idx + i);
      best_idx = idx + i;
      score = 0;
      break;
    }

    if (aged) {
      DEBUG ("found aged FDB entry (score 1) at %lu", idx + i);
      best_idx = idx + i;
      score = 1;
      continue;
    }

    if (tmp.isStatic) {
      if (tmp.userDefined == VLAN_MAC_ENTRY) {
        DEBUG ("found static VLAN FDB entry (score 10) at %lu", idx + i);
        continue;
      } else {
        DEBUG ("found static FDB entry (score 5) at %lu", idx + i);
        if (score > 5) {
          best_idx = idx + i;
          score = 5;
        }
      }
    } else {
      DEBUG ("found dynamic FDB entry (score 2) at %lu", idx + i);
      if (score > 2) {
        best_idx = idx + i;
        score = 2;
      }
    }
  }

  if (score < 10) {
    DEBUG ("writing VLAN FDB entry at %lu (score %lu)", best_idx, score);
    rc = CRP (cpssDxChBrgFdbMacEntryWrite
              (0, best_idx, GT_FALSE, &mac_entry));
    if (rc != GT_OK)
      goto out;
  } else {
    DEBUG ("no room for VLAN FDB entry");
    rc = GT_NOT_FOUND;
    goto out;
  }

  memcpy (vlans[vid - 1].c_mac_addr, addr, 6);
  vlans[vid - 1].mac_addr_set = 1;
  vlans[vid - 1].mac_idx = best_idx;

  return GT_OK;

 out:
  return rc;
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
      cmd = CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E;
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

static void
vlan_reconf_cpu (vid_t vid, bool_t cpu)
{
  CPSS_PORTS_BMP_STC members, tagging = { .ports = {0, 0} };
  CPSS_DXCH_BRG_VLAN_INFO_STC vlan_info;
  CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC tagging_cmd;
  GT_BOOL valid;

  CRP (cpssDxChBrgVlanEntryRead
       (0, vid, &members, &tagging, &vlan_info, &valid, &tagging_cmd));
  if (cpu) {
    vlan_info.ipCtrlToCpuEn      = CPSS_DXCH_BRG_IP_CTRL_IPV4_E;
    vlan_info.unregIpv4BcastCmd  = CPSS_PACKET_CMD_MIRROR_TO_CPU_E;
  } else {
    vlan_info.ipCtrlToCpuEn      = CPSS_DXCH_BRG_IP_CTRL_NONE_E;
    vlan_info.unregIpv4BcastCmd  = CPSS_PACKET_CMD_FORWARD_E;
  }
  CRP (cpssDxChBrgVlanEntryWrite
       (0, vid, &members, &tagging, &vlan_info, &tagging_cmd));
}

enum status
vlan_set_cpu (vid_t vid, bool_t cpu)
{
  struct vlan *vlan;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  vlan = &vlans[vid - 1];

  cpu = !!cpu;
  vlan_reconf_cpu (vid, cpu);
  if (vlan->c_cpu == cpu)
    return ST_OK;

  vlan->c_cpu = cpu;

  if (!cpu)
    vlan_clear_mac_addr (vlan);

  return pdsa_vlan_if_op (vid, cpu);
}

enum status
vlan_set_fdb_map (const stp_id_t *ids)
{
  int i;

  for (i = 0; i < NVLANS; i++)
    if (ids[i] > 255)
      return ST_BAD_VALUE;

  stgs_clear ();
  for (i = 0; i < NVLANS; i++) {
    vlans[i].stp_id = ids[i];
    stg_set_active (ids[i]);
    if (vlans[i].state == VS_ACTIVE)
      CRP (cpssDxChBrgVlanToStpIdBind (0, i + 1, ids[i]));
  }

  return ST_OK;
}

enum status
vlan_get_mac_addr (vid_t vid, mac_addr_t addr)
{
  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  memcpy (addr, vlans[vid - 1].c_mac_addr, sizeof (mac_addr_t));
  return ST_OK;
}

enum status
vlan_get_ip_addr (vid_t vid, ip_addr_t addr)
{
  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  memcpy (addr, vlans[vid - 1].c_ip_addr, sizeof (ip_addr_t));
  return ST_OK;
}

enum status
vlan_set_ip_addr (vid_t vid, ip_addr_t addr)
{
  struct vlan *vlan;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  vlan = &vlans[vid - 1];

  if (vlan->ip_addr_set)
    route_del_mgmt_ip (vlan->c_ip_addr);

  route_add_mgmt_ip (addr);
  memcpy (vlan->c_ip_addr, addr, sizeof (addr));
  vlan->ip_addr_set = 1;

  return ST_OK;
}

enum status
vlan_del_ip_addr (vid_t vid)
{
  struct vlan *vlan;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  vlan = &vlans[vid - 1];

  if (!vlan->ip_addr_set)
    return ST_DOES_NOT_EXIST;

  route_del_mgmt_ip (vlan->c_ip_addr);
  vlan->ip_addr_set = 0;

  return ST_OK;
}

void
vlan_stack_setup (void)
{
  __vlan_add (4095);
}
