#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/generic/events/cpssGenEventUnifyTypes.h>
#include <cpss/generic/events/cpssGenEventRequests.h>
#include <cpss/dxCh/dxChxGen/config/cpssDxChCfgInit.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIfTypes.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgGen.h>

#include <gtOs/gtOsIntr.h>
#include <gtOs/gtOsTask.h>

#include <gtExtDrv/drivers/gtIntDrv.h>

#include <stdio.h>
#include <stdlib.h>

#include <presteramgr.h>
#include <debug.h>
#include <utils.h>


static CPSS_UNI_EV_CAUSE_ENT events [] = {
  CPSS_PP_PORT_LINK_STATUS_CHANGED_E,
  CPSS_PP_RX_BUFFER_QUEUE0_E,
  CPSS_PP_RX_BUFFER_QUEUE1_E,
  CPSS_PP_RX_BUFFER_QUEUE2_E,
  CPSS_PP_RX_BUFFER_QUEUE3_E,
  CPSS_PP_RX_BUFFER_QUEUE4_E,
  CPSS_PP_RX_BUFFER_QUEUE5_E,
  CPSS_PP_RX_BUFFER_QUEUE6_E,
  CPSS_PP_RX_BUFFER_QUEUE7_E
};
#define EVENT_NUM ARRAY_SIZE (events)

static GT_UINTPTR event_handle;


static inline GT_32
intr_lock (void)
{
  GT_32 key = 0;

  CRP (osTaskLock ());
  CRP (extDrvSetIntLockUnlock (INTR_MODE_LOCK, &key));

  return key;
}

static inline void
intr_unlock (GT_32 key)
{
  CRP (extDrvSetIntLockUnlock (INTR_MODE_UNLOCK, &key));
  CRP (osTaskUnLock ());
}

static inline int
eventp (CPSS_UNI_EV_CAUSE_ENT e, const GT_U32 *b)
{
  return b [e >> 5] & (1 << (e & 0x1f));
}

void
event_enter_loop (void)
{
  static GT_U32 ebmp [CPSS_UNI_EV_BITMAP_SIZE_CNS];
  GT_U32 edata;
  GT_U8 dev;
  GT_STATUS rc;
  int i;
  GT_32 key;

  for (i = 0; i < 28; ++i)
    CRP (cpssDxChPortEnableSet (0, i, GT_TRUE));
  CRP (cpssDxChPortEnableSet (0, 63, GT_TRUE));
  CRP (cpssDxChCfgDevEnable (0, GT_TRUE));

  for (i = 0; i < 8; ++i) {
    rc = CRP (cpssDxChNetIfSdmaRxQueueEnable (0, i, GT_TRUE));
    if (rc != GT_OK)
      exit (1);
  }

  rc = CRP (cpssDxChBrgGenBpduTrapEnableSet (0, GT_TRUE));
  if (rc != GT_OK)
    exit (1);

  key = intr_lock ();

  rc = CRP (cpssEventBind (events, EVENT_NUM, &event_handle));
  if (rc != GT_OK)
    exit (1);

  for (i = 0; i < EVENT_NUM; ++i) {
    rc = CRP (cpssEventDeviceMaskSet (0, events [i],
                                      CPSS_EVENT_UNMASK_E));
    if (rc != GT_OK)
      exit (1);
  }

  intr_unlock (key);

  while (1) {
    rc = CRP (cpssEventSelect (event_handle, NULL, ebmp,
                               CPSS_UNI_EV_BITMAP_SIZE_CNS));
    if (rc != GT_OK)
      exit (1);

    for (i = 0; i < CPSS_UNI_EV_BITMAP_SIZE_CNS; ++i)
      osPrintSync ("%08X ", ebmp [i]);
    osPrintSync ("\n");

    for (i = 0; i < 8; ++i) {
      if (eventp (CPSS_PP_RX_BUFFER_QUEUE0_E + i, ebmp)) {
        while ((rc = cpssEventRecv (event_handle,
                                    CPSS_PP_RX_BUFFER_QUEUE0_E + i,
                                    &edata, &dev)) == GT_OK) {
#define NBUFS 100
          static GT_U8 *bufs [NBUFS];
          static GT_U32 lens [NBUFS];
          GT_U32 nbufs;
          CPSS_DXCH_NET_RX_PARAMS_STC rxp;

          osPrintSync ("doing rx: dev %d, ext %d\n", dev, edata);

          rc = CRP (cpssDxChNetIfSdmaRxPacketGet (dev, i, &nbufs, bufs, lens, &rxp));
          if (rc == GT_NO_MORE)
            break;
          if (rc != GT_OK)
            exit (1);

          rc = CRP (cpssDxChNetIfRxBufFree (dev, i, bufs, nbufs));
          osPrintSync ("rx buffers freed\n");
        }
        if (rc != GT_NO_MORE)
          exit (1);
      }
    }

    if (eventp (CPSS_PP_PORT_LINK_STATUS_CHANGED_E, ebmp)) {
      while ((rc = cpssEventRecv (event_handle,
                                  CPSS_PP_PORT_LINK_STATUS_CHANGED_E,
                                  &edata, &dev)) == GT_OK) {
        GT_BOOL link;

        rc = CRP (cpssDxChPortLinkStatusGet (dev, (GT_U8) edata, &link));
        if (rc != GT_OK)
          exit (1);

        osPrintSync ("dev %d port %2d link %s\n",
                     dev, edata, link ? "up" : "down");
      }
      if (rc != GT_NO_MORE)
        exit (1);
    }
  }
}
