#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/generic/events/cpssGenEventUnifyTypes.h>
#include <cpss/generic/events/cpssGenEventRequests.h>

#include <stdio.h>

#include <debug.h>
#include <utils.h>


static CPSS_UNI_EV_CAUSE_ENT events [] = {
  CPSS_PP_PORT_LINK_STATUS_CHANGED_E
};
#define EVENT_NUM ARRAY_SIZE (events)

static GT_UINTPTR event_handle;


static GT_VOID
event_isr_cb (GT_UINTPTR hndl, GT_VOID *cookie)
{
  GT_STATUS rc;
  static GT_U32 ebmp [CPSS_UNI_EV_BITMAP_SIZE_CNS];
  GT_U32 edata;
  GT_U8 dev;

  rc = CRP (cpssEventWaitingEventsGet (event_handle, ebmp,
                                       CPSS_UNI_EV_BITMAP_SIZE_CNS));
  if (rc != GT_OK)
    return;
  osPrintSync ("waiting: %08X\n",
               ebmp [CPSS_PP_PORT_LINK_STATUS_CHANGED_E >> 5]);

  while ((rc = CRP (cpssEventRecv (event_handle,
                                   CPSS_PP_PORT_LINK_STATUS_CHANGED_E,
                                   &edata, &dev))) == GT_OK)
    osPrintSync ("link change on dev %d port %2d\n",
                 dev, edata);

  osPrintSync ("clearing treated events\n");
  CRP (cpssEventTreatedEventsClear (event_handle));
  osPrintSync ("done\n");
}

GT_STATUS
cpss_bind_events (void)
{
  GT_STATUS rc;
  int i;

  extDrvUartInit ();

  rc = CRP (cpssEventIsrBind (events, EVENT_NUM, event_isr_cb, NULL, &event_handle));
  if (rc != GT_OK)
    return rc;

  for (i = 0; i < EVENT_NUM; ++i) {
    rc = CRP (cpssEventDeviceMaskSet (0, events [i], CPSS_EVENT_UNMASK_E));
    if (rc != GT_OK)
      return rc;
  }

  rc = CRP (cpssEventTreatedEventsClear (event_handle));
  if (rc != GT_OK)
    return rc;

  return GT_OK;
}
