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
#include <log.h>
#include <utils.h>
#include <port.h>
#include <data.h>
#include <zcontext.h>

#include <czmq.h>


static void *pub_sock;

static zmsg_t *
make_notify_message (enum control_notification type)
{
  zmsg_t *msg = zmsg_new ();
  assert (msg);

  notification_t tmp = type;
  zmsg_addmem (msg, &tmp, sizeof (tmp));

  return msg;
}

static inline void
notify_send (zmsg_t **msg)
{
  zmsg_send (msg, pub_sock);
}

static inline void
put_port_id (zmsg_t *msg, port_id_t pid)
{
  zmsg_addmem (msg, &pid, sizeof (pid));
}

static void
put_port_state (zmsg_t *msg, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  struct port_link_state state;

  data_encode_port_state (&state, attrs);
  zmsg_addmem (msg, &state, sizeof (state));
}

static void
notify_port_state (port_id_t pid, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  zmsg_t *msg = make_notify_message (CN_PORT_LINK_STATE);
  put_port_id (msg, pid);
  put_port_state (msg, attrs);
  notify_send (&msg);
}


DECLSHOW (CPSS_PORT_SPEED_ENT);
DECLSHOW (CPSS_PORT_DUPLEX_ENT);


static CPSS_UNI_EV_CAUSE_ENT events [] = {
  CPSS_PP_MAC_AGE_VIA_TRIGGER_ENDED_E,
  CPSS_PP_PORT_LINK_STATUS_CHANGED_E,
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

static GT_STATUS
event_handle_link_change (void)
{
  GT_U32 edata;
  GT_U8 dev;
  GT_STATUS rc;

  while ((rc = cpssEventRecv (event_handle,
                              CPSS_PP_PORT_LINK_STATUS_CHANGED_E,
                              &edata, &dev)) == GT_OK) {
    port_id_t pid;
    CPSS_PORT_ATTRIBUTES_STC attrs;
    if (port_handle_link_change (dev, (GT_U8) edata, &pid, &attrs) == ST_OK)
      notify_port_state (pid, &attrs);
  }

  if (rc == GT_NO_MORE)
    rc = GT_OK;

  return CRP (rc);
}

static GT_STATUS
event_handle_aging_done (void)
{
  GT_U32 edata;
  GT_U8 dev;
  GT_STATUS rc;

  /* TODO: notify others. */

  while ((rc = cpssEventRecv (event_handle,
                              CPSS_PP_MAC_AGE_VIA_TRIGGER_ENDED_E,
                              &edata, &dev)) == GT_OK)
    DEBUG ("aging done\r\n");
  if (rc == GT_NO_MORE)
    rc = GT_OK;

  return CRP (rc);
}

void
event_enter_loop (void)
{
  static GT_U32 ebmp [CPSS_UNI_EV_BITMAP_SIZE_CNS];
  GT_STATUS rc;
  int i;
  GT_32 key;

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
      continue;

    if (eventp (CPSS_PP_MAC_AGE_VIA_TRIGGER_ENDED_E, ebmp))
      event_handle_aging_done ();
    if (eventp (CPSS_PP_PORT_LINK_STATUS_CHANGED_E, ebmp))
      event_handle_link_change ();
  }
}

void
event_init (void)
{
  pub_sock = zsocket_new (zcontext, ZMQ_PUB);
  assert (pub_sock);
  zsocket_bind (pub_sock, EVENT_PUBSUB_EP);
}
