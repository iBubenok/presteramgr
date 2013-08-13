#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/pcl/cpssDxChPcl.h>

#include <pcl.h>
#include <port.h>
#include <log.h>
#include <vlan.h>
#include <utils.h>
#include <uthash.h>
#include <debug.h>

#define PORT_IPCL_ID(n) (((n) - 1) * 2)
#define PORT_EPCL_ID(n) (((n) - 1) * 2 + 1)
#define PORT_LBD_RULE_IX(n) ((n) - 1)

#define STACK_ENTRIES 300
#define STACK_FIRST_ENTRY (PORT_LBD_RULE_IX (64))
#define STACK_MAX (STACK_ENTRIES + STACK_FIRST_ENTRY)
#define PORT_IPCL_DEF_IX(n) (STACK_MAX + (n) * 2)
#define PORT_EPCL_DEF_IX(n) (STACK_MAX + (n) * 2 + 1)

static struct stack {
  int sp;
  int n_free;
  uint16_t data[STACK_ENTRIES];
} rules;

static void
pcl_init_rules (void)
{
  int i;

  for (i = STACK_FIRST_ENTRY; i < STACK_MAX; i++)
    rules.data[i] = i + STACK_FIRST_ENTRY;

  rules.sp = 0;
  rules.n_free = STACK_ENTRIES;
}

static int
pcl_alloc_rules (uint16_t *nums, int n)
{
  int i;

  if (rules.n_free < n)
    return 0;

  rules.n_free -= n;
  for (i = 0; i < n; i++)
    nums[i] = rules.data[rules.sp++];

  return 1;
}

static void __attribute__ ((unused))
pcl_free_rules (const uint16_t *nums, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    if (nums[i] < STACK_MAX) {
      rules.n_free++;
      rules.data[--rules.sp] = nums[i];
    }
  }
}

struct vt_ix {
  int key;
  int tunnel;
  vid_t to;
  uint16_t ix[2];
  UT_hash_handle hh;
};

static int
vt_key (int from, int to, int tunnel)
{
  int key = 0;

  key = from;
  if (tunnel)
    key |= to << 16;

  return key;
}

static struct vt_ix *vt_ix = NULL;

struct vt_ix *
get_vt_ix (port_id_t pid, vid_t from, vid_t to, int tunnel, int alloc)
{
  int key = vt_key (from, to, tunnel);
  struct vt_ix *ix;

  HASH_FIND_INT (vt_ix, &key, ix);
  if (alloc && !ix) {
    ix = calloc (1, sizeof (struct vt_ix));
    ix->key = key;
    ix->tunnel = tunnel;
    ix->to = to;
    if (from) {
      if (!pcl_alloc_rules (ix->ix, tunnel ? 1 : 2)) {
        free (ix);
        return NULL;
      }
    } else {
      /* Default rule for port. */
      ix->ix[0] = PORT_IPCL_DEF_IX (pid);
      ix->ix[1] = PORT_EPCL_DEF_IX (pid);
    }
    HASH_ADD_INT (vt_ix, key, ix);
  }

  return ix;
}

void
free_vt_ix (struct vt_ix *ix)
{
  CRP (cpssDxChPclRuleInvalidate (0, CPSS_PCL_RULE_SIZE_EXT_E, ix->ix[0]));
  pcl_free_rules (&(ix->ix[0]), 1);
  if (!ix->tunnel) {
    CRP (cpssDxChPclRuleInvalidate (0, CPSS_PCL_RULE_SIZE_EXT_E, ix->ix[1]));
    pcl_free_rules (&(ix->ix[1]), 1);
  }
  HASH_DEL (vt_ix, ix);
  free (ix);
}

enum status
pcl_setup_vt (port_id_t pid, vid_t from, vid_t to, int tunnel, int enable)
{
  struct port *port = port_ptr (pid);
  struct vt_ix *ix;

  if (!port)
    return ST_BAD_VALUE;

  if (from == ALL_VLANS) {
    if (to && !vlan_valid (to))
      return ST_BAD_VALUE;
  } else {
    if (!(vlan_valid (from) && vlan_valid (to)))
      return ST_BAD_VALUE;
  }

  ix = get_vt_ix (pid, from, to, tunnel, enable);

  if (enable) { /* Enable translation. */
    CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
    CPSS_DXCH_PCL_ACTION_STC act;

    if (!ix)
      return ST_BAD_STATE;

    memset (&act, 0, sizeof (act));
    act.actionStop = GT_TRUE;
    act.egressPolicy = GT_FALSE;
    if (to) {
      act.pktCmd = CPSS_PACKET_CMD_FORWARD_E;
      act.vlan.modifyVlan = CPSS_PACKET_ATTRIBUTE_ASSIGN_FOR_ALL_E;
      act.vlan.nestedVlan = gt_bool (tunnel);
      act.vlan.vlanId = to;
      act.vlan.precedence = CPSS_PACKET_ATTRIBUTE_ASSIGN_PRECEDENCE_HARD_E;
    } else
      act.pktCmd = CPSS_PACKET_CMD_DROP_HARD_E;

    memset (&mask, 0, sizeof (mask));
    mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
    mask.ruleExtNotIpv6.common.vid = from ? 0xFFFF : 0;
    mask.ruleExtNotIpv6.common.sourcePort = 0xFF;

    memset (&rule, 0, sizeof (rule));
    rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pid);
    rule.ruleExtNotIpv6.common.vid = from;
    rule.ruleExtNotIpv6.common.sourcePort = port->lport;

    CRP (cpssDxChPclRuleSet
         (port->ldev,
          CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
          ix->ix[0],
          0,
          &mask,
          &rule,
          &act));

    if (to && !tunnel) {
      memset (&mask, 0, sizeof (mask));
      mask.ruleEgrExtNotIpv6.common.pclId = 0xFFFF;
      mask.ruleEgrExtNotIpv6.common.vid = from ? 0xFFFF : 0;

      memset (&rule, 0, sizeof (rule));
      rule.ruleEgrExtNotIpv6.common.pclId = PORT_EPCL_ID (pid);
      rule.ruleEgrExtNotIpv6.common.vid = to;

      memset (&act, 0, sizeof (act));
      act.pktCmd = CPSS_PACKET_CMD_FORWARD_E;
      act.actionStop = GT_TRUE;
      act.egressPolicy = GT_TRUE;
      act.vlan.modifyVlan = CPSS_PACKET_ATTRIBUTE_ASSIGN_FOR_ALL_E;
      act.vlan.nestedVlan = GT_FALSE;
      act.vlan.vlanId = from;
      act.vlan.precedence = CPSS_PACKET_ATTRIBUTE_ASSIGN_PRECEDENCE_HARD_E;

      CRP (cpssDxChPclRuleSet
           (port->ldev,
            CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
            ix->ix[1],
            0,
            &mask,
            &rule,
            &act));
    }
  } else { /* Disable translation. */
    if (!ix)
      return ST_DOES_NOT_EXIST;

    free_vt_ix (ix);
  }

  return ST_OK;
}

enum status
pcl_enable_port (port_id_t pid, int enable)
{
  struct port *port = port_ptr (pid);
  CPSS_INTERFACE_INFO_STC iface = {
    .type    = CPSS_INTERFACE_PORT_E,
    .devPort = {
      .devNum  = port->ldev,
      .portNum = port->lport
    }
  };
  CPSS_DXCH_PCL_LOOKUP_CFG_STC lc = {
    .enableLookup  = gt_bool (enable),
    .pclId         = PORT_IPCL_ID (pid),
    .dualLookup    = GT_FALSE,
    .pclIdL01      = 0,
    .groupKeyTypes = {
      .nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L4_E
    }
  };

  CRP (cpssDxChPclCfgTblSet
       (0, &iface,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        &lc));

  lc.pclId                  = PORT_EPCL_ID (pid);
  lc.groupKeyTypes.nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E;
  lc.groupKeyTypes.ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E;
  lc.groupKeyTypes.ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_IPV6_L4_E;
  CRP (cpssDxChPclCfgTblSet
       (0, &iface,
        CPSS_PCL_DIRECTION_EGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        &lc));

  return ST_OK;
}

enum status
pcl_enable_lbd_trap (port_id_t pid, int enable)
{
  if (!port_ptr (pid))
    return ST_BAD_VALUE;

  if (enable) {
    CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
    CPSS_DXCH_PCL_ACTION_STC act;

    memset (&mask, 0, sizeof (mask));
    mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
    mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
    mask.ruleExtNotIpv6.etherType = 0xFFFF;
    mask.ruleExtNotIpv6.l2Encap = 0xFF;

    memset (&rule, 0, sizeof (rule));
    rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pid);
    rule.ruleExtNotIpv6.common.isL2Valid = 1;
    rule.ruleExtNotIpv6.etherType = 0x88B7;
    rule.ruleExtNotIpv6.l2Encap = 1;

    memset (&act, 0, sizeof (act));
    act.pktCmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    act.actionStop = GT_TRUE;
    act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E;

    CRP (cpssDxChPclRuleSet
         (0,
          CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
          PORT_LBD_RULE_IX (pid),
          0,
          &mask,
          &rule,
          &act));
  } else
    CRP (cpssDxChPclRuleInvalidate
         (0, CPSS_PCL_RULE_SIZE_EXT_E, PORT_LBD_RULE_IX (pid)));

  return ST_OK;
}

enum status
pcl_port_setup (port_id_t pid)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  /* Enable ingress PCL. */
  CRP (cpssDxChPclPortIngressPolicyEnable
       (port->ldev, port->lport, GT_TRUE));
  CRP (cpssDxChPclPortLookupCfgTabAccessModeSet
       (port->ldev, port->lport,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        0,
        CPSS_DXCH_PCL_PORT_LOOKUP_CFG_TAB_ACC_MODE_BY_PORT_E));

  /* Enable egress PCL. */
  CRP (cpssDxCh2EgressPclPacketTypesSet
       (port->ldev, port->lport, CPSS_DXCH_PCL_EGRESS_PKT_NON_TS_E, GT_TRUE));
  CRP (cpssDxChPclPortLookupCfgTabAccessModeSet
       (port->ldev, port->lport,
        CPSS_PCL_DIRECTION_EGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        0,
        CPSS_DXCH_PCL_PORT_LOOKUP_CFG_TAB_ACC_MODE_BY_PORT_E));

  return ST_OK;
}

enum status
pcl_cpss_lib_init (void)
{
  CPSS_DXCH_PCL_CFG_TBL_ACCESS_MODE_STC am;

  pcl_init_rules ();

  CRP (cpssDxChPclInit (0));

  /* Enable ingress PCL. */
  CRP (cpssDxChPclIngressPolicyEnable (0, GT_TRUE));

  /* Enable egress PCL. */
  CRP (cpssDxCh2PclEgressPolicyEnable (0, GT_TRUE));

  /* Configure access modes. */
  memset (&am, 0, sizeof (am));
  am.ipclAccMode = CPSS_DXCH_PCL_CFG_TBL_ACCESS_LOCAL_PORT_E;
  am.epclAccMode = CPSS_DXCH_PCL_CFG_TBL_ACCESS_LOCAL_PORT_E;
  CRP (cpssDxChPclCfgTblAccessModeSet (0, &am));

  return ST_OK;
}
