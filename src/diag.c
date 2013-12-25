#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/diag/cpssDxChDiag.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgCount.h>

#include <diag.h>
#include <debug.h>

uint32_t diag_pci_base_addr = 0;

enum status
diag_reg_read (uint32_t reg, uint32_t *val)
{
  GT_STATUS rc;

  rc = CRP (cpssDxChDiagRegRead
            (diag_pci_base_addr,
             CPSS_CHANNEL_PEX_E,
             CPSS_DIAG_PP_REG_INTERNAL_E,
             reg, val, GT_FALSE));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}

enum status
diag_bdc_set_mode (uint8_t mode)
{
  GT_STATUS rc;

  if (mode > 33)
    return ST_BAD_VALUE;

  rc = CRP (cpssDxChBrgCntDropCntrModeSet (0, mode));
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

enum status
diag_bdc_read (uint32_t *val)
{
  GT_STATUS rc;

  rc = CRP (cpssDxChBrgCntDropCntrGet (0, val));
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  default:               return ST_HEX;
  }
}
