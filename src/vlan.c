#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgStp.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdbHash.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgMc.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>

#include <sysdeps.h>
#include <presteramgr.h>
#include <debug.h>
#include <string.h>
#include <vlan.h>
#include <port.h>
#include <pdsa.h>
#include <route.h>
#include <utils.h>
#include <dev.h>
#include <mac.h>
#include <control-proto.h>

struct vlan vlans[NVLANS];
stp_state_t stg_state[NPORTS][256];

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

static inline void
stg_set_inactive (stp_id_t stg)
{
  stgs[STG_IDX (stg)] &= ~(STG_BIT (stg));
}

int vlan_dot1q_tag_native = 0;
int vlan_xlate_tunnel = 0;

static void
setup_tagging (vid_t vid,
               CPSS_PORTS_BMP_STC *members,
               CPSS_PORTS_BMP_STC *tagging,
               CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC *tagging_cmd)
{
  int i, d;

  memset (members, 0, sizeof (*members) * NDEVS);
  memset (tagging, 0, sizeof (*tagging) * NDEVS);
  memset (tagging_cmd, 0, sizeof (*tagging_cmd) * NDEVS);

  for_each_dev (d) {
    for (i = 0; i < dev_info[d].n_ic_ports; i++) {
      int p = dev_info[d].ic_ports[i];

      CPSS_PORTS_BMP_PORT_SET_MAC (&members[d], p);
      CPSS_PORTS_BMP_PORT_SET_MAC (&tagging[d], p);
      tagging_cmd[d].portsCmd[p] =
        CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E;
    }
  }

  for (i = 0; i < nports; i++) {
    struct port *port = port_ptr (i + 1);
    d = port->ldev;

    if (is_stack_port (port)) {
      CPSS_PORTS_BMP_PORT_SET_MAC (members, port->lport);
      tagging_cmd->portsCmd[port->lport] =
        CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E;
      continue;
    }

    switch (port->mode) {
    case PM_ACCESS:
      if (port->access_vid == vid) {
        CPSS_PORTS_BMP_PORT_SET_MAC (&members[d], port->lport);
        tagging_cmd[d].portsCmd[port->lport] =
          CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
      } else if (port->voice_vid == vid) {
        CPSS_PORTS_BMP_PORT_SET_MAC (&members[d], port->lport);
        tagging_cmd[d].portsCmd[port->lport] =
          CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E;
      }
      break;

    case PM_TRUNK:
      if (port->native_vid == vid ||
          port->vlan_conf[vid - 1].tallow ||
          port->vlan_conf[vid - 1].refc) {
        CPSS_PORTS_BMP_PORT_SET_MAC (&members[d], port->lport);

        if (port->vlan_conf[vid - 1].refc) {
          CPSS_PORTS_BMP_PORT_SET_MAC (&tagging[d], port->lport);
          tagging_cmd[d].portsCmd[port->lport] = vlan_xlate_tunnel
            ? CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E
            : CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
        } else if (port->native_vid == vid) {
          CPSS_PORTS_BMP_PORT_SET_MAC (&tagging[d], port->lport);
          tagging_cmd[d].portsCmd[port->lport] = vlan_dot1q_tag_native
            ? CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E
            : CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
        } else if (vlans[vid - 1].vt_refc && vlan_xlate_tunnel) {
          CPSS_PORTS_BMP_PORT_SET_MAC (&tagging[d], port->lport);
          tagging_cmd[d].portsCmd[port->lport] =
            CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
        } else
          tagging_cmd[d].portsCmd[port->lport] =
            CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E;
      }
      break;

    case PM_CUSTOMER:
      if (port->customer_vid == vid) {
        CPSS_PORTS_BMP_PORT_SET_MAC (&members[d], port->lport);
        tagging_cmd[d].portsCmd[port->lport] =
          CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E;
      }
      break;
    }
  }
}

static enum status
__vlan_add (vid_t vid)
{
  CPSS_PORTS_BMP_STC members[NDEVS], tagging[NDEVS];
  CPSS_DXCH_BRG_VLAN_INFO_STC vlan_info;
  CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC tagging_cmd[NDEVS];
  int d;
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
  vlan_info.autoLearnDisable      = GT_TRUE;
  vlan_info.naMsgToCpuEn          = GT_TRUE;
  vlan_info.mruIdx                = 0;
  vlan_info.bcastUdpTrapMirrEn    = GT_FALSE;
  vlan_info.vrfId                 = 0;
  vlan_info.floodVidx             = 4095;
  vlan_info.floodVidxMode         = CPSS_DXCH_BRG_VLAN_FLOOD_VIDX_MODE_UNREG_MC_E;
  vlan_info.portIsolationMode     = CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_L2_CMD_E;
  vlan_info.ucastLocalSwitchingEn = GT_FALSE;
  vlan_info.mcastLocalSwitchingEn = GT_FALSE;

  setup_tagging (vid, members, tagging, tagging_cmd);

  for_each_dev (d) {
    rc = CRP (cpssDxChBrgVlanEntryWrite (d, vid, &members[d],
                                         &tagging[d], &vlan_info,
                                         &tagging_cmd[d]));
    ON_GT_ERROR (rc) break;
  }

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
vlan_add_range (uint16_t size, vid_t* arr)
{
  enum status result;

  while (size) {
    if ( IS_IN_RANGE (*arr) ) {
    	vid_t v;
    	for (v = (*arr - 10000); v <= (*(arr + 1) - 10000); v++) {
    		if ( (result = vlan_add (v)) != ST_OK ) {
    			return result;
    		}
    	}
      if (size) {
    	  arr += 2;
    	  size -= 2;
      }
    } else {
    	if ( (result = vlan_add (*arr)) != ST_OK ) {
    			return result;
    	}
      if (size) {
    	  arr++;
    	  size--;
      }
    }
  }
  return ST_OK;
}

enum status
vlan_delete (vid_t vid)
{
  GT_STATUS rc;
  int d;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  for_each_dev (d) {
    rc = CRP (cpssDxChBrgVlanEntryInvalidate (d, vid));
    ON_GT_ERROR (rc) break;
  }

  switch (rc) {
  case GT_OK:
    vlans[vid - 1].state = VS_DELETED;
    return vlan_set_cpu (vid, 0);
  default:
    return ST_HEX;
  }
}

enum status
vlan_delete_range (uint16_t size, vid_t* arr)
{
  enum status result;

  while (size) {
    if ( IS_IN_RANGE (*arr) ) {
      vid_t v;
      for (v = (*arr - 10000); v <= (*(arr + 1) - 10000); v++) {
        if ( (result = vlan_delete (v)) != ST_OK ) {
          return result;
        }
      }
      if (size) {
        arr += 2;
        size -= 2;
      }
    } else {
      if ( (result = vlan_delete (*arr)) != ST_OK ) {
          return result;
      }
      if (size) {
        arr++;
        size--;
      }
    }
  }

  return ST_OK;
}

int
vlan_init (void)
{
  CPSS_PORTS_BMP_STC pbm;
  int i, d;

  for (i = 0; i < NVLANS; i++) {
    vlans[i].vid = i + 1;
    vlans[i].stp_id = 0;
    vlans[i].c_cpu = 0;
    vlans[i].mac_addr_set = 0;
    vlans[i].state = VS_DELETED;
    vlans[i].vt_refc = 0;
  }

  memset (&pbm, 0, sizeof (pbm));

  for_each_dev (d) {
    CRP (cpssDxChBrgVlanTableInvalidate (d));
    CRP (cpssDxChBrgVlanMruProfileValueSet (d, 0, 12000));
    CRP (cpssDxChBrgVlanBridgingModeSet (d, CPSS_BRG_MODE_802_1Q_E));
    CRP (cpssDxChBrgVlanRemoveVlanTag1IfZeroModeSet
         (d, CPSS_DXCH_BRG_VLAN_REMOVE_TAG1_IF_ZERO_E));

    CRP (cpssDxChBrgVlanTpidEntrySet
         (d, CPSS_DIRECTION_INGRESS_E, VLAN_TPID_IDX, VLAN_TPID));
    CRP (cpssDxChBrgVlanTpidEntrySet
         (d, CPSS_DIRECTION_EGRESS_E, VLAN_TPID_IDX, VLAN_TPID));

    CRP (cpssDxChBrgVlanTpidEntrySet
         (d, CPSS_DIRECTION_INGRESS_E, FAKE_TPID_IDX, FAKE_TPID));
    CRP (cpssDxChBrgVlanTpidEntrySet
         (d, CPSS_DIRECTION_EGRESS_E, FAKE_TPID_IDX, FAKE_TPID));

    CRP (cpssDxChBrgMcEntryWrite (d, 4092, &pbm));
  }

  static stp_id_t ids[NVLANS];
  memset (ids, 0, sizeof (ids));
  vlan_set_fdb_map (ids);
  memset (stg_state, STP_STATE_DISABLED, sizeof(stg_state));

  vlan_add (1);
  __vlan_add (SVC_VID);

  return ST_OK;
}

static void
vlan_clear_mac_addr (struct vlan *vlan)
{
  if (vlan->mac_addr_set) {
    mac_op_own (vlan->vid, vlan->c_mac_addr, 0);
    vlan->mac_addr_set = 0;
  }
}

void
vlan_set_mac_addr (GT_U16 vid, const unsigned char *addr)
{
  struct vlan *vlan = &vlans[vid - 1];

  vlan_clear_mac_addr (vlan);
  memcpy (vlan->c_mac_addr, addr, 6);
  mac_op_own (vlan->vid, vlan->c_mac_addr, 1);
  vlan->mac_addr_set = 1;
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

      if (port->mode == PM_TRUNK &&
          !port->vlan_conf[port->native_vid - 1].refc) {
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
  CPSS_PORTS_BMP_STC members, tagging;
  CPSS_DXCH_BRG_VLAN_INFO_STC vlan_info;
  CPSS_DXCH_BRG_VLAN_PORTS_TAG_CMD_STC tagging_cmd;
  GT_BOOL valid;
  int d;

  for_each_dev (d) {
    CRP (cpssDxChBrgVlanEntryRead
         (d, vid, &members, &tagging, &vlan_info, &valid, &tagging_cmd));
    if (cpu) {
      vlan_info.ipCtrlToCpuEn      = CPSS_DXCH_BRG_IP_CTRL_IPV4_E;
      vlan_info.unregIpv4BcastCmd  = CPSS_PACKET_CMD_MIRROR_TO_CPU_E;
    } else {
      vlan_info.ipCtrlToCpuEn      = CPSS_DXCH_BRG_IP_CTRL_NONE_E;
      vlan_info.unregIpv4BcastCmd  = CPSS_PACKET_CMD_FORWARD_E;
    }
    CRP (cpssDxChBrgVlanEntryWrite
         (d, vid, &members, &tagging, &vlan_info, &tagging_cmd));
  }
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
vlan_set_cpu_range (uint16_t size, vid_t* arr, bool_t cpu)
{
  enum status result;

  while (size) {
    if ( IS_IN_RANGE (*arr) ) {
      vid_t v;
      for (v = (*arr - 10000); v <= (*(arr + 1) - 10000); v++) {
        if ( (result = vlan_set_cpu (v, cpu)) != ST_OK ) {
          return result;
        }
      }
      if (size) {
        arr += 2;
        size -= 2;
      }
    } else {
      if ( (result = vlan_set_cpu (*arr, cpu)) != ST_OK ) {
          return result;
      }
      if (size) {
        arr++;
        size--;
      }
    }
  }

  return ST_OK;
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
    vlan_set_stp_id(i + 1, ids[i]);
  }

  return ST_OK;
}

enum status
vlan_set_stp_id (vid_t vid, stp_id_t id)
{
  if (id > 255) return ST_BAD_VALUE;

  int i, deactive = 1;
  for (i = 0; i < NVLANS; i++)
    if ((i != (vid-1))                           &&
        (vlans[i].stp_id == vlans[vid-1].stp_id) &&
        stg_is_active(vlans[i].stp_id)) {
      deactive = 0;
      break;
    }

  if (deactive) stg_set_inactive(vlans[vid-1].stp_id);

  vlans[vid-1].stp_id = id;
  stg_set_active(id);

  int d;
  for_each_dev (d) {
    int p;
    CRP (cpssDxChBrgVlanToStpIdBind (d, vid, id));
    for (p = 0; p < dev_info[d].n_ic_ports; p++) {
      CRP (cpssDxChBrgStpStateSet
           (d, dev_info[d].ic_ports[p], id, CPSS_STP_FRWRD_E));
    }
  }

  ports_all_set_stp_state_default(id);

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
vlan_svc_enable_port (port_id_t pid, int en)
{
  struct port *port;

  port = port_ptr (pid);
  if (!port || is_stack_port (port))
    return;

  CRP (cpssDxChBrgVlanMemberSet
       (port->ldev, SVC_VID, port->lport, gt_bool (en),
        GT_FALSE, CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E));
}

enum status
vlan_set_xlate_tunnel (int enable)
{
  enable = !!enable;

  if (vlan_xlate_tunnel != enable) {
    int i, d;

    port_clear_translation (ALL_PORTS);
    for (i = 1; i < 4095; i++)
      port_update_trunk_vlan_all_ports (i);

    for_each_dev (d)
      CRP (cpssDxChBrgVlanRemoveVlanTag1IfZeroModeSet
           (d, (enable
                ? CPSS_DXCH_BRG_VLAN_REMOVE_TAG1_IF_ZERO_DISABLE_E
                : CPSS_DXCH_BRG_VLAN_REMOVE_TAG1_IF_ZERO_E)));

    vlan_xlate_tunnel = enable;
  }

  return ST_OK;
}

enum status
vlan_igmp_snoop (vid_t vid, int enable)
{
  GT_STATUS rc;
  int d;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  for_each_dev (d)
    rc = CRP (cpssDxChBrgVlanIgmpSnoopingEnable (d, vid, gt_bool (enable)));
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

void
vlan_stack_setup (void)
{
  __vlan_add (4095);
}

enum status
vlan_mc_route (vid_t vid, bool_t enable)
{
  int d;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  for_each_dev (d) {
    if (enable) {
      CRP (cpssDxChBrgVlanIpMcRouteEnable
           (d, vid, CPSS_IP_PROTOCOL_IPV4_E, GT_TRUE));
      CRP (cpssDxChBrgVlanFloodVidxModeSet
           (d, vid, 4092, CPSS_DXCH_BRG_VLAN_FLOOD_VIDX_MODE_UNREG_MC_E));
    } else {
      CRP (cpssDxChBrgVlanIpMcRouteEnable
           (d, vid, CPSS_IP_PROTOCOL_IPV4_E, GT_FALSE));
      CRP (cpssDxChBrgVlanFloodVidxModeSet
           (d, vid, 4095, CPSS_DXCH_BRG_VLAN_FLOOD_VIDX_MODE_UNREG_MC_E));
    }
  }

  return ST_OK;
}

enum status
vlan_get_stp_id (vid_t vid, stp_id_t *stp_id)
{
  if (!vlan_valid(vid)) return ST_BAD_VALUE;

  memcpy(stp_id, &vlans[vid - 1].stp_id, sizeof(stp_id_t));

  return ST_OK;
}
