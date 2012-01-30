#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgGen.h>

#include <wnct.h>
#include <debug.h>
#include <utils.h>

static uint8_t protos[256];

enum status
wnct_enable_proto (uint8_t proto, int enable)
{
  GT_STATUS rc;

  enable = !!enable;
  if (protos[proto] == enable)
    return ST_OK;

  rc = CRP (cpssDxChBrgGenIeeeReservedMcastProtCmdSet
            (0, 0, proto, enable
             ? CPSS_PACKET_CMD_TRAP_TO_CPU_E
             : CPSS_PACKET_CMD_FORWARD_E));
  ON_GT_ERROR (rc, goto out);

  protos[proto] = enable;

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
wnct_start (void)
{
  GT_STATUS rc;

  memset (protos, 0, sizeof (protos));

  rc = CRP (cpssDxChBrgGenIeeeReservedMcastTrapEnable (0, GT_TRUE));
  ON_GT_ERROR (rc, goto gt_error);

  return wnct_enable_proto (WNCT_STP, 1);

 gt_error:
  switch (rc) {
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}