#include <sflow.h>
#include <port.h>

#include <cpss/dxCh/dxChxGen/mirror/cpssDxChStc.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/dxCh/dxChxGen/ipfix/cpssDxChIpfix.h>

enum status convert_status (
  GT_STATUS st)
{
  int new_st;
  switch (st) {
    case GT_OK:
      new_st = ST_OK;
      break;
    case GT_BAD_PARAM: /* wrong device number,port number or STC type */
      new_st = ST_BAD_FORMAT;
      break;
    case GT_HW_ERROR: /* on writing to HW error */
      new_st = ST_HW_ERROR;
      break;
    case GT_NOT_APPLICABLE_DEVICE: /* on not applicable device */
      new_st = ST_NOT_SUPPORTED;
      break;
    case GT_BAD_PTR: /* one of the parameters is NULL pointer */
    case GT_OUT_OF_RANGE: /* limit is out of range */
      new_st = ST_BAD_VALUE;
      break;
    case GT_TIMEOUT: /* after max number of retries checking if PP ready */
      new_st = ST_BUSY;
      break;
    default:
      new_st = st;
      break;
  }

  return new_st;
}

enum status sflow_set_enable (
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
          goto out;
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_EGRESS_E,
                                      gt_bool(0)));
        if (rc != GT_OK)
          goto out;
        break;
      case EGRESS:
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_EGRESS_E,
                                      gt_bool(enable)));
        if (rc != GT_OK)
          goto out;
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_INGRESS_E,
                                      gt_bool(0)));
        if (rc != GT_OK)
          goto out;
        break;
      case BOTH:
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_INGRESS_E,
                                      gt_bool(enable)));
        if (rc != GT_OK)
          goto out;
        rc = CRP(cpssDxChStcEnableSet(dev,
                                      CPSS_DXCH_STC_EGRESS_E,
                                      gt_bool(enable)));
        if (rc != GT_OK)
          goto out;
        break;
      default:
        DEBUG("%s Bad type: %d\n", __FUNCTION__, type);
        rc = GT_BAD_PARAM;
        goto out;
        break;
    }
  }

  rc = GT_OK;

out:
  return convert_status (rc);
}

enum status sflow_set_ingress_count_mode (
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
      rc = GT_BAD_PARAM;
      goto out;
      break;
  }

  for_each_dev(dev) {
    rc = CRP(cpssDxChStcIngressCountModeSet(dev, cpss_mode));
    if (rc != GT_OK)
      goto out;
  }

  rc = GT_OK;

out:
  return convert_status (rc);
}

enum status sflow_set_reload_mode (
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
    rc = GT_BAD_PARAM;
    goto out;
    break;
  }

  for_each_dev(dev) {
    switch (type) {
      case INGRESS:
        rc = CRP(cpssDxChStcReloadModeSet(dev,
                                          CPSS_DXCH_STC_INGRESS_E,
                                          cpss_mode));
        if (rc != GT_OK)
          goto out;
        break;
      case EGRESS:
        rc = CRP(cpssDxChStcReloadModeSet(dev,
                                          CPSS_DXCH_STC_EGRESS_E,
                                          cpss_mode));
        if (rc != GT_OK)
          goto out;
        break;
      case BOTH:
        rc = CRP(cpssDxChStcReloadModeSet(dev,
                                          CPSS_DXCH_STC_INGRESS_E,
                                          cpss_mode));
        if (rc != GT_OK)
          goto out;
        rc = CRP(cpssDxChStcReloadModeSet(dev,
                                          CPSS_DXCH_STC_EGRESS_E,
                                          cpss_mode));
        if (rc != GT_OK)
          goto out;
        break;
      default:
        DEBUG("%s Bad type: %d\n", __FUNCTION__, type);
        rc = GT_BAD_PARAM;
        goto out;
        break;
    }
  }

  rc = GT_OK;
out:
  return convert_status (rc);
}

enum status sflow_set_port_limit (
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
        goto out;
      rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                        port->lport,
                                        CPSS_DXCH_STC_EGRESS_E,
                                        0));
      if (rc != GT_OK)
        goto out;
      break;

    case EGRESS:
      rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                        port->lport,
                                        CPSS_DXCH_STC_EGRESS_E,
                                        limit));
      if (rc != GT_OK)
        goto out;
      rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                        port->lport,
                                        CPSS_DXCH_STC_INGRESS_E,
                                        0));
      if (rc != GT_OK)
        goto out;
      break;

    case BOTH:
      rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                        port->lport,
                                        CPSS_DXCH_STC_INGRESS_E,
                                        limit));
      if (rc != GT_OK)
        goto out;
      rc = CRP(cpssDxChStcPortLimitSet (port->ldev,
                                        port->lport,
                                        CPSS_DXCH_STC_EGRESS_E,
                                        limit));
      if (rc != GT_OK)
        goto out;
      break;

    default:
      DEBUG("%s Bad type: %d\n", __FUNCTION__, type);
      rc = GT_BAD_PARAM;
      goto out;
      break;

  }

out:
  return convert_status (rc);
}
