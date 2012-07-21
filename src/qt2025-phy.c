#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if defined (VARIANT_ARLAN_3424GE)

#include <cpssdefs.h>

#include <cpssDriver/pp/hardware/cpssDriverPpHw.h>
#include <cpss/dxCh/dxChxGen/phy/cpssDxChPhySmi.h>
#include <cpss/generic/smi/cpssGenSmi.h>
#include <cpss/dxCh/dxChxGen/config/private/prvCpssDxChInfo.h>

#include <gtOs/gtOsIo.h>
#include <gtOs/gtOsTimer.h>

#include <utils.h>
#include <log.h>
#include <debug.h>

#include <qt2025-phy.h>
#include <qt2025-phy-fw.h>

#define SHOW_XG_PHY_HEARTBEAT
#define SHOW_HG_PHY_FW_VERSION

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

  /* phase 1 */
  if (CRP (qt2025PhyCfgPhaseX
           (dev, 1, ARRAY_SIZE (phyDataPhase1), phyDataPhase1))
      != GT_OK)
    return GT_FAIL;

  /* wait 1 mSec */
  osTimerWkAfter (0);

  /* special phase to enable broardcast */
  if (CRP (qt2025PhyCfgPhaseX
           (dev, 1, ARRAY_SIZE (phyDataPhase1a), phyDataPhase1a))
      != GT_OK)
    return GT_FAIL;

  /* wait 1 mSec */
  osTimerWkAfter (0);

  /* phase 2 */
  if (CRP (qt2025PhyCfgPhaseX
           (dev, 2, ARRAY_SIZE (phyDataPhase2), phyDataPhase2))
      != GT_OK)
    return GT_FAIL;

  /* wait 600 mSec */
  osTimerWkAfter (600);

  /* phase 3 */
  if (CRP (qt2025PhyCfgPhaseX
           (dev, 3, ARRAY_SIZE (phyDataPhase3), phyDataPhase3))
      != GT_OK)
    return GT_FAIL;

  /* wait additional 3000 mSec before FW stabilized */
  osTimerWkAfter (3000);

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

      if ((data == 0xF0) || (data == 0) || (data == 0x10)) /* Checksum bad. */
        ERR ("FW Checksum error on port %d portGroup %d (phyAddr=0x%x) - value is 0x%x.",
             localPortNum, portGroup, phyAddr, data);
    }
  }

  return GT_OK;
}

enum status
qt2025_phy_load_fw (void)
{
  static GT_U32 xsmi_addrs[] = {0x18, 0x19, 0x1A, 0x1B};
  GT_U32 xsmiAddr;
  GT_U16 val;
  int i;

  CRP (prvCpssDrvHwPpSetRegField (0, 0x01800180, 14, 1, 1));
  CRP (qt2025PhyConfig (0, 1, ARRAY_SIZE (xsmi_addrs), 0, xsmi_addrs));

#ifdef SHOW_XG_PHY_HEARTBEAT
  for (i = 0; i < 10; ++i) {
    for (xsmiAddr = 0x18; xsmiAddr <= 0x1B; ++xsmiAddr) {
      CRP (cpssXsmiPortGroupRegisterRead
           (0, CPSS_PORT_GROUP_UNAWARE_MODE_CNS, xsmiAddr, 0xD7EE, 3, &val));
      DEBUG ("heartbeat: %02X %04X\n", xsmiAddr, val);
    }
    osDelay (10);
  }
#endif /* SHOW_XG_PHY_HEARTBEAT */

#ifdef SHOW_HG_PHY_FW_VERSION
  for (xsmiAddr = 0x18; xsmiAddr <= 0x1B; ++xsmiAddr) {
    CRP (cpssXsmiPortGroupRegisterRead
         (0, CPSS_PORT_GROUP_UNAWARE_MODE_CNS, xsmiAddr, 0xD7F3, 3, &val));
    DEBUG ("3.D7F3: %02X %04X\n", xsmiAddr, val);
    CRP (cpssXsmiPortGroupRegisterRead
         (0, CPSS_PORT_GROUP_UNAWARE_MODE_CNS, xsmiAddr, 0xD7F4, 3, &val));
    DEBUG ("3.D7F4: %02X %04X\n", xsmiAddr, val);
    CRP (cpssXsmiPortGroupRegisterRead
         (0, CPSS_PORT_GROUP_UNAWARE_MODE_CNS, xsmiAddr, 0xD7F5, 3, &val));
    DEBUG ("3.D7F5: %02X %04X\n", xsmiAddr, val);
  }
#endif /* SHOW_HG_PHY_FW_VERSION */

  return ST_OK;
}

#endif /* VARIANT_ARLAN_3424GE */
