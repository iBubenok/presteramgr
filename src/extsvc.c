#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/extServices/cpssExtServices.h>

/* OS binding function prototypes. */
#include <gtOs/gtOsMem.h>
#include <gtOs/gtOsStr.h>
#include <gtOs/gtOsSem.h>
#include <gtOs/gtOsIo.h>
#include <gtOs/gtOsInet.h>
#include <gtOs/gtOsTimer.h>
#include <gtOs/gtOsIntr.h>
#include <gtOs/gtOsRand.h>
#include <gtOs/gtOsTask.h>
#include <gtOs/gtOsStdLib.h>
#include <gtOs/gtOsMsgQ.h>

/* Ext driver function prototypes. */
#include <gtExtDrv/drivers/gtCacheMng.h>
#include <gtExtDrv/drivers/gtSmiHwCtrl.h>
#include <gtExtDrv/drivers/gtTwsiHwCtrl.h>
#include <gtExtDrv/drivers/gtDmaDrv.h>
#include <gtExtDrv/drivers/gtEthPortCtrl.h>
#include <gtExtDrv/drivers/gtHsuDrv.h>
#include <gtExtDrv/drivers/gtIntDrv.h>
#include <gtExtDrv/drivers/gtPciDrv.h>
#include <gtExtDrv/drivers/gtDragoniteDrv.h>

#include <presteramgr.h>
#include <debug.h>


static GT_STATUS
cpssGetDefaultOsBindFuncs (OUT CPSS_OS_FUNC_BIND_STC *osFuncBindPtr)
{
  osMemSet (osFuncBindPtr, 0, sizeof (*osFuncBindPtr));

  /* bind the OS functions to the CPSS */
  osFuncBindPtr->osMemBindInfo.osMemBzeroFunc          = osBzero;
  osFuncBindPtr->osMemBindInfo.osMemSetFunc            = osMemSet;
  osFuncBindPtr->osMemBindInfo.osMemCpyFunc            = osMemCpy;
  osFuncBindPtr->osMemBindInfo.osMemCmpFunc            = osMemCmp;
  osFuncBindPtr->osMemBindInfo.osMemStaticMallocFunc   = osStaticMalloc;
  osFuncBindPtr->osMemBindInfo.osMemMallocFunc         = osMalloc;
  osFuncBindPtr->osMemBindInfo.osMemReallocFunc        = osRealloc;
  osFuncBindPtr->osMemBindInfo.osMemFreeFunc           = osFree;
  osFuncBindPtr->osMemBindInfo.osMemCacheDmaMallocFunc = osCacheDmaMalloc;
  osFuncBindPtr->osMemBindInfo.osMemCacheDmaFreeFunc   = osCacheDmaFree;
  osFuncBindPtr->osMemBindInfo.osMemPhyToVirtFunc      = osPhy2Virt;
  osFuncBindPtr->osMemBindInfo.osMemVirtToPhyFunc      = osVirt2Phy;

  osFuncBindPtr->osStrBindInfo.osStrlenFunc            = osStrlen;
  osFuncBindPtr->osStrBindInfo.osStrCpyFunc            = osStrCpy;
#ifdef  LINUX
  osFuncBindPtr->osStrBindInfo.osStrNCpyFunc           = osStrNCpy;
#endif
  osFuncBindPtr->osStrBindInfo.osStrChrFunc            = osStrChr;
  osFuncBindPtr->osStrBindInfo.osStrCmpFunc            = osStrCmp;
  osFuncBindPtr->osStrBindInfo.osStrNCmpFunc           = osStrNCmp;
  osFuncBindPtr->osStrBindInfo.osStrCatFunc            = osStrCat;
  osFuncBindPtr->osStrBindInfo.osStrStrNCatFunc        = osStrNCat;
  osFuncBindPtr->osStrBindInfo.osStrChrToUpperFunc     = osToUpper;
  osFuncBindPtr->osStrBindInfo.osStrTo32Func           = osStrTo32;
  osFuncBindPtr->osStrBindInfo.osStrToU32Func          = osStrToU32;

  osFuncBindPtr->osSemBindInfo.osMutexCreateFunc       = osMutexCreate;
  osFuncBindPtr->osSemBindInfo.osMutexDeleteFunc       = osMutexDelete;
  osFuncBindPtr->osSemBindInfo.osMutexLockFunc         = osMutexLock;
  osFuncBindPtr->osSemBindInfo.osMutexUnlockFunc       = osMutexUnlock;

  osFuncBindPtr->osSemBindInfo.osSigSemBinCreateFunc   = (void *) osSemBinCreate;
#ifdef  LINUX
  osFuncBindPtr->osSemBindInfo.osSigSemMCreateFunc     = osSemMCreate;
  osFuncBindPtr->osSemBindInfo.osSigSemCCreateFunc     = osSemCCreate;
#endif
  osFuncBindPtr->osSemBindInfo.osSigSemDeleteFunc      = osSemDelete;
  osFuncBindPtr->osSemBindInfo.osSigSemWaitFunc        = osSemWait;
  osFuncBindPtr->osSemBindInfo.osSigSemSignalFunc      = osSemSignal;

  osFuncBindPtr->osIoBindInfo.osIoBindStdOutFunc       = osBindStdOut;
  osFuncBindPtr->osIoBindInfo.osIoPrintfFunc           = osPrintf;
#ifdef  LINUX
  osFuncBindPtr->osIoBindInfo.osIoVprintfFunc          = osVprintf;
#endif
  osFuncBindPtr->osIoBindInfo.osIoSprintfFunc          = osSprintf;
#ifdef  LINUX
  osFuncBindPtr->osIoBindInfo.osIoVsprintfFunc         = osVsprintf;
#endif
  osFuncBindPtr->osIoBindInfo.osIoPrintSynchFunc       = osPrintSync;
  osFuncBindPtr->osIoBindInfo.osIoGetsFunc             = osGets;

  osFuncBindPtr->osInetBindInfo.osInetNtohlFunc        = osNtohl;
  osFuncBindPtr->osInetBindInfo.osInetHtonlFunc        = osHtonl;
  osFuncBindPtr->osInetBindInfo.osInetNtohsFunc        = osNtohs;
  osFuncBindPtr->osInetBindInfo.osInetHtonsFunc        = osHtons;
  osFuncBindPtr->osInetBindInfo.osInetNtoaFunc         = osInetNtoa;

  osFuncBindPtr->osTimeBindInfo.osTimeWkAfterFunc      = osTimerWkAfter;
  osFuncBindPtr->osTimeBindInfo.osTimeTickGetFunc      = osTickGet;
  osFuncBindPtr->osTimeBindInfo.osTimeGetFunc          = osTime;
  osFuncBindPtr->osTimeBindInfo.osTimeRTFunc           = osTimeRT;
#ifdef  LINUX
  osFuncBindPtr->osTimeBindInfo.osGetSysClockRateFunc  = osGetSysClockRate;
  osFuncBindPtr->osTimeBindInfo.osDelayFunc            = osDelay;
#endif

#if !defined(UNIX) || defined(ASIC_SIMULATION)
  osFuncBindPtr->osIntBindInfo.osIntEnableFunc         = osIntEnable;
  osFuncBindPtr->osIntBindInfo.osIntDisableFunc        = osIntDisable;
  osFuncBindPtr->osIntBindInfo.osIntConnectFunc        = osInterruptConnect;
#endif
#if (!defined(FREEBSD) && !defined(UCLINUX)) || defined(ASIC_SIMULATION)
  /* this function required for sand_os_mainOs_interface.c
   * Now it is implemented for:
   *   all os with ASIC simulation
   *   vxWorks
   *   Win32
   *   Linux (stub which does nothing)
   */
  osFuncBindPtr->osIntBindInfo.osIntModeSetFunc        = (void *) osSetIntLockUnlock;
#endif
  osFuncBindPtr->osRandBindInfo.osRandFunc             = osRand;
  osFuncBindPtr->osRandBindInfo.osSrandFunc            = osSrand;

  osFuncBindPtr->osTaskBindInfo.osTaskCreateFunc       = osTaskCreate;
  osFuncBindPtr->osTaskBindInfo.osTaskDeleteFunc       = osTaskDelete;
  /*osFuncBindPtr->osTaskBindInfo.osTaskGetSelfFunc    = osTaskGetSelf;*/
  osFuncBindPtr->osTaskBindInfo.osTaskLockFunc         = osTaskLock;
  osFuncBindPtr->osTaskBindInfo.osTaskUnLockFunc       = osTaskUnLock;

#ifdef  LINUX
  osFuncBindPtr->osStdLibBindInfo.osQsortFunc          = osQsort;
  osFuncBindPtr->osStdLibBindInfo.osBsearchFunc        = osBsearch;
#endif

  osFuncBindPtr->osMsgQBindInfo.osMsgQCreateFunc       = osMsgQCreate;
  osFuncBindPtr->osMsgQBindInfo.osMsgQDeleteFunc       = osMsgQDelete;
  osFuncBindPtr->osMsgQBindInfo.osMsgQSendFunc         = osMsgQSend;
  osFuncBindPtr->osMsgQBindInfo.osMsgQRecvFunc         = osMsgQRecv;
  osFuncBindPtr->osMsgQBindInfo.osMsgQNumMsgsFunc      = osMsgQNumMsgs;

  return GT_OK;
}

static GT_STATUS
cpssGetDefaultExtDrvFuncs (OUT CPSS_EXT_DRV_FUNC_BIND_STC  *extDrvFuncBindInfoPtr)
{
  osMemSet (extDrvFuncBindInfoPtr, 0, sizeof (*extDrvFuncBindInfoPtr));

  /* bind the external drivers functions to the CPSS */
  extDrvFuncBindInfoPtr->extDrvMgmtCacheBindInfo.extDrvMgmtCacheFlush      = (void *) extDrvMgmtCacheFlush;
  extDrvFuncBindInfoPtr->extDrvMgmtCacheBindInfo.extDrvMgmtCacheInvalidate = (void *) extDrvMgmtCacheInvalidate;

  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiInitDriverFunc      = hwIfSmiInitDriver;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiWriteRegFunc        = hwIfSmiWriteReg;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiReadRegFunc         = hwIfSmiReadReg;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiTaskRegRamReadFunc  = hwIfSmiTskRegRamRead;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiTaskRegRamWriteFunc = hwIfSmiTskRegRamWrite;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiTaskRegVecReadFunc  = hwIfSmiTskRegVecRead;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiTaskRegVecWriteFunc = hwIfSmiTskRegVecWrite;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiTaskWriteRegFunc    = hwIfSmiTaskWriteReg;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiTaskReadRegFunc     = hwIfSmiTaskReadReg;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiIntReadRegFunc      = hwIfSmiInterruptReadReg;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiIntWriteRegFunc     = hwIfSmiInterruptWriteReg;
  extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiDevVendorIdGetFunc  = extDrvSmiDevVendorIdGet;
  /*  used only in linux -- will need to be under some kind of COMPILATION FLAG
      extDrvFuncBindInfoPtr->extDrvHwIfSmiBindInfo.extDrvHwIfSmiTaskWriteFieldFunc  = hwIfSmiTaskWriteRegField;
  */

#ifdef GT_I2C
  extDrvFuncBindInfoPtr->extDrvHwIfTwsiBindInfo.extDrvHwIfTwsiInitDriverFunc = hwIfTwsiInitDriver;
  extDrvFuncBindInfoPtr->extDrvHwIfTwsiBindInfo.extDrvHwIfTwsiWriteRegFunc   = hwIfTwsiWriteReg;
  extDrvFuncBindInfoPtr->extDrvHwIfTwsiBindInfo.extDrvHwIfTwsiReadRegFunc    = hwIfTwsiReadReg;
#endif /* GT_I2C */

/*  XBAR related services */
#if defined(IMPL_FA) || defined(IMPL_XBAR)
  extDrvFuncBindInfoPtr->extDrvMgmtHwIfBindInfo.extDrvI2cMgmtMasterInitFunc    = gtI2cMgmtMasterInit;
  extDrvFuncBindInfoPtr->extDrvMgmtHwIfBindInfo.extDrvMgmtReadRegisterFunc     = (void *) extDrvMgmtReadRegister;
  extDrvFuncBindInfoPtr->extDrvMgmtHwIfBindInfo.extDrvMgmtWriteRegisterFunc    = (void *) extDrvMgmtWriteRegister;
  extDrvFuncBindInfoPtr->extDrvMgmtHwIfBindInfo.extDrvMgmtIsrReadRegisterFunc  = (void *) extDrvMgmtIsrReadRegister;
  extDrvFuncBindInfoPtr->extDrvMgmtHwIfBindInfo.extDrvMgmtIsrWriteRegisterFunc = (void *) extDrvMgmtIsrWriteRegister;
#endif

  extDrvFuncBindInfoPtr->extDrvDmaBindInfo.extDrvDmaWriteDriverFunc = extDrvDmaWrite;
  extDrvFuncBindInfoPtr->extDrvDmaBindInfo.extDrvDmaReadFunc        = extDrvDmaRead;

  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPortRxInitFunc            = extDrvEthPortRxInit;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPortTxInitFunc            = extDrvEthPortTxInit;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPortEnableFunc            = extDrvEthPortEnable;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPortDisableFunc           = extDrvEthPortDisable;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPortTxFunc                = extDrvEthPortTx;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPortInputHookAddFunc      = extDrvEthInputHookAdd;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPortTxCompleteHookAddFunc = extDrvEthTxCompleteHookAdd;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPortRxPacketFreeFunc      = extDrvEthRxPacketFree;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPortTxModeSetFunc         = (void *) extDrvEthPortTxModeSet;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthRawSocketModeSetFunc  = extDrvEthRawSocketModeSet;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthRawSocketModeGetFunc  = extDrvEthRawSocketModeGet;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvLinuxModeSetFunc  = extDrvLinuxModeSet;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvLinuxModeGetFunc  = extDrvLinuxModeGet;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthRawSocketRxHookAddFunc = extDrvEthRawSocketRxHookAdd;

  extDrvFuncBindInfoPtr->extDrvHsuDrvBindInfo.extDrvHsuMemBaseAddrGetFunc = extDrvHsuMemBaseAddrGet;
  extDrvFuncBindInfoPtr->extDrvHsuDrvBindInfo.extDrvHsuWarmRestartFunc = extDrvHsuWarmRestart;

#if defined (XCAT_DRV)
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthCpuCodeToQueueFunc        = extDrvEthCpuCodeToQueue;
  extDrvFuncBindInfoPtr->extDrvEthPortBindInfo.extDrvEthPrePendTwoBytesHeaderSetFunc = extDrvEthPrePendTwoBytesHeaderSet;
#endif

  extDrvFuncBindInfoPtr->extDrvIntBindInfo.extDrvIntConnectFunc     = extDrvIntConnect;
  extDrvFuncBindInfoPtr->extDrvIntBindInfo.extDrvIntEnableFunc      = extDrvIntEnable;
  extDrvFuncBindInfoPtr->extDrvIntBindInfo.extDrvIntDisableFunc     = extDrvIntDisable;
  extDrvFuncBindInfoPtr->extDrvIntBindInfo.extDrvIntLockModeSetFunc = (void *) extDrvSetIntLockUnlock;

  extDrvFuncBindInfoPtr->extDrvPciInfo.extDrvPciConfigWriteRegFunc        = extDrvPciConfigWriteReg;
  extDrvFuncBindInfoPtr->extDrvPciInfo.extDrvPciConfigReadRegFunc         = extDrvPciConfigReadReg;
  extDrvFuncBindInfoPtr->extDrvPciInfo.extDrvPciDevFindFunc               = extDrvPciFindDev;
  extDrvFuncBindInfoPtr->extDrvPciInfo.extDrvPciIntVecFunc                = (void *) extDrvGetPciIntVec;
  extDrvFuncBindInfoPtr->extDrvPciInfo.extDrvPciIntMaskFunc               = (void *) extDrvGetIntMask;
  extDrvFuncBindInfoPtr->extDrvPciInfo.extDrvPciCombinedAccessEnableFunc  = extDrvEnableCombinedPciAccess;
  extDrvFuncBindInfoPtr->extDrvPciInfo.extDrvPciDoubleWriteFunc           = extDrvPciDoubleWrite;
  extDrvFuncBindInfoPtr->extDrvPciInfo.extDrvPciDoubleReadFunc            = extDrvPciDoubleRead;

#ifdef DRAGONITE_TYPE_A1
  extDrvFuncBindInfoPtr->extDrvDragoniteInfo.extDrvDragoniteShMemBaseAddrGetFunc = extDrvDragoniteShMemBaseAddrGet;
#endif

  return GT_OK;
}

static GT_STATUS
trace_hw_access_stub (IN GT_U8       devNum,
                      IN GT_U32      portGroupId,
                      IN GT_BOOL     isrContext,
                      IN GT_BOOL     pciPexSpace,
                      IN GT_U32      addr,
                      IN GT_U32      length,
                      IN GT_U32      *dataPtr)
{
  return GT_OK;
}

static GT_STATUS
cpssGetDefaultTraceFuncs (OUT CPSS_TRACE_FUNC_BIND_STC  *traceFuncBindInfoPtr)
{
  osMemSet (traceFuncBindInfoPtr, 0, sizeof (*traceFuncBindInfoPtr));

  /* bind the external drivers functions to the CPSS */
  traceFuncBindInfoPtr->traceHwBindInfo.traceHwAccessReadFunc  = trace_hw_access_stub;
  traceFuncBindInfoPtr->traceHwBindInfo.traceHwAccessWriteFunc = trace_hw_access_stub;

  return GT_OK;
}

GT_STATUS
extsvc_bind (void)
{
  CPSS_EXT_DRV_FUNC_BIND_STC ef;
  CPSS_OS_FUNC_BIND_STC of;
  CPSS_TRACE_FUNC_BIND_STC tf;

  CRPR (cpssGetDefaultOsBindFuncs (&of));
  CRPR (cpssGetDefaultExtDrvFuncs (&ef));
  CRPR (cpssGetDefaultTraceFuncs (&tf));
  return CRP (cpssExtServicesBind (&ef, &of, &tf));
}
