#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <variant.h>

#if defined (VARIANT_GE)

#include <cpssdefs.h>

#include <cpssDriver/pp/hardware/cpssDriverPpHw.h>
#include <cpss/dxCh/dxChxGen/phy/cpssDxChPhySmi.h>
#include <cpss/generic/smi/cpssGenSmi.h>
#include <cpss/dxCh/dxChxGen/config/private/prvCpssDxChInfo.h>

#include <gtOs/gtOsIo.h>
#include <gtOs/gtOsTimer.h>

#include <utils.h>
#include <log.h>
#include <sysdeps.h>
#include <debug.h>

#include <qt2025-phy.h>
#include <qt2025-phy-fw.h>

#undef SHOW_XG_PHY_HEARTBEAT
#undef SHOW_HG_PHY_FW_VERSION

/*******************************************************************************
 * Global variables
 ******************************************************************************/

static GT_U32 *xsmiAddrPtr = NULL;
static int n_port_groups = 0;
static int n_ports = 0;
static int check_lioncub = -1;

/*******************************************************************************
* qt2025PhyCfgPhaseX
*
* DESCRIPTION:
*       Init PHY for Lion 48 - init done by 7 different phases
*
* INPUTS:
*       dev - device number
*       phase - phase number (1 to 7)
*       phyDataPhase - pointer to phy init structure
*
* OUTPUTS:
*       None
*
* RETURNS:
*       GT_OK   - on success,
*       GT_FAIL - otherwise.
*
* COMMENTS:
*       None.
*
*******************************************************************************/
static GT_STATUS
qt2025PhyCfgPhaseX (GT_U8 dev, GT_U32 phase, GT_U32 fw_size, GT_QT2025_FW *fw)
{
  GT_U8 phyAddr, portGroup, port, localPortNum;
  GT_U32 i;
  GT_U16 rxLossVal, txLossVal;

  if ((xsmiAddrPtr == NULL) ||
      (n_port_groups == 0) ||
      (n_ports == 0) ||
      (check_lioncub == -1))
    return GT_NOT_INITIALIZED;

  for (portGroup = 0; portGroup < n_port_groups; portGroup++) {
    for (localPortNum = 0; localPortNum < n_ports; localPortNum++) {
      port = localPortNum + (16 * portGroup);
      phyAddr = xsmiAddrPtr[port];

      for (i = 0; i < fw_size; i++) {
        /* set rx/tx loss for phase 1 only */
        if ((phase == 1) && (fw[i].regAddr == 0x27)) {
          rxLossVal = reg27RxTxLossArray_Lion_B0[localPortNum].rxLoss;
          txLossVal = reg27RxTxLossArray_Lion_B0[localPortNum].txLoss;
          GT_U16_QT2025_SET_FIELD (fw[i].data, 4, 3, rxLossVal);
          GT_U16_QT2025_SET_FIELD (fw[i].data, 1, 3, txLossVal);
        }

        if (CRP (cpssXsmiPortGroupRegisterWrite
                 (dev, (1 << portGroup), phyAddr, fw[i].regAddr,
                  fw[i].devAddr, fw[i].data)) != GT_OK)
          return GT_FAIL;
      }

      /* Second phase will broadcast data to all PHYs together so we
         need to perform it one time only */
      if (phase == 2)
        break;
    }
  }

  return GT_OK;
}

GT_STATUS
qt2025PhyConfig (GT_U8 dev, int ng, int np, int cl, GT_U32 *xsmiAddrArrayPtr)
{
  GT_U8 phyAddr, portGroup, port, localPortNum;
  GT_U16 data;

  if (((xsmiAddrPtr = xsmiAddrArrayPtr) == NULL) ||
      ((n_port_groups = ng) == 0) ||
      ((n_ports = np) == 0) ||
      ((check_lioncub = cl) == -1))
    return GT_NOT_INITIALIZED;

ERR ("FW download Phase-1 start.");
  /* phase 1 */
  if (CRP (qt2025PhyCfgPhaseX
           (dev, 1, ARRAY_SIZE (phyDataPhase1), phyDataPhase1))
      != GT_OK)
    return GT_FAIL;
ERR ("FW download Phase-1 end.");

  /* wait 1 mSec */
  osTimerWkAfter (1);

ERR ("FW download Phase-1a start.");
  /* special phase to enable broardcast */
  if (CRP (qt2025PhyCfgPhaseX
           (dev, 1, ARRAY_SIZE (phyDataPhase1a), phyDataPhase1a))
      != GT_OK)
    return GT_FAIL;
ERR ("FW download Phase-1a end.");

  /* wait 1 mSec */
  osTimerWkAfter (1);

ERR ("FW download Phase-2 start.");
  /* phase 2 */
  if (CRP (qt2025PhyCfgPhaseX
           (dev, 2, ARRAY_SIZE (phyDataPhase2), phyDataPhase2))
      != GT_OK)
    return GT_FAIL;
ERR ("FW download Phase-2 end.");

  /* wait 600 mSec */
  osTimerWkAfter (600);

ERR ("FW download Phase-3 start.");
  /* phase 3 */
  if (CRP (qt2025PhyCfgPhaseX
           (dev, 3, ARRAY_SIZE (phyDataPhase3), phyDataPhase3))
      != GT_OK)
    return GT_FAIL;
ERR ("FW download Phase-3 end.");

  /* wait additional 3000 mSec before FW stabilized */
  osTimerWkAfter (3000);

ERR ("FW Check download status start.");
  /* Check download status */
  for (portGroup = 0; portGroup < n_port_groups; portGroup++) {
    if (CPSS_98CX8203_CNS == PRV_CPSS_PP_MAC (dev)->devType)
      if ((2 == portGroup) || (3 == portGroup))
        continue;

    for (localPortNum = 0; localPortNum < n_ports; localPortNum++) {
      port = localPortNum + (16 * portGroup);
      phyAddr = xsmiAddrPtr[port];

      if (CRP (cpssXsmiPortGroupRegisterRead
               (dev, (1 << portGroup), phyAddr, 0xd7fd, 3, &data))
          != GT_OK)
        return GT_FAIL;

      if ((data == 0xF0) || (data == 0) || (data == 0x10)) { /* Checksum bad. */
        ERR ("FW Checksum error on port %d portGroup %d (phyAddr=0x%x) - value is 0x%x.",
             localPortNum, portGroup, phyAddr, data);
        return GT_FAIL;
      }
    }
  }
ERR ("FW Check download status end.");

  return GT_OK;
}

enum status
qt2025_phy_load_fw (void)
{
#if defined (SHOW_HG_PHY_FW_VERSION) || defined (SHOW_XG_PHY_HEARTBEAT)
  GT_U16 val;
  int i, a;
#endif /* SHOW_HG_PHY_FW_VERSION) || SHOW_XG_PHY_HEARTBEAT */
  int d;
  GT_STATUS rc;

  for_each_dev (d) {
    rc = CRP (prvCpssDrvHwPpSetRegField (d, 0x01800180, 14, 1, 1));
    if (rc != GT_OK)
      return ST_HEX;
    rc = CRP (qt2025PhyConfig (d, 1, dev_info[d].n_xg_phys, 0, dev_info[d].xg_phys));
    if (rc != GT_OK)
      return ST_HEX;

#ifdef SHOW_XG_PHY_HEARTBEAT
    for (i = 0; i < 10; ++i) {
      for (a = 0; a < dev_info[d].n_xg_phys; a++) {
        CRP (cpssXsmiPortGroupRegisterRead
             (d, CPSS_PORT_GROUP_UNAWARE_MODE_CNS,
              dev_info[d].xg_phys[a], 0xD7EE, 3, &val));
        DEBUG ("heartbeat: %d:%02X %04X\n", d, dev_info[d].xg_phys[a], val);
      }
      osDelay (10);
    }
#endif /* SHOW_XG_PHY_HEARTBEAT */

#ifdef SHOW_HG_PHY_FW_VERSION
    for (a = 0; a < dev_info[d].n_xg_phys; a++) {
      CRP (cpssXsmiPortGroupRegisterRead
           (d, CPSS_PORT_GROUP_UNAWARE_MODE_CNS,
            dev_info[d].xg_phys[a], 0xD7F3, 3, &val));
      DEBUG ("3.D7F3: %d:%02X %04X\n", d, dev_info[d].xg_phys[a], val);
      CRP (cpssXsmiPortGroupRegisterRead
           (d, CPSS_PORT_GROUP_UNAWARE_MODE_CNS,
            dev_info[d].xg_phys[a], 0xD7F4, 3, &val));
      DEBUG ("3.D7F4: %d:%02X %04X\n", d, dev_info[d].xg_phys[a], val);
      CRP (cpssXsmiPortGroupRegisterRead
           (d, CPSS_PORT_GROUP_UNAWARE_MODE_CNS,
            dev_info[d].xg_phys[a], 0xD7F5, 3, &val));
      DEBUG ("3.D7F5: %d:%02X %04X\n", d, dev_info[d].xg_phys[a], val);
    }
#endif /* SHOW_HG_PHY_FW_VERSION */
  }

  return ST_OK;
}

#endif /* VARIANT_GE */
