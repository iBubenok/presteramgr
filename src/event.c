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
#include <pthread.h>
#include <sys/prctl.h>

#include <control-proto.h>
#include <presteramgr.h>
#include <debug.h>
#include <log.h>
#include <utils.h>
#include <vif.h>
#include <port.h>
#include <data.h>
#include <tipc.h>
#include <mac.h>
#include <sec.h>
#include <zcontext.h>
#include <sysdeps.h>
#include <variant.h>

#include <czmq.h>


static void *pub_sock;
static void *not_sock;
static void *fdb_sock;

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

static void __attribute__ ((unused))
put_port_state (zmsg_t *msg, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  struct port_link_state state;

  data_encode_port_state (&state, attrs);
  zmsg_addmem (msg, &state, sizeof (state));
}

static void
thr_notify_port_state (vif_id_t vifid, port_id_t pid, const struct port_link_state *ps)
{
  tipc_notify_link (vifid, pid, ps);

  if (!pid)
    return;

  zmsg_t *msg = make_notify_message (CN_PORT_LINK_STATE);
  put_port_id (msg, pid);
  zmsg_addmem(msg, ps, sizeof(*ps));
  notify_send (&msg);

  struct port *port = port_ptr (pid);
  if (is_stack_port (port)) {
    zmsg_t *msg = make_notify_message (CN_STACK_PORT_STATE);
    port_stack_role_t role = port->stack_role;
    zmsg_addmem (msg, &role, sizeof (role));
    uint8_t link = ps->link;
    zmsg_addmem (msg, &link, sizeof (link));
    notify_send (&msg);
  }
}

static void
notify_port_state (vif_id_t vifid, port_id_t pid, const CPSS_PORT_ATTRIBUTES_STC *attrs) {
  struct port_link_state ps;
  data_encode_port_state (&ps, attrs);

  zmsg_t *msg = zmsg_new ();
  assert (msg);
  zmsg_addmem (msg, &vifid, sizeof (vifid));
  zmsg_addmem (msg, &pid, sizeof (pid));
  zmsg_addmem (msg, &ps, sizeof (ps));
  zmsg_send (&msg, not_sock);

  vif_set_link_status(vifid, &ps, not_sock);
}

DECLSHOW (CPSS_PORT_SPEED_ENT);
DECLSHOW (CPSS_PORT_DUPLEX_ENT);


static CPSS_UNI_EV_CAUSE_ENT events [] = {
  /* CPSS_PP_MAC_AGE_VIA_TRIGGER_ENDED_E, */
#ifndef VARIANT_FE
  CPSS_PP_PORT_LINK_STATUS_CHANGED_E,
#endif
  CPSS_PP_EB_AUQ_PENDING_E,
  CPSS_PP_EB_SECURITY_BREACH_UPDATE_E
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
event_handle_security_breach_update(CPSS_UNI_EV_CAUSE_ENT evt) {

  GT_U32 edata;
  GT_U8 dev;
  GT_STATUS rc;
  unsigned long long nn = 0;

  while ((rc = cpssEventRecv (event_handle,
                              evt,
                              &edata, &dev)) == GT_OK)
    nn++;
  if (rc == GT_NO_MORE) {
    sec_handle_security_breach_updates (dev, edata);
  }else {
    DEBUG("unappropriate GT_STATUS == %d after security breach events masspop\n", rc);
  }
  return GT_OK;
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
    vif_id_t vifid;
    port_id_t pid;
    CPSS_PORT_ATTRIBUTES_STC attrs;
    if (port_handle_link_change (dev, (GT_U8) edata, &vifid, &pid, &attrs) == ST_OK)
      notify_port_state (vifid, pid, &attrs);
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

static GT_STATUS
event_handle_au_msg (void)
{
  GT_STATUS rc;
  GT_U32 edata;
  GT_U8 dev;

  while ((rc = cpssEventRecv (event_handle,
                              CPSS_PP_EB_AUQ_PENDING_E,
                              &edata, &dev)) == GT_OK)
  if (rc == GT_NO_MORE)
    rc = GT_OK;

  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, &dev, sizeof (dev));
  zmsg_send (&msg, fdb_sock);

  return GT_OK;
}

void
event_enter_loop (void)
{
  static GT_U32 ebmp [CPSS_UNI_EV_BITMAP_SIZE_CNS];
  GT_STATUS rc;
  int i, d;
  GT_32 key;

  key = intr_lock ();

  rc = CRP (cpssEventBind (events, EVENT_NUM, &event_handle));
  if (rc != GT_OK)
    exit (1);

  for (i = 0; i < EVENT_NUM; ++i) {
    for_each_dev (d) {
      rc = CRP (cpssEventDeviceMaskSet (d, events [i], CPSS_EVENT_UNMASK_E));
      if (rc != GT_OK)
        exit (1);
    }
  }

  intr_unlock (key);

  prctl(PR_SET_NAME, "evt-loop", 0, 0, 0);

  while (1) {
    rc = CRP (cpssEventSelect (event_handle, NULL, ebmp,
                               CPSS_UNI_EV_BITMAP_SIZE_CNS));
    if (rc != GT_OK)
      continue;

    if (eventp (CPSS_PP_MAC_AGE_VIA_TRIGGER_ENDED_E, ebmp)) {
//      DEBUG("EVENT!!!: CPSS_PP_MAC_AGE_VIA_TRIGGER_ENDED_E\n"); // TODO remove
      event_handle_aging_done ();
    }
    if (eventp (CPSS_PP_EB_SECURITY_BREACH_UPDATE_E, ebmp)){
      event_handle_security_breach_update(CPSS_PP_EB_SECURITY_BREACH_UPDATE_E);
    }
    if (eventp (CPSS_PP_PORT_LINK_STATUS_CHANGED_E, ebmp))
      event_handle_link_change ();
    if (eventp (CPSS_PP_EB_AUQ_PENDING_E, ebmp))  {
      DEBUG("EVENT!!!: CPSS_PP_EB_AUQ_PENDING_E\n"); // TODO remove
      event_handle_au_msg ();}
  }
}

static int
notify_evt_handler (zloop_t *loop, zmq_pollitem_t *pi, void *not_sock)
{
  zmsg_t *msg = zmsg_recv (not_sock);

  zframe_t *frame = zmsg_first (msg);
  vif_id_t vifid = *((vif_id_t *) zframe_data (frame));
  assert(zframe_size(frame) == sizeof(vifid));

  frame = zmsg_next(msg);
  port_id_t pid = *((port_id_t *) zframe_data (frame));
  assert(zframe_size(frame) == sizeof(pid));

  frame = zmsg_next(msg);
  assert(frame);
  struct port_link_state *ps = (struct port_link_state *) zframe_data(frame);
  assert(zframe_size(frame) == sizeof(struct port_link_state));

  thr_notify_port_state (vifid, pid, ps);

  zmsg_destroy (&msg);

  return 0;
}

volatile static int notify_thread_started = 0;

static void*
notify_thread(void *_) {
  void *tnot_sock;
  zloop_t  *loop = zloop_new ();
  assert (loop);

  pub_sock = zsocket_new (zcontext, ZMQ_PUB);
  assert (pub_sock);
  zsocket_bind (pub_sock, EVENT_PUBSUB_EP);

  tnot_sock = zsocket_new (zcontext, ZMQ_PULL);
  assert (tnot_sock);
  zsocket_bind (tnot_sock, NOTIFY_QUEUE_EP);

  zmq_pollitem_t tnot_pi = { not_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &tnot_pi, notify_evt_handler, tnot_sock);

  prctl(PR_SET_NAME, "evt-notify", 0, 0, 0);
  notify_thread_started = 1;

  zloop_start(loop);

  return NULL;
}

void
event_start_notify_thread (void) {
  pthread_t tid;
  pthread_create (&tid, NULL, notify_thread, NULL);

  DEBUG ("waiting for event notify thread startup\r\n");
  unsigned n = 0;
  while (!notify_thread_started) {
    n++;
    usleep (10000);
  }
  DEBUG ("event notify thread startup finished after %u iteractions\r\n", n);

  not_sock = zsocket_new (zcontext, ZMQ_PUSH);
  assert (not_sock);
  zsocket_connect (not_sock, NOTIFY_QUEUE_EP);

}

void
event_init (void)
{
  fdb_sock = zsocket_new (zcontext, ZMQ_PUSH);
  assert (fdb_sock);
  zsocket_connect (fdb_sock, FDB_NOTIFY_EP);
}
