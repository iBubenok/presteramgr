#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/dxCh/dxChxGen/cos/cpssDxChCos.h>

#include <presteramgr.h>
#include <debug.h>
#include <qos.h>
#include <port.h>

int mls_qos_trust = 0;

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

  for (i = 0; i < 64; i++) {
    dscp_map[i].dscp = i;
    dscp_map[i].queue = i / 8;
  }
  qos_set_dscp_prio (64, dscp_map);

  qos_set_cos_prio (cos_map);

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
