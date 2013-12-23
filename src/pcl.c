#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/pcl/cpssDxChPcl.h>

#include <pcl.h>
#include <port.h>
#include <dev.h>
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

  for (i = 0; i < STACK_ENTRIES; i++)
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

static inline int
vt_key (int pid, int from)
{
  return (pid << 12) | from;
}

static inline int
vt_key_vid (int key)
{
  return key & 0xFFF;
}

static inline int
vt_key_pid (int key)
{
  return (key >> 12) & 0xFFFF;
}

static struct vt_ix *vt_ix = NULL;

static struct vt_ix *
get_vt_ix (port_id_t pid, vid_t from, vid_t to, int tunnel, int alloc)
{
  int key = vt_key (pid, from);
  struct vt_ix *ix;

  HASH_FIND_INT (vt_ix, &key, ix);
  if (alloc && !ix) {
    ix = calloc (1, sizeof (struct vt_ix));
    ix->key = key;
    if (from) {
      if (!pcl_alloc_rules (ix->ix, 2)) {
        free (ix);
        return NULL;
      }
    } else {
      /* Default rule for port. */
      ix->ix[0] = PORT_IPCL_DEF_IX (pid);
    }
    HASH_ADD_INT (vt_ix, key, ix);
  }

  if (ix) {
    ix->tunnel = tunnel;
    ix->to = to;
  }

  return ix;
}

static void
invalidate_vt_ix (struct vt_ix *ix)
{
  CRP (cpssDxChPclRuleInvalidate (0, CPSS_PCL_RULE_SIZE_EXT_E, ix->ix[0]));
  if (vt_key_vid (ix->key))
    CRP (cpssDxChPclRuleInvalidate (0, CPSS_PCL_RULE_SIZE_EXT_E, ix->ix[1]));
}

static void
free_vt_ix (struct vt_ix *ix)
{
  invalidate_vt_ix (ix);
  pcl_free_rules (ix->ix, 2);
  HASH_DEL (vt_ix, ix);
  free (ix);
}

enum status
pcl_remove_vt (port_id_t pid, vid_t from, int tunnel)
{
  struct vt_ix *ix = get_vt_ix (pid, from, 0, tunnel, 0);
  struct port *port = port_ptr (pid);

  if (!ix)
    return ST_DOES_NOT_EXIST;

  if (!port)
    return ST_BAD_VALUE;

  free_vt_ix (ix);

  CRP (cpssDxCh2EgressPclPacketTypesSet
       (port->ldev, port->lport,
        CPSS_DXCH_PCL_EGRESS_PKT_NON_TS_E,
        GT_TRUE));
  CRP (cpssDxChPclPortLookupCfgTabAccessModeSet
       (port->ldev, port->lport,
        CPSS_PCL_DIRECTION_EGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        0,
        CPSS_DXCH_PCL_PORT_LOOKUP_CFG_TAB_ACC_MODE_BY_PORT_E));

  return ST_OK;
}

static void
pcl_enable_vt (struct vt_ix *ix, int enable)
{
  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;
  struct port *port;
  vid_t from, to;
  port_id_t pid;
  int tunnel;

  invalidate_vt_ix (ix);
  if (!enable)
    return;

  from   = vt_key_vid (ix->key);
  to     = ix->to;
  pid    = vt_key_pid (ix->key);
  port   = port_ptr (pid);
  tunnel = ix->tunnel;

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

  if (from && to && !tunnel) {
    memset (&mask, 0, sizeof (mask));
    mask.ruleEgrExtNotIpv6.common.pclId = 0xFFFF;
    mask.ruleEgrExtNotIpv6.common.vid = 0xFFFF;

    memset (&rule, 0, sizeof (rule));
    rule.ruleEgrExtNotIpv6.common.pclId = PORT_EPCL_ID (pid);
    rule.ruleEgrExtNotIpv6.common.vid = to;

    memset (&act, 0, sizeof (act));
    act.pktCmd = CPSS_PACKET_CMD_FORWARD_E;
    act.actionStop = GT_FALSE;
    act.egressPolicy = GT_TRUE;
    act.vlan.modifyVlan = CPSS_PACKET_ATTRIBUTE_ASSIGN_FOR_TAGGED_E;
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
}

void
pcl_port_enable_vt (port_id_t pid, int enable)
{
  struct vt_ix *ix, *tmp;

  HASH_ITER (hh, vt_ix, ix, tmp) {
    if (vt_key_pid (ix->key) == pid)
      pcl_enable_vt (ix, enable);
  }
}

void
pcl_port_clear_vt (port_id_t pid)
{
  struct vt_ix *ix, *tmp;

  HASH_ITER (hh, vt_ix, ix, tmp) {
    if (pid == ALL_PORTS || vt_key_pid (ix->key) == pid)
      free_vt_ix (ix);
  }
}

enum status
pcl_setup_vt (port_id_t pid, vid_t from, vid_t to, int tunnel, int enable)
{
  struct vt_ix *ix;

  ix = get_vt_ix (pid, from, to, tunnel, enable);
  if (!ix)
    return ST_BAD_STATE;

  if (enable)
    pcl_enable_vt (ix, 1);

  return ST_OK;
}

static void
pcl_setup_mc_drop (void)
{
  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;

  memset (&act, 0, sizeof (act));
  act.pktCmd = CPSS_PACKET_CMD_DROP_HARD_E;
  act.actionStop = GT_TRUE;

  memset (&mask, 0, sizeof (mask));
  mask.ruleEgrExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleEgrExtNotIpv6.common.isL2Valid = 0xFF;
  mask.ruleEgrExtNotIpv6.macDa.arEther[0] = 0x01;

  memset (&rule, 0, sizeof (rule));
  rule.ruleEgrExtNotIpv6.common.pclId = 2;
  rule.ruleEgrExtNotIpv6.common.isL2Valid = 1;
  rule.ruleEgrExtNotIpv6.macDa.arEther[0] = 0x01;

  CRP (cpssDxChPclRuleSet
       (0,
        CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
        1,
        0,
        &mask,
        &rule,
        &act));

  memset (&mask, 0, sizeof (mask));
  mask.ruleEgrExtIpv6L2.common.pclId = 0xFFFF;
  mask.ruleEgrExtIpv6L2.common.isL2Valid = 0xFF;
  mask.ruleEgrExtIpv6L2.macDa.arEther[0] = 0x01;

  memset (&rule, 0, sizeof (rule));
  rule.ruleEgrExtIpv6L2.common.pclId = 2;
  rule.ruleEgrExtIpv6L2.common.isL2Valid = 1;
  rule.ruleEgrExtIpv6L2.macDa.arEther[0] = 0x01;

  CRP (cpssDxChPclRuleSet
       (0,
        CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_IPV6_L2_E,
        2,
        0,
        &mask,
        &rule,
        &act));

  memset (&mask, 0, sizeof (mask));
  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
  mask.ruleExtNotIpv6.macDa.arEther[0] = 0x01;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtNotIpv6.common.pclId = 2;
  rule.ruleExtNotIpv6.common.isL2Valid = 1;
  rule.ruleExtNotIpv6.macDa.arEther[0] = 0x01;

  CRP (cpssDxChPclRuleSet
       (0,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
        3,
        0,
        &mask,
        &rule,
        &act));

  memset (&mask, 0, sizeof (mask));
  mask.ruleExtIpv6L2.common.pclId = 0xFFFF;
  mask.ruleExtIpv6L2.common.isL2Valid = 0xFF;
  mask.ruleExtIpv6L2.macDa.arEther[0] = 0x01;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtIpv6L2.common.pclId = 2;
  rule.ruleExtIpv6L2.common.isL2Valid = 1;
  rule.ruleExtIpv6L2.macDa.arEther[0] = 0x01;

  CRP (cpssDxChPclRuleSet
       (0,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L2_E,
        4,
        0,
        &mask,
        &rule,
        &act));
}

enum status
pcl_enable_port (port_id_t pid, int enable)
{
  struct port *port = port_ptr (pid);
  CPSS_INTERFACE_INFO_STC iface = {
    .type    = CPSS_INTERFACE_PORT_E,
    .devPort = {
      .devNum  = phys_dev (port->ldev),
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
pcl_enable_mc_drop (port_id_t pid, int enable)
{
  struct port *port = port_ptr (pid);
  CPSS_INTERFACE_INFO_STC iface = {
    .type    = CPSS_INTERFACE_PORT_E,
    .devPort = {
      .devNum  = phys_dev (port->ldev),
      .portNum = port->lport
    }
  };
  CPSS_DXCH_PCL_LOOKUP_CFG_STC elc = {
    .enableLookup  = gt_bool (enable),
    .pclId         = 2,
    .dualLookup    = GT_FALSE,
    .pclIdL01      = 0,
    .groupKeyTypes = {
      .nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
      .ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
      .ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_IPV6_L2_E
    }
  };
  CPSS_DXCH_PCL_LOOKUP_CFG_STC ilc = {
    .enableLookup  = gt_bool (enable),
    .pclId         = 2,
    .dualLookup    = GT_FALSE,
    .pclIdL01      = 0,
    .groupKeyTypes = {
      .nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L2_E
    }
  };

  DEBUG ("%s mc drop\r\n", enable ? "enabling" : "disabling");

  CRP (cpssDxChPclCfgTblSet
       (0, &iface,
        CPSS_PCL_DIRECTION_EGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        &elc));
  CRP (cpssDxChPclCfgTblSet
       (0, &iface,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        &ilc));

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

  pcl_setup_mc_drop ();

  return ST_OK;
}
