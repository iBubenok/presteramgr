#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>

#include <cpssdefs.h>

#include <cpss/extServices/os/gtOs/gtGenTypes.h>
#include <cpss/dxCh/dxChxGen/cpssHwInit/cpssDxChHwInit.h>
#include <cpss/generic/init/cpssInit.h>
#include <gtExtDrv/drivers/gtPciDrv.h>
#include <gtExtDrv/drivers/gtEthPortCtrl.h>
#include <gtExtDrv/drivers/gtUartDrv.h>
#include <gtOs/gtOsExc.h>
#include <gtOs/gtOsGen.h>

#include <presteramgr.h>
#include <extsvc.h>
#include <utils.h>
#include <debug.h>
#include <log.h>
#include <sysdeps.h>

extern GT_STATUS osStartEngine (int, const char **, const char *, GT_VOIDFUNCPTR);

static GT_STATUS
init_pci (CPSS_DXCH_PP_PHASE1_INIT_INFO_STC *info)
{
  GT_U16 did = (DEVICE_ID >> 16) & 0xFFFF;
  GT_U16 vid = DEVICE_ID & 0xFFFF;
  GT_U32 ins = 0, bus_no = 0, dev_sel = 0, func_no = 0;
  GT_UINTPTR pci_base_addr, internal_pci_base;
  void *int_vec;
  GT_U32 int_mask;

  CRP (extDrvPciFindDev (vid, did, ins, &bus_no, &dev_sel, &func_no));
  DEBUG ("Device found: bus no %d, dev sel %d, func no %d\r\n",
         bus_no, dev_sel, func_no);

  CRP (extDrvPciMap (bus_no, dev_sel, func_no, vid, did,
                     &pci_base_addr, &internal_pci_base));
  internal_pci_base &= 0xFFF00000;
  DEBUG ("%08X %08X\r\n", pci_base_addr, internal_pci_base);

  CRP (extDrvGetPciIntVec (GT_PCI_INT_B, &int_vec));
  DEBUG ("intvec: %d\r\n", (GT_U32) int_vec);

  CRP (extDrvGetIntMask (GT_PCI_INT_B, &int_mask));
  DEBUG ("intmask: %08X\r\n", int_mask);

  info->busBaseAddr = pci_base_addr;
  info->internalPciBase = internal_pci_base;
  info->intVecNum = (GT_U32) int_vec;
  info->intMask = int_mask;

  return GT_OK;
}

static void
init_cpss (void)
{
  CPSS_DXCH_PP_PHASE1_INIT_INFO_STC ph1_info;
  CPSS_PP_DEVICE_TYPE dev_type;

  CRP (extDrvEthRawSocketModeSet (GT_TRUE));
  CRP (extsvc_bind ());
  CRP (cpssPpInit ());

  osFatalErrorInit (NULL);
  osMemInit (2048 * 1024, GT_TRUE);
  extDrvUartInit ();

  osMemSet (&ph1_info, 0, sizeof (ph1_info));

  ph1_info.devNum = 0;
  ph1_info.coreClock = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS;
  ph1_info.mngInterfaceType = CPSS_CHANNEL_PEX_E;
  ph1_info.ppHAState = CPSS_SYS_HA_MODE_ACTIVE_E;
  ph1_info.serdesRefClock = CPSS_DXCH_PP_SERDES_REF_CLOCK_INTERNAL_125_E;
  ph1_info.initSerdesDefaults = GT_TRUE;
  ph1_info.isExternalCpuConnected = GT_FALSE;

  CRP (init_pci (&ph1_info));

  DEBUG ("doing phase1 config\n");
  CRP (cpssDxChHwPpPhase1Init (&ph1_info, &dev_type));
  DEBUG ("device type: %08X\n", dev_type);
}

void
cpss_start (void)
{
  if (osWrapperOpen (NULL) != GT_OK) {
    ALERT ("osWrapper initialization failure!\n");
    return;
  }

  init_cpss ();

  DEBUG ("doing soft reset");
  CRP (cpssDxChHwPpSoftResetSkipParamSet
       (0, CPSS_HW_PP_RESET_SKIP_TYPE_REGISTER_E, GT_TRUE));
  CRP (cpssDxChHwPpSoftResetSkipParamSet
       (0, CPSS_HW_PP_RESET_SKIP_TYPE_TABLE_E, GT_FALSE));
  CRP (cpssDxChHwPpSoftResetSkipParamSet
       (0, CPSS_HW_PP_RESET_SKIP_TYPE_EEPROM_E, GT_FALSE));
  CRP (cpssDxChHwPpSoftResetSkipParamSet
       (0, CPSS_HW_PP_RESET_SKIP_TYPE_PEX_E, GT_FALSE));
  CRP (cpssDxChHwPpSoftResetSkipParamSet
       (0, CPSS_HW_PP_RESET_SKIP_TYPE_LINK_LOSS_E, GT_FALSE));
  CRP (cpssDxChHwPpSoftResetTrigger (0));
  sleep (1);

  exit (EXIT_SUCCESS);
}

static void
start (void)
{
  GT_STATUS rc;
  const char *argv[] = {
    "presterarst"
  };
  int argc = ARRAY_SIZE (argv);

  rc = osStartEngine (argc, argv, "presterarst", cpss_start);
  if (rc != GT_OK) {
    CRIT ("engine startup failed with %s (%d)\n", SHOW (GT_STATUS, rc), rc);
    exit (EXIT_FAILURE);
  }
}

int
main (int argc, char **argv)
{
  int debug = 0;

  while (1) {
    int c, option_index = 0;
    static struct option opts[] = {
      {"debug", 0, NULL, 'd'},
      {"log-cpss-errors", 0, NULL, 'c'},
      {NULL, 0, NULL, 0}
    };

    c = getopt_long (argc, argv, "dc", opts, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'd':
      debug = 1;
      break;
    case 'c':
      log_cpss_errors = 1;
      break;
    default:
      fprintf (stderr, "invalid command line arguments\n");
      exit (EXIT_FAILURE);
    }
  }

  log_init (0, debug);
  start ();

  exit (EXIT_SUCCESS);
}
