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
  int i;

  for (i = 0; i < 8; i++) {
    CPSS_DXCH_COS_PROFILE_STC prof = {
      .dropPrecedence = CPSS_DP_GREEN_E,
      .userPriority = i,
      .trafficClass = 0,
      .dscp = 0,
      .exp = 0
    };

    CRP (cpssDxChCosProfileEntrySet (0, i, &prof));
  }

  return ST_OK;
}
