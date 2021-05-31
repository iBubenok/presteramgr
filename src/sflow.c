#include <sflow.h>
#include <port.h>

#include <cpss/dxCh/dxChxGen/mirror/cpssDxChStc.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/dxCh/dxChxGen/ipfix/cpssDxChIpfix.h>

#define for_each_port(p) for (p = 1; p <= nports; p++)

GT_STATUS sflow_set_egress_enable(int enable)
{
  DEBUG("%s\n", __FUNCTION__);

  int dev, rc;
  for_each_dev(dev) {
    rc = CRP(cpssDxChStcEnableSet(dev, CPSS_DXCH_STC_EGRESS_E,  gt_bool(enable)));
    if (rc != GT_OK)
      return rc;
  }

  return GT_OK;
}

GT_STATUS sflow_set_ingress_enable(int enable)
{
  DEBUG("%s\n", __FUNCTION__);

  int dev, rc;
  for_each_dev(dev) {
    rc = CRP(cpssDxChStcEnableSet(dev, CPSS_DXCH_STC_INGRESS_E,  gt_bool(enable)));
    if (rc != GT_OK)
      return rc;
  }

  return GT_OK;
}

GT_STATUS sflow_set_ingress_count_mode()
{
  DEBUG("%s\n", __FUNCTION__);

  int dev, rc;
  for_each_dev(dev) {
    // TODO: мод только 1 реализован. все входящие пакеты;
    rc = CRP(cpssDxChStcIngressCountModeSet(dev, CPSS_DXCH_STC_COUNT_ALL_PACKETS_E));
    if (rc != GT_OK)
      return rc;
  }

  return GT_OK;
}

GT_STATUS sflow_set_egress_reload_mode()
{
  DEBUG("%s\n", __FUNCTION__);

  int dev, rc;
  for_each_dev(dev) {
    // TODO: мод только 1 реализован.
    rc = CRP(cpssDxChStcReloadModeSet(dev,
                                      CPSS_DXCH_STC_EGRESS_E,
                                      CPSS_DXCH_STC_COUNT_RELOAD_CONTINUOUS_E));
    if (rc != GT_OK)
      return rc;
  }

  return GT_OK;
}

GT_STATUS sflow_set_ingress_reload_mode()
{
  DEBUG("%s\n", __FUNCTION__);

  int dev, rc;
  for_each_dev(dev) {
    // TODO: мод только 1 реализован.
    rc = CRP(cpssDxChStcReloadModeSet(dev,
                                      CPSS_DXCH_STC_INGRESS_E,
                                      CPSS_DXCH_STC_COUNT_RELOAD_CONTINUOUS_E));
    if (rc != GT_OK)
      return rc;
  }

  return GT_OK;
}

GT_STATUS sflow_set_egress_port_limit()
{
  DEBUG("%s\n", __FUNCTION__);

  int rc;
  port_id_t  pid;
  for_each_port(pid) {
    struct port *port = port_ptr (pid);
    // TODO: количество и порты
    rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                      port->lport,
                                      CPSS_DXCH_STC_EGRESS_E,
                                      2));
    if (rc != GT_OK)
      return rc;
  }

  return GT_OK;
}

GT_STATUS sflow_set_ingress_port_limit()
{
  DEBUG("%s\n", __FUNCTION__);

  int rc;
  port_id_t  pid;
  for_each_port(pid) {
    struct port *port = port_ptr (pid);
    // TODO: количество и порты
    rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                      port->lport,
                                      CPSS_DXCH_STC_INGRESS_E,
                                      2));
    if (rc != GT_OK)
      return rc;
  }

  return GT_OK;
}

void 
pcl_get_sflow_count() // v2
{
  DEBUG("%s\n", __FUNCTION__);

  port_id_t pid;
  GT_U32 count = -100;

  for_each_port (pid) {
    struct port *port = port_ptr (pid);

    CRP(cpssDxChStcPortSampledPacketsCntrGet (port->ldev, port->lport, CPSS_DXCH_STC_INGRESS_E, &count));
    if(count != 0) {
      DEBUG("d  count ingress = %d\n",      count);
      DEBUG("u  count ingress = %u\n",      count);
    }

    CRP(cpssDxChStcPortSampledPacketsCntrGet (port->ldev, port->lport, CPSS_DXCH_STC_EGRESS_E,  &count));
    if(count != 0) {
      DEBUG("d  count egress = %d\n",       count);
      DEBUG("u  count egress = %u\n",       count);
    }

    CRP(cpssDxChStcPortCountdownCntrGet      (port->ldev, port->lport, CPSS_DXCH_STC_EGRESS_E,  &count));
    if(count != 0) {
      DEBUG("d  count down egress = %d\n",  count);
      DEBUG("u  count down egress = %u\n",  count);
    }

    CRP(cpssDxChStcPortCountdownCntrGet      (port->ldev, port->lport, CPSS_DXCH_STC_INGRESS_E, &count));
    if(count != 0) {
      DEBUG("d  count down ingress = %d\n", count);
      DEBUG("u  count down ingress = %u\n", count);
    }
  }

  DEBUG("%s eeeeeeeeeend\n", __FUNCTION__);
}
