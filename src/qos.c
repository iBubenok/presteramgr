#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/cos/cpssDxChCos.h>
#include <cpss/generic/port/cpssPortTx.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortTx.h>

#include <presteramgr.h>
#include <debug.h>
#include <qos.h>
#include <port.h>

int mls_qos_trust = 0;
const uint8_t qos_default_wrr_weights[8] = {
  1, 2, 4, 8, 16, 32, 64, 128
};

static int prioq_num = 8;

static enum status
__qos_set_prioq_num (int num, int prof)
{
  int i;

  if (num < 0 || num > 8)
    return ST_BAD_VALUE;

  for (i = 0; i < 8 - num; i++) {
    DEBUG ("set %d:%d to WRR\r\n", prof, i);
    CRP (cpssDxChPortTxQArbGroupSet
         (0, i, CPSS_PORT_TX_WRR_ARB_GROUP_0_E, prof));
  }
  for ( ; i < 8; i++) {
    DEBUG ("set %d:%d to SP\r\n", prof, i);
    CRP (cpssDxChPortTxQArbGroupSet
         (0, i, CPSS_PORT_TX_SP_ARB_GROUP_E, prof));
  }

  return ST_OK;
}

enum status
qos_set_mls_qos_trust (int trust)
{
  port_id_t i;

  mls_qos_trust = trust;
  for (i = 1; i <= nports; i++)
    port_update_qos_trust (port_ptr (i));

  return ST_OK;
}

enum status
qos_set_port_mls_qos_trust_cos (port_id_t pid, bool_t trust)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  port->trust_cos = !!trust;
  return port_update_qos_trust (port);
}

enum status
qos_set_port_mls_qos_trust_dscp (port_id_t pid, bool_t trust)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  port->trust_dscp = !!trust;
  return port_update_qos_trust (port);
}

enum status
qos_start (void)
{
  struct dscp_map dscp_map[64];
  queue_id_t cos_map[8] = { 2, 0, 1, 3, 4, 5, 6, 7 };
  int i;

  for (i = 0; i < 8; i++) {
    CPSS_DXCH_COS_PROFILE_STC prof = {
      .dropPrecedence = CPSS_DP_GREEN_E,
      .userPriority = 0,
      .trafficClass = i,
      .dscp = 0,
      .exp = 0
    };

    CRP (cpssDxChCosProfileEntrySet (0, i, &prof));
  }

  CRP (cpssDxChPortTxWrrGlobalParamSet
       (0,
        CPSS_PORT_TX_WRR_BYTE_MODE_E,
        CPSS_PORT_TX_WRR_MTU_2K_E));

  for (i = 0; i < 64; i++) {
    dscp_map[i].dscp = i;
    dscp_map[i].queue = i / 8;
  }
  qos_set_dscp_prio (64, dscp_map);

  qos_set_cos_prio (cos_map);

  __qos_set_prioq_num (1, CPSS_PORT_TX_SCHEDULER_PROFILE_1_E);
  __qos_set_prioq_num (1, CPSS_PORT_TX_SCHEDULER_PROFILE_3_E);
  qos_set_prioq_num (8);
  qos_set_wrr_queue_weights (qos_default_wrr_weights);

  return ST_OK;
}

enum status
qos_set_dscp_prio (int n, const struct dscp_map *map)
{
  int i;

  for (i = 0; i < n; ++i)
    if (map[i].dscp > 63 || map[i].queue > 7)
      return ST_BAD_VALUE;

  for (i = 0; i < n; ++i)
    CRP (cpssDxChCosDscpToProfileMapSet (0, map[i].dscp, map[i].queue));

  return ST_OK;
}

enum status
qos_set_cos_prio (const queue_id_t *map)
{
  int i;

  for (i = 0; i < 8; i++)
    if (map[i] > 7)
      return ST_BAD_VALUE;

  for (i = 0; i < 8; i++) {
    CRP (cpssDxChCosUpCfiDeiToProfileMapSet (0, 0, i, 0, map[i]));
    CRP (cpssDxChCosUpCfiDeiToProfileMapSet (0, 0, i, 1, map[i]));
  }

  return ST_OK;
}

enum status
qos_set_prioq_num (int num)
{
  if (num < 0 || num > 8)
    return ST_BAD_VALUE;

  prioq_num = num;
  return __qos_set_prioq_num (num, CPSS_PORT_TX_SCHEDULER_PROFILE_2_E);
}

enum status
qos_set_wrr_queue_weights (const uint8_t *weights)
{
  int i;

  for (i = 0; i < 8; i++)
    CRP (cpssDxChPortTxQWrrProfileSet
         (0, i, weights[i], CPSS_PORT_TX_SCHEDULER_PROFILE_2_E));

  return ST_OK;
}
