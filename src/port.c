#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <presteramgr.h>
#include <debug.h>

#include <cpss/generic/port/cpssPortCtrl.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>


GT_STATUS
port_set_sgmii_mode (GT_U8 dev, GT_U8 port)
{
  CRPR (cpssDxChPortInterfaceModeSet
        (dev, port, CPSS_PORT_INTERFACE_MODE_SGMII_E));
  CRPR (cpssDxChPortSpeedSet (dev, port, CPSS_PORT_SPEED_1000_E));
  CRPR (cpssDxChPortSerdesPowerStatusSet
        (dev, port, CPSS_PORT_DIRECTION_BOTH_E, 0x01, GT_TRUE));
  CRPR (cpssDxChPortInbandAutoNegEnableSet (dev, port, GT_TRUE));

  return GT_OK;
}

int
port_exists (GT_U8 dev, GT_U8 port)
{
  return CPSS_PORTS_BMP_IS_PORT_SET_MAC
    (&(PRV_CPSS_PP_MAC (dev)->existingPorts), port);
}
