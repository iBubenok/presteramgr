#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <cpss/dxCh/dxChxGen/trunk/cpssDxChTrunk.h>

#include <trunk.h>
#include <gif.h>
#include <port.h>
#include <stack.h>
#include <dev.h>
#include <utils.h>
#include <debug.h>

struct trunk trunks[TRUNK_MAX + 1];

static void set_balance_mode_ip ();
static void set_balance_mode_ip_port ();
static void set_balance_mode_mac ();
static void set_balance_mode_mac_ip ();
static void set_balance_mode_mac_ip_port ();

static void
trunk_lib_init (void)
{
  int d;

  for_each_dev (d)
    CRP (cpssDxChTrunkInit
         (d, TRUNK_MAX, CPSS_DXCH_TRUNK_MEMBERS_MODE_NATIVE_E));
}

void
trunk_init (void)
{
  int dev, i;

  trunk_lib_init ();

  for_each_dev (dev) {
    CRP (cpssDxChTrunkHashGlobalModeSet (dev, CPSS_DXCH_TRUNK_LBH_PACKETS_INFO_E));
  }

  set_balance_mode_ip_port();

  memset (trunks, 0, sizeof (trunks));

  for (i = 1; i <= TRUNK_MAX; i++) {
    trunks[i].vif.vifid.type = VIFT_PC;
    trunks[i].vif.vifid.dev = 0;
    trunks[i].vif.vifid.num = i;
    int j;
    for (j = 0; j < 256; j++)
      trunks[i].vif.stg_state[j] = STP_STATE_DISABLED;
    trunks[i].id = i;
    trunks[i].designated = NULL;

    trunks[i].vif.c_speed = PORT_SPEED_AUTO;
    trunks[i].vif.c_speed_auto = 1;
    trunks[i].vif.c_duplex = PORT_DUPLEX_AUTO;
    trunks[i].vif.c_shutdown = 0;

    vif_trunk_proc_init(&trunks[i].vif);
//    trunks[i].vif.set_speed = vif_set_speed_trunk;

    trunks[i].nports = 0;
    for (j = 0; j < TRUNK_MAX_MEMBERS; j++) {
      trunks[i].vif_port[j] = NULL;
      trunks[i].port_enabled[j] = 0;
    }
  }
}

enum status
trunk_set_members (trunk_id_t trunk, int nmem, struct trunk_member *mem, void *socket) {
  CPSS_TRUNK_MEMBER_STC e[TRUNK_MAX_MEMBERS], d[TRUNK_MAX_MEMBERS];
  int ne, nd, i, dev;

  if (!in_range (trunk, TRUNK_ID_MIN, TRUNK_ID_MAX))
    return ST_BAD_VALUE;

  if (!in_range (nmem, 0, TRUNK_MAX_MEMBERS))
    return ST_BAD_VALUE;

  memset (e, 0, sizeof (e));
  ne = 0;
  memset (d, 0, sizeof (d));
  nd = 0;

  for (i = 0; i < nmem; i++) {
    struct hw_port hp;
    enum status result;

    if (!in_range (mem[i].id.type, GIFT_FE, GIFT_XG))
      return ST_BAD_VALUE;

    result = gif_get_hw_port (&hp, mem[i].id.type, mem[i].id.dev, mem[i].id.num);
    if (result == ST_DOES_NOT_EXIST)
      continue;
    if (result != ST_OK)
      return result;

    if (mem[i].enabled) {
      e[ne].port = hp.hw_port;
      e[ne].device = hp.hw_dev;
      ne++;
    } else {
      d[nd].port = hp.hw_port;
      d[nd].device = hp.hw_dev;
      nd++;
    }
  }

  vif_set_trunk_members (trunk, nmem, mem, socket);

  for_each_dev (dev)
    CRP (cpssDxChTrunkMembersSet (dev, trunk, ne, e, nd, d));

  return ST_OK;
}

enum status
trunk_set_balance_mode(traffic_balance_mode_t mode)
{
  switch (mode) {
    case TBM_IP:          set_balance_mode_ip (mode); break;
    case TBM_IP_PORT:     set_balance_mode_ip_port (mode); break;
    case TBM_MAC:         set_balance_mode_mac (mode); break;
    case TBM_MAC_IP:      set_balance_mode_mac_ip (mode); break;
    case TBM_MAC_IP_PORT: set_balance_mode_mac_ip_port (mode); break;
    default:              return ST_BAD_VALUE;
  }

  return ST_OK;
}

/*** internal functions ***/

static void
set_balance_mode_ip ()
{
  int dev;

  for_each_dev (dev) {
    cpssDxChTrunkHashIpModeSet (dev, GT_TRUE);
    cpssDxChTrunkHashIpAddMacModeSet (dev, GT_FALSE);
    cpssDxChTrunkHashL4ModeSet (dev, CPSS_DXCH_TRUNK_L4_LBH_DISABLED_E);
  }
}

static void
set_balance_mode_ip_port ()
{
  int dev;

  for_each_dev (dev) {
    cpssDxChTrunkHashIpModeSet (dev, GT_TRUE);
    cpssDxChTrunkHashIpAddMacModeSet (dev, GT_FALSE);
    cpssDxChTrunkHashL4ModeSet (dev, CPSS_DXCH_TRUNK_L4_LBH_LONG_E);
    cpssDxChTrunkHashIpv6ModeSet (dev, CPSS_DXCH_TRUNK_IPV6_HASH_MSB_LSB_SIP_DIP_FLOW_E);
  }
}

static void
set_balance_mode_mac ()
{
  int dev;

  for_each_dev (dev) {
    cpssDxChTrunkHashIpModeSet (dev, GT_FALSE);
    cpssDxChTrunkHashIpAddMacModeSet (dev, GT_TRUE);
  }
}

static void
set_balance_mode_mac_ip ()
{
  int dev;

  for_each_dev (dev) {
    cpssDxChTrunkHashIpModeSet (dev, GT_TRUE);
    cpssDxChTrunkHashIpAddMacModeSet (dev, GT_TRUE);
    cpssDxChTrunkHashL4ModeSet (dev, CPSS_DXCH_TRUNK_L4_LBH_DISABLED_E);
    cpssDxChTrunkHashIpv6ModeSet (dev, CPSS_DXCH_TRUNK_IPV6_HASH_LSB_SIP_DIP_E);
  }
}

static void
set_balance_mode_mac_ip_port ()
{
  int dev;

  for_each_dev (dev) {
    cpssDxChTrunkHashIpModeSet (dev, GT_TRUE);
    cpssDxChTrunkHashIpAddMacModeSet (dev, GT_TRUE);
    cpssDxChTrunkHashL4ModeSet (dev, CPSS_DXCH_TRUNK_L4_LBH_LONG_E);
    cpssDxChTrunkHashIpv6ModeSet (dev, CPSS_DXCH_TRUNK_IPV6_HASH_MSB_LSB_SIP_DIP_FLOW_E);
  }
}
