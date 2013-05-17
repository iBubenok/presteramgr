#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/pcl/cpssDxChPcl.h>

#include <pcl.h>
#include <port.h>
#include <dev.h>
#include <log.h>
#include <utils.h>
#include <debug.h>


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
}

enum status
pcl_enable_lbd_trap (port_id_t pid)
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
  CPSS_DXCH_PCL_LOOKUP_CFG_STC lc = {
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
  GT_STATUS rc;

  rc = CRP (cpssDxChPclCfgTblSet
            (0, &iface,
             CPSS_PCL_DIRECTION_EGRESS_E,
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

  CRP (cpssDxChPclInit (0));
  CRP (cpssDxChPclIngressPolicyEnable (0, GT_TRUE));
  CRP (cpssDxCh2PclEgressPolicyEnable (0, GT_TRUE));
  memset (&am, 0, sizeof (am));
  am.ipclAccMode = CPSS_DXCH_PCL_CFG_TBL_ACCESS_LOCAL_PORT_E;
  am.epclAccMode = CPSS_DXCH_PCL_CFG_TBL_ACCESS_LOCAL_PORT_E;
  CRP (cpssDxChPclCfgTblAccessModeSet (0, &am));
  pcl_setup_lbd_trap ();
  pcl_setup_mc_drop ();

  return ST_OK;
}
