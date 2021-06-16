#include <sflow.h>
#include <port.h>

#include <cpss/dxCh/dxChxGen/mirror/cpssDxChStc.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/dxCh/dxChxGen/ipfix/cpssDxChIpfix.h>

GT_STATUS sflow_set_enable (
  sflow_type_t type, 
  int enable)
{
  DEBUG("%s type: %d enable: %d\n", __FUNCTION__, type, enable);

  int dev, rc;
  for_each_dev(dev) {
    switch (type) {
      case INGRESS:
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_INGRESS_E,
                                      gt_bool(enable)));
        if (rc != GT_OK)
          return rc;
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_EGRESS_E,
                                      gt_bool(0)));
        if (rc != GT_OK)
          return rc;
        break;
      case EGRESS:
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_EGRESS_E,
                                      gt_bool(enable)));
        if (rc != GT_OK)
          return rc;
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_INGRESS_E,
                                      gt_bool(0)));
        if (rc != GT_OK)
          return rc;
        break;
      case BOTH:
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_INGRESS_E,
                                      gt_bool(enable)));
        if (rc != GT_OK)
          return rc;
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_EGRESS_E,
                                      gt_bool(enable)));
        if (rc != GT_OK)
          return rc;
        break;
      default:
        DEBUG("%s Bad type: %d\n", __FUNCTION__, type);
        return GT_BAD_PARAM;
        break;
    }
  }

  return GT_OK;
}

GT_STATUS sflow_set_ingress_count_mode (
  sflow_count_mode_t mode)
{
  DEBUG("%s mode: %d\n", __FUNCTION__, mode);

  int dev, rc;
  CPSS_DXCH_STC_COUNT_MODE_ENT cpss_mode;

  switch (mode) {
    case ALL_PACKETS:
      cpss_mode = CPSS_DXCH_STC_COUNT_ALL_PACKETS_E;
      break;
    case NON_DROPPED_PACKETS:
      cpss_mode = CPSS_DXCH_STC_COUNT_NON_DROPPED_PACKETS_E;
      break;
    default:
      DEBUG("%s Bad mode: %d\n", __FUNCTION__, mode);
      return GT_BAD_PARAM;
      break;
  }

  for_each_dev(dev) {
    rc = CRP(cpssDxChStcIngressCountModeSet(dev, cpss_mode));
    if (rc != GT_OK)
      return rc;
  }

  return GT_OK;
}

GT_STATUS sflow_set_reload_mode (
  sflow_type_t type, 
  sflow_count_reload_mode_t mode)
{
  DEBUG("%s type: %d mode: %d\n", __FUNCTION__, type, mode);

  int dev, rc;
  CPSS_DXCH_STC_COUNT_RELOAD_MODE_ENT cpss_mode;

  switch (mode) {
  case RELOAD_CONTINUOUS:
    cpss_mode = CPSS_DXCH_STC_COUNT_RELOAD_CONTINUOUS_E;
    break;
  case RELOAD_TRIGGERED:
    cpss_mode = CPSS_DXCH_STC_COUNT_RELOAD_TRIGGERED_E;
    break;
  default:
    DEBUG("%s Bad mode: %d\n", __FUNCTION__, mode);
    return GT_BAD_PARAM;
    break;
  }

  for_each_dev(dev) {
    switch (type) {
      case INGRESS:
        rc = CRP(cpssDxChStcReloadModeSet(dev,
                                          CPSS_DXCH_STC_INGRESS_E,
                                          cpss_mode));
        if (rc != GT_OK)
          return rc;
        break;
      case EGRESS:
        rc = CRP(cpssDxChStcReloadModeSet(dev,
                                          CPSS_DXCH_STC_EGRESS_E,
                                          cpss_mode));
        if (rc != GT_OK)
          return rc;
        break;
      case BOTH:
        rc = CRP(cpssDxChStcReloadModeSet(dev,
                                          CPSS_DXCH_STC_INGRESS_E,
                                          cpss_mode));
        if (rc != GT_OK)
          return rc;
        rc = CRP(cpssDxChStcReloadModeSet(dev,
                                          CPSS_DXCH_STC_EGRESS_E,
                                          cpss_mode));
        if (rc != GT_OK)
          return rc;
        break;
      default:
        DEBUG("%s Bad type: %d\n", __FUNCTION__, type);
        return GT_BAD_PARAM;
        break;
    }
  }

  return GT_OK;
}

GT_STATUS sflow_set_port_limit (
  port_id_t pid,
  sflow_type_t type,
  uint32_t limit)
{
  DEBUG("%s pid: %d type: %d limit: %d\n", __FUNCTION__, pid, type, limit);

  int rc;
  struct port *port = port_ptr (pid);

  switch (type) {
    case INGRESS:
      rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                        port->lport,
                                        CPSS_DXCH_STC_INGRESS_E,
                                        limit));
      if (rc != GT_OK)
        return rc;
      break;
    case EGRESS:
      rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                        port->lport,
                                        CPSS_DXCH_STC_EGRESS_E,
                                        limit));
      if (rc != GT_OK)
        return rc;
      break;
    case BOTH:
      rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                        port->lport,
                                        CPSS_DXCH_STC_INGRESS_E,
                                        limit));
      if (rc != GT_OK)
        return rc;
      rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                        port->lport,
                                        CPSS_DXCH_STC_EGRESS_E,
                                        limit));
      if (rc != GT_OK)
        return rc;
      break;
    default:
      DEBUG("%s Bad type: %d\n", __FUNCTION__, type);
      return GT_BAD_PARAM;
      break;
  }

  return GT_OK;
}

void 
pcl_get_sflow_count() // v2
{
  DEBUG("%s\n", __FUNCTION__);

  port_id_t pid = 1;
  GT_U32 count = -100;

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

  DEBUG("%s eeeeeeeeeend\n", __FUNCTION__);
}
