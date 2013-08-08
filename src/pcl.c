#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/pcl/cpssDxChPcl.h>

#include <pcl.h>
#include <port.h>
#include <log.h>
#include <vlan.h>
#include <debug.h>

#define PORT_IPCL_ID(n) (n * 2)
#define PORT_EPCL_ID(n) (n * 2 + 1)

#define STACK_ENTRIES 300
#define STACK_FIRST_ENTRY 1
#define STACK_MAX (STACK_ENTRIES + STACK_FIRST_ENTRY)

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

  rules.n_free += n;
  for (i = 0; i < n; i++)
    rules.data[--rules.sp] = nums[i];
}

enum status
pcl_vlan_xlate (port_id_t pid, vid_t from, vid_t to)
{
  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;
  struct port *port = port_ptr (pid);
  uint16_t ix[2];

  if (!(port && vlan_valid (from) && vlan_valid (to)))
    return ST_BAD_VALUE;

  if (!pcl_alloc_rules (ix, 2))
    return ST_BAD_STATE;

  memset (&act, 0, sizeof (act));
  act.pktCmd = CPSS_PACKET_CMD_FORWARD_E;
  act.actionStop = GT_TRUE;
  act.egressPolicy = GT_FALSE;
  act.vlan.modifyVlan = CPSS_PACKET_ATTRIBUTE_ASSIGN_FOR_ALL_E;
  act.vlan.nestedVlan = GT_FALSE;
  act.vlan.vlanId = to;
  act.vlan.precedence = CPSS_PACKET_ATTRIBUTE_ASSIGN_PRECEDENCE_HARD_E;

  memset (&mask, 0, sizeof (mask));
  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.common.vid = 0xFFFF;
  mask.ruleExtNotIpv6.common.sourcePort = 0xFF;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pid);
  rule.ruleExtNotIpv6.common.vid = from;
  rule.ruleExtNotIpv6.common.sourcePort = port->lport;

  CRP (cpssDxChPclRuleSet
       (port->ldev,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
        ix[0],
        0,
        &mask,
        &rule,
        &act));

  memset (&mask, 0, sizeof (mask));
  mask.ruleEgrExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleEgrExtNotIpv6.common.vid = 0xFFFF;

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
        ix[1],
        0,
        &mask,
        &rule,
        &act));

  return ST_OK;
}

enum status
pcl_vlan_tunnel (port_id_t pid, vid_t from, vid_t to)
{
  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;
  struct port *port = port_ptr (pid);
  uint16_t ix[1];

  if (!(port && vlan_valid (from) && vlan_valid (to)))
    return ST_BAD_VALUE;

  if (!pcl_alloc_rules (ix, 1))
    return ST_BAD_STATE;

  memset (&act, 0, sizeof (act));
  act.pktCmd = CPSS_PACKET_CMD_FORWARD_E;
  act.actionStop = GT_TRUE;
  act.egressPolicy = GT_FALSE;
  act.vlan.modifyVlan = CPSS_PACKET_ATTRIBUTE_ASSIGN_FOR_ALL_E;
  act.vlan.nestedVlan = GT_TRUE;
  act.vlan.vlanId = to;
  act.vlan.precedence = CPSS_PACKET_ATTRIBUTE_ASSIGN_PRECEDENCE_HARD_E;

  memset (&mask, 0, sizeof (mask));
  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.common.vid = 0xFFFF;
  mask.ruleExtNotIpv6.common.sourcePort = 0xFF;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pid);
  rule.ruleExtNotIpv6.common.vid = from;
  rule.ruleExtNotIpv6.common.sourcePort = port->lport;

  CRP (cpssDxChPclRuleSet
       (port->ldev,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
        ix[0],
        0,
        &mask,
        &rule,
        &act));

  return ST_OK;
}


enum status
pcl_port_setup (port_id_t pid)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  CRP (cpssDxChPclPortIngressPolicyEnable
       (port->ldev, port->lport, GT_TRUE));
  CRP (cpssDxChPclPortLookupCfgTabAccessModeSet
       (port->ldev, port->lport,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        0,
        CPSS_DXCH_PCL_PORT_LOOKUP_CFG_TAB_ACC_MODE_BY_PORT_E));

  return ST_OK;
}

static void
pcl_setup_lbd_trap (void)
{
  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;

  memset (&mask, 0, sizeof (mask));
  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
  mask.ruleExtNotIpv6.etherType = 0xFFFF;
  mask.ruleExtNotIpv6.l2Encap = 0xFF;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtNotIpv6.common.pclId = 1;
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
        0,
        0,
        &mask,
        &rule,
        &act));
}

enum status
pcl_enable_lbd_trap (port_id_t pid)
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
    .enableLookup  = GT_TRUE,
    .pclId         = 1,
    .dualLookup    = GT_FALSE,
    .pclIdL01      = 0,
    .groupKeyTypes = {
      .nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L4_E
    }
  };
  GT_STATUS rc;

  rc = CRP (cpssDxChPclCfgTblSet
            (0, &iface,
             CPSS_PCL_DIRECTION_INGRESS_E,
             CPSS_PCL_LOOKUP_0_E,
             &lc));
  switch (rc) {
  case GT_OK:        return ST_OK;
  case GT_BAD_PARAM: return ST_BAD_VALUE;
  case GT_HW_ERROR:  return ST_HW_ERROR;
  default:           return ST_HEX;
  }
}

enum status
pcl_cpss_lib_init (void)
{
  CPSS_DXCH_PCL_CFG_TBL_ACCESS_MODE_STC am;

  pcl_init_rules ();

  CRP (cpssDxChPclInit (0));
  CRP (cpssDxChPclIngressPolicyEnable (0, GT_TRUE));
  memset (&am, 0, sizeof (am));
  am.ipclAccMode = CPSS_DXCH_PCL_CFG_TBL_ACCESS_LOCAL_PORT_E;
  CRP (cpssDxChPclCfgTblAccessModeSet (0, &am));
  pcl_setup_lbd_trap ();

  return ST_OK;
}
