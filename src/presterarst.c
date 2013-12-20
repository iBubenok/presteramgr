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
#include <control-proto.h>
#include <sysdeps.h>

extern GT_STATUS osStartEngine (int, const char **, const char *, GT_VOIDFUNCPTR);

static DECLARE_DEV_INFO (dev_info) = DEV_INFO;

static enum status
pci_find_dev (struct dev_info *info)
{
  GT_U16 did = (info->dev_id >> 16) & 0xFFFF;
  GT_U16 vid = info->dev_id & 0xFFFF;
  GT_U32 ins = 0, bus_no = 0, dev_sel = 0, func_no = 0;
  GT_UINTPTR pci_base_addr, internal_pci_base;
  void *int_vec;
  GT_U32 int_mask;
  GT_STATUS rc;

  rc = CRP (extDrvPciFindDev (vid, did, ins, &bus_no, &dev_sel, &func_no));
  ON_GT_ERROR (rc) goto out;
  DEBUG ("Device found: id %X, bus no %d, dev sel %d, func no %d\r\n",
         info->dev_id, bus_no, dev_sel, func_no);

  rc = CRP (extDrvPciMap (bus_no, dev_sel, func_no, vid, did,
                          &pci_base_addr, &internal_pci_base));
  ON_GT_ERROR (rc) goto out;
  internal_pci_base &= 0xFFF00000;
  DEBUG ("%08X %08X\r\n", pci_base_addr, internal_pci_base);

  rc = CRP (extDrvGetPciIntVec (info->int_num, &int_vec));
  ON_GT_ERROR (rc) goto out;
  DEBUG ("intvec: %d\r\n", (GT_U32) int_vec);

  rc = extDrvGetIntMask (info->int_num, &int_mask);
  ON_GT_ERROR (rc) goto out;
  DEBUG ("intmask: %08X\r\n", int_mask);

  info->ph1_info.busBaseAddr = pci_base_addr;
  info->ph1_info.internalPciBase = internal_pci_base;
  info->ph1_info.intVecNum = (GT_U32) int_vec;
  info->ph1_info.intMask = int_mask;

 out:
  return (rc == GT_OK) ? ST_OK : ST_HEX;
}

static void
init_cpss (void)
{
  CPSS_PP_DEVICE_TYPE dev_type;

  int i;

  CRP (extDrvEthRawSocketModeSet (GT_TRUE));
  CRP (extsvc_bind ());
  CRP (cpssPpInit ());

  osFatalErrorInit (NULL);
  osMemInit (2048 * 1024, GT_TRUE);
  extDrvUartInit ();

  for (i = 0; i < NDEVS; i++) {
    pci_find_dev (&dev_info[i]);

    DEBUG ("doing phase1 config\n");
    CRP (cpssDxChHwPpPhase1Init (&dev_info[i].ph1_info, &dev_type));
    DEBUG ("device type: %08X\n", dev_type);
  }
}

void
cpss_start (void)
{
  int i;

  if (osWrapperOpen (NULL) != GT_OK) {
    ALERT ("osWrapper initialization failure!\n");
    return;
  }

  init_cpss ();

  for (i = 0; i < NDEVS; i++) {
    DEBUG ("dev %d: doing soft reset", i);
    CRP (cpssDxChHwPpSoftResetSkipParamSet
         (i, CPSS_HW_PP_RESET_SKIP_TYPE_REGISTER_E, GT_TRUE));
    CRP (cpssDxChHwPpSoftResetSkipParamSet
         (i, CPSS_HW_PP_RESET_SKIP_TYPE_TABLE_E, GT_FALSE));
    CRP (cpssDxChHwPpSoftResetSkipParamSet
         (i, CPSS_HW_PP_RESET_SKIP_TYPE_EEPROM_E, GT_FALSE));
    CRP (cpssDxChHwPpSoftResetSkipParamSet
         (i, CPSS_HW_PP_RESET_SKIP_TYPE_PEX_E, GT_TRUE));
    CRP (cpssDxChHwPpSoftResetSkipParamSet
         (i, CPSS_HW_PP_RESET_SKIP_TYPE_LINK_LOSS_E, GT_FALSE));
    CRP (cpssDxChHwPpSoftResetTrigger (i));
    sleep (1);
  }

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
  int debug = 1;

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
