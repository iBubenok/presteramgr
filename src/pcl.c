#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/pcl/cpssDxChPcl.h>

#include <pcl.h>
#include <port.h>
#include <dev.h>
#include <log.h>
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
pcl_cpss_lib_init (void)
{
  CPSS_DXCH_PCL_CFG_TBL_ACCESS_MODE_STC am;

  CRP (cpssDxChPclInit (0));
  CRP (cpssDxChPclIngressPolicyEnable (0, GT_TRUE));
  memset (&am, 0, sizeof (am));
  am.ipclAccMode = CPSS_DXCH_PCL_CFG_TBL_ACCESS_LOCAL_PORT_E;
  CRP (cpssDxChPclCfgTblAccessModeSet (0, &am));
  pcl_setup_lbd_trap ();

  return ST_OK;
}
