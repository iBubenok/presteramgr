#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <presteramgr.h>
#include <debug.h>

#include <cpss/generic/port/cpssPortCtrl.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>


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
