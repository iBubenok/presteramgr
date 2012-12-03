#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/pcl/cpssDxChPcl.h>

#include <pcl.h>
#include <port.h>
#include <log.h>
#include <debug.h>


enum status
pcl_port_enable (port_id_t pid, int enable)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  DEBUG ("enable PCL for port %d", pid);
  rc = CRP (cpssDxChPclPortIngressPolicyEnable
            (port->ldev, port->lport, !!enable));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}

void
pcl_test (void)
{
  struct port *port = port_ptr (1);
  CPSS_INTERFACE_INFO_STC iface = {
    .type = CPSS_INTERFACE_PORT_E,
    .devPort = {
      .devNum = port->ldev,
      .portNum = port->lport
    }
  };
  CPSS_DXCH_PCL_LOOKUP_CFG_STC lc;
  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;

  CRP (cpssDxChPclPortLookupCfgTabAccessModeSet
       (port->ldev, port->lport,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        0,
        CPSS_DXCH_PCL_PORT_LOOKUP_CFG_TAB_ACC_MODE_BY_PORT_E));

  lc.enableLookup = GT_TRUE;
  lc.pclId = 1;
  lc.dualLookup = GT_FALSE;
  lc.pclIdL01 = 0;
  lc.groupKeyTypes.nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_STD_NOT_IP_E;
  lc.groupKeyTypes.ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E;
  lc.groupKeyTypes.ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_STD_IPV6_DIP_E;

  CRP (cpssDxChPclCfgTblSet
       (0, &iface,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        &lc));

  memset (&mask, 0, sizeof (mask));
  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.commonExt.l4Byte2 = 0xFF;
  mask.ruleExtNotIpv6.commonExt.l4Byte3 = 0xFF;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtNotIpv6.common.pclId = 1;
  rule.ruleExtNotIpv6.commonExt.l4Byte2 = 0;
  rule.ruleExtNotIpv6.commonExt.l4Byte3 = 67;

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

  rule.ruleExtNotIpv6.commonExt.l4Byte3 = 68;
  act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E;

  CRP (cpssDxChPclRuleSet
       (0,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
        2,
        0,
        &mask,
        &rule,
        &act));
}

enum status
pcl_cpss_lib_init (void)
{
  CPSS_DXCH_PCL_CFG_TBL_ACCESS_MODE_STC am;

  CRP (cpssDxChPclInit (0));
  CRP (cpssDxChPclIngressPolicyEnable (0, GT_TRUE));
  memset (&am, 0, sizeof (am));
  am.ipclAccMode = CPSS_DXCH_PCL_CFG_TBL_ACCESS_LOCAL_PORT_E;
  CRP (cpssDxChPclCfgTblAccessModeSet (0, &am));

  return ST_OK;
}
