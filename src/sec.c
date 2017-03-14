#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgSecurityBreach.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <zcontext.h>
#include <sec.h>
#include <port.h>
#include <sysdeps.h>
#include <mac.h>
#include <utils.h>
#include <debug.h>
#include <log.h>
#include <dev.h>

static void *pub_sock;
static void *sec_sock;

struct sb_delay {
  uint8_t port_na_enabled;
  uint8_t port_na_blocked;
  uint32_t tdelay_sb_port_na;
  uint32_t tdelay_sb_moved_static;
  uint64_t tst_port_na;
  uint64_t tst_moved_static;
};

struct sb_delay sb_delay[NPORTS + 1];

enum status
sec_port_na_delay_set (port_id_t pid, uint32_t delay_secs) {
  assert(pid <= NPORTS);
  sb_delay[pid].tdelay_sb_port_na = delay_secs * 1000;
  return ST_OK;
}

enum status
sec_moved_static_delay_set (port_id_t pid, uint32_t delay_secs) {
  assert(pid <= NPORTS);
  sb_delay[pid].tdelay_sb_moved_static = delay_secs * 1000;
  return ST_OK;
}

enum status
sec_moved_static_enable (uint8_t dev, GT_BOOL enable) {

  GT_STATUS rc;
  rc = CRP(cpssDxChBrgSecurBreachMovedStaticAddrSet(dev, enable));

  switch (rc) {
  case GT_OK:                     return ST_OK;
  case GT_BAD_PARAM:              return ST_BAD_VALUE;
  case GT_HW_ERROR:               return ST_HW_ERROR;
  case GT_NOT_APPLICABLE_DEVICE:  return ST_NOT_SUPPORTED;
  default:                        return ST_HEX;
  }
}

enum status
sec_port_na_enable (const struct port *port, GT_BOOL enable) {

  sb_delay[port->id].port_na_enabled = enable;
  GT_STATUS rc;
  rc = CRP(cpssDxChBrgSecurBreachNaPerPortSet(port->ldev, port->lport, enable));

  switch (rc) {
  case GT_OK:                     return ST_OK;
  case GT_BAD_PARAM:              return ST_BAD_VALUE;
  case GT_HW_ERROR:               return ST_HW_ERROR;
  case GT_NOT_APPLICABLE_DEVICE:  return ST_NOT_SUPPORTED;
  default:                        return ST_HEX;
  }
}

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
put_port_id (zmsg_t *msg, port_id_t pid)
{
  zmsg_addmem (msg, &pid, sizeof (pid));
}

static inline void
put_mac (zmsg_t *msg, uint8_t *mac)
{
  zmsg_addmem (msg, mac, 6);
}

static inline void
put_vlan_id (zmsg_t *msg, vid_t vid)
{
  zmsg_addmem (msg, &vid, sizeof (vid));
}

static inline void
notify_send (zmsg_t **msg)
{
  zmsg_send (msg, pub_sock);
}

enum status
sec_handle_security_breach_updates (GT_U8 d, GT_U32 edata) {

  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, &d, sizeof (d));
  zmsg_addmem (msg, &edata, sizeof (edata));
  zmsg_send (&msg, sec_sock);
  return ST_OK;
}

static int
sect_event_handler (zloop_t *loop, zmq_pollitem_t *pi, void *sect_sock) {

  zmsg_t *msg = zmsg_recv (sect_sock);
  zframe_t *frame = zmsg_first (msg);
  GT_U8 dev = *((GT_U8 *) zframe_data (frame));
  GT_U32 edata = *((GT_U32 *) zframe_data (frame));
  zmsg_destroy (&msg);

  struct fdb_entry fe;
  CPSS_BRG_SECUR_BREACH_MSG_STC sbmsg;

  CRP (cpssDxChSecurBreachMsgGet (dev, &sbmsg));

  monotimemsec_t ts = time_monotonic();
  port_id_t pid = port_id(phys_dev(dev), sbmsg.port);
  uint8_t sb_type = 0;

  switch (sbmsg.code) {
    case CPSS_BRG_SECUR_BREACH_EVENTS_PORT_NOT_IN_VLAN_E:
      break;
    case CPSS_BRG_SECUR_BREACH_EVENTS_PORT_NA_E:
      if (ts > sb_delay[pid].tst_port_na + sb_delay[pid].tdelay_sb_port_na) {
      sb_delay[pid].tst_port_na = ts;
      sb_delay[pid].port_na_blocked = 1;
      sb_type = SB_PORT_NA;
      psec_enable_na_sb(pid, 0);
      fe.me.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
      fe.me.key.key.macVlan.vlanId = sbmsg.vlan;
      memcpy(fe.me.key.key.macVlan.macAddr.arEther, sbmsg.macSa.arEther, 6);
      if (mac2_query(&fe) == ST_OK) {
/*      DEBUG("SECBREACH (IGNORE) : " MAC_FMT " %03hu:%2hhu CODE: %u EDATA: %08X\n",   //TODO remove
            MAC_ARG(sbmsg.macSa.arEther), sbmsg.vlan, sbmsg.port, sbmsg.code, (unsigned)edata);*/
        sb_type = 0;
      }

/*      DEBUG("SECBREACH  : " MAC_FMT " %03hu:%2hhu CODE: %u EDATA: %08X\n",   //TODO remove
            MAC_ARG(sbmsg.macSa.arEther), sbmsg.vlan, sbmsg.port, sbmsg.code, (unsigned)edata);*/
      }
      break;
    case CPSS_BRG_SECUR_BREACH_EVENTS_MOVED_STATIC_E:
      if (ts > sb_delay[pid].tst_moved_static + sb_delay[pid].tdelay_sb_moved_static) {
        sb_delay[pid].tst_moved_static = ts;
        sb_type = SB_MOVED_STATIC;
/*        DEBUG("SECBREACH: " MAC_FMT " %03hu:%2hhu CODE: %u EDATA: %08X\n",   //TODO remove
            MAC_ARG(sbmsg.macSa.arEther), sbmsg.vlan, sbmsg.port, sbmsg.code, (unsigned)edata);*/
      }
      break;
    default:
      DEBUG("UNHANDLED SECURIY BREACH: " MAC_FMT " %03hu:%2hhu CODE: %u EDATA: %08X\n",
          MAC_ARG(sbmsg.macSa.arEther), sbmsg.vlan, sbmsg.port, sbmsg.code, (uint32_t) edata);
      break;
  }
  if (sb_type) {
    zmsg_t *msg = make_notify_message (CN_SECURITY_BREACH);
    zmsg_addmem (msg, &sb_type, sizeof (sb_type));
    put_port_id (msg, pid);
    put_vlan_id (msg, sbmsg.vlan);
    put_mac (msg, sbmsg.macSa.arEther);
    notify_send (&msg);
  }
  return 0;
}

static int
sect_delay_timer (zloop_t *loop, zmq_pollitem_t *pi, void *p) {

  monotimemsec_t ts = time_monotonic();
  unsigned pid;
  for (pid = 1; pid <= NPORTS; pid++) {
    if (sb_delay[pid].port_na_blocked
         && ts > sb_delay[pid].tst_port_na + sb_delay[pid].tdelay_sb_port_na) {
      sb_delay[pid].port_na_blocked = 0;
      psec_enable_na_sb(pid, 1);
    }
  }
  return 0;
}

static volatile int sec_thread_started = 0;

static void *
sect_thread (void *_)
{
  void *sect_sock;
  zloop_t *loop;

  DEBUG ("starting up security event handler thread\r\n");

  loop = zloop_new ();
  assert (loop);

  sect_sock = zsocket_new (zcontext, ZMQ_PULL);
  assert (sect_sock);
  zsocket_bind (sect_sock, SEC_EVENT_NOTIFY_EP);

  zmq_pollitem_t sect_pi = { sect_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &sect_pi, sect_event_handler, sect_sock);
  zloop_timer (loop, 1000, 0, sect_delay_timer, NULL);

  prctl(PR_SET_NAME, "sec-breach", 0, 0, 0);

  DEBUG ("security event handler thread startup done\r\n");
  sec_thread_started = 1;
  zloop_start (loop);

  return NULL;
}

enum status
sec_init(void) {
  pub_sock = zsocket_new (zcontext, ZMQ_PUB);
  assert (pub_sock);
  zsocket_bind (pub_sock, SEC_PUBSUB_EP);
  return ST_OK;
}

enum status
sec_start(void) {

  pthread_t sec_tid;
  unsigned n;

  monotimemsec_t ts = time_monotonic();
  unsigned i;
  for (i = 1 ; i <= NPORTS; i++) {
    sec_port_na_delay_set (i, 30);
    sec_moved_static_delay_set (i, 30);
    sb_delay[i].tst_port_na = ts;
    sb_delay[i].tst_moved_static = ts;
    sb_delay[i].port_na_enabled = 0;
    sb_delay[i].port_na_blocked = 0;
  }

  pthread_create (&sec_tid, NULL, sect_thread, NULL);

  DEBUG ("waiting for security event handler startup\r\n");
  n = 0;
  while (!sec_thread_started) {
    n++;
    usleep (10000);
  }
  DEBUG ("security event handler startup finished after %u iteractions\r\n", n);

  sec_sock = zsocket_new (zcontext, ZMQ_PUSH);
  assert (sec_sock);
  zsocket_connect (sec_sock, SEC_EVENT_NOTIFY_EP);

  return ST_OK;
}
