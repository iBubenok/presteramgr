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


GT_STATUS
cpss_bind_events (void)
{
  GT_STATUS rc;
  int i;
  static GT_U32 ebmp [CPSS_UNI_EV_BITMAP_SIZE_CNS];
  GT_U32 edata;
  GT_U8 dev;

  extDrvUartInit ();

  rc = CRP (cpssEventBind (events, EVENT_NUM, &event_handle));
  if (rc != GT_OK)
    return rc;

  for (i = 0; i < EVENT_NUM; ++i) {
    rc = CRP (cpssEventDeviceMaskSet (0, events [i],
                                      CPSS_EVENT_UNMASK_E));
    if (rc != GT_OK)
      return rc;
  }

  while (1) {
    rc = CRP (cpssEventSelect (event_handle, NULL, ebmp,
                               CPSS_UNI_EV_BITMAP_SIZE_CNS));
    if (rc != GT_OK)
      return rc;

    while ((rc = cpssEventRecv (event_handle,
                                CPSS_PP_PORT_LINK_STATUS_CHANGED_E,
                                &edata, &dev)) == GT_OK) {
      GT_BOOL link;

      rc = CRP (cpssDxChPortLinkStatusGet (dev, (GT_U8) edata, &link));
      if (rc != GT_OK)
        return rc;

      osPrintSync ("dev %d port %2d link %s\n",
                   dev, edata, link ? "up" : "down");
    }
  }

  return GT_OK;
}
