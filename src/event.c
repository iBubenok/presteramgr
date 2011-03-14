#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/generic/events/cpssGenEventUnifyTypes.h>
#include <cpss/generic/events/cpssGenEventRequests.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>

#include <stdio.h>
#include <stdlib.h>

#include <presteramgr.h>
#include <debug.h>
#include <utils.h>


static CPSS_UNI_EV_CAUSE_ENT events [] = {
  CPSS_PP_PORT_LINK_STATUS_CHANGED_E
};
#define EVENT_NUM ARRAY_SIZE (events)

static GT_UINTPTR event_handle;


void
event_enter_loop (void)
{
  static GT_U32 ebmp [CPSS_UNI_EV_BITMAP_SIZE_CNS];
  GT_U32 edata;
  GT_U8 dev;
  GT_STATUS rc;
  int i;

  rc = CRP (cpssEventBind (events, EVENT_NUM, &event_handle));
  if (rc != GT_OK)
    exit (1);

  for (i = 0; i < EVENT_NUM; ++i) {
    rc = CRP (cpssEventDeviceMaskSet (0, events [i],
                                      CPSS_EVENT_UNMASK_E));
    if (rc != GT_OK)
      exit (1);
  }

  while (1) {
    rc = CRP (cpssEventSelect (event_handle, NULL, ebmp,
                               CPSS_UNI_EV_BITMAP_SIZE_CNS));
    if (rc != GT_OK)
      exit (1);

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
