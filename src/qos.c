#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/cos/cpssDxChCos.h>
#include <cpss/generic/port/cpssPortTx.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortTx.h>

#include <cpss/dxCh/dxChxGen/config/private/prvCpssDxChInfo.h>
// PRV_CPSS_DXCH2_QOS_PROFILE_NUM_MAX_CNS 128

#include <presteramgr.h>
#include <debug.h>
#include <qos.h>
#include <port.h>
#include <sysdeps.h>
#include <utils.h>
#include <sys/prctl.h>

int mls_qos_trust = 0;
const uint8_t qos_default_wrr_weights[8] = {
  1, 2, 4, 8, 16, 32, 64, 128
};

#define QSP_FIRST_REGULAR_IDX (QSP_BASE_TC + QSP_NUM_TC)
#define QSP_MAX \
  (PRV_CPSS_DXCH2_QOS_PROFILE_NUM_MAX_CNS /*xCat 128 */  - QSP_FIRST_REGULAR_IDX)

struct stack {
  int sp;
  qos_profile_id_t data[QSP_MAX];
};

static struct stack res;

static inline int
res_pop (void)
{
  if (res.sp >= QSP_MAX)
    return -1;

  return res.data[res.sp++];
}

static inline int
res_push (uint16_t re)
{
  if (res.sp == 0)
    return -1;

  res.data[--res.sp] = re;
  return 0;
}

static inline int
res_chk_avail (int n) {
  return (QSP_MAX - res.sp >= n)? 1 : 0;
}

#define HASH_FIND_QP(findqp, out)                 \
    HASH_FIND (hh, qpdb, findqp, sizeof (qos_profile_id_t), out)
#define HASH_ADD_QP(gwfield, add)                 \
    HASH_ADD (hh, qpdb, gwfield, sizeof (qos_profile_id_t), add)

struct qpentry {
  qos_profile_id_t id;
  int refc;
  UT_hash_handle hh;
};

struct qpentry *qpdb = NULL;


static enum status
__qos_set_prioq_num (int num, int prof)
{
  int i, d;

  if (num < 0 || num > 8)
    return ST_BAD_VALUE;

  for (i = 0; i < 8 - num; i++) {
    DEBUG ("set %d:%d to WRR\r\n", prof, i);
    for_each_dev (d)
      CRP (cpssDxChPortTxQArbGroupSet
           (d, i, CPSS_PORT_TX_WRR_ARB_GROUP_0_E, prof));
  }
  for ( ; i < 8; i++) {
    DEBUG ("set %d:%d to SP\r\n", prof, i);
    for_each_dev (d)
      CRP (cpssDxChPortTxQArbGroupSet
           (d, i, CPSS_PORT_TX_SP_ARB_GROUP_E, prof));
  }

  return ST_OK;
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

enum status
qos_set_mls_qos_trust (int trust)
{
  port_id_t i;

  mls_qos_trust = trust;
  for (i = 1; i <= nports; i++)
    port_update_qos_trust (port_ptr (i));

  return ST_OK;
}

enum status
qos_set_port_mls_qos_trust_cos (port_id_t pid, bool_t trust)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  port->trust_cos = !!trust;
  return port_update_qos_trust (port);
}

enum status
qos_set_port_mls_qos_trust_dscp (port_id_t pid, bool_t trust)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  port->trust_dscp = !!trust;
  return port_update_qos_trust (port);
}

enum status
qos_start (void)
{
  DEBUG ("qos_start");
  struct dscp_map dscp_map[64];
  queue_id_t cos_map[8] = { 2, 0, 1, 3, 4, 5, 6, 7 };
  int i, d;
  pthread_t tid;

  for (i = QSP_BASE_TC; i < QSP_BASE_TC + QSP_NUM_TC; i++) {
    CPSS_DXCH_COS_PROFILE_STC prof = {
      .dropPrecedence = CPSS_DP_GREEN_E,
      .userPriority = 0,
      .trafficClass = i,
      .dscp = 0,
      .exp = 0
    };

    for_each_dev (d)
      CRP (cpssDxChCosProfileEntrySet (d, i, &prof));
  }

  for (i = 0; i < QSP_MAX; i++)
    res.data[i] = i + QSP_FIRST_REGULAR_IDX;
  res.sp = 0;


  CPSS_DXCH_COS_PROFILE_STC prof = {
    .dropPrecedence = CPSS_DP_GREEN_E,
    .userPriority = 0,
    .trafficClass = 1,
    .dscp = 46,
    .exp = 0
  };

  for_each_dev (d)
    CRP (cpssDxChCosProfileEntrySet (d, 10, &prof));

CRP(cpssDxChCosProfileEntryGet(0, 10, &prof));
DEBUG(".dropPrecedence %d: .userPriority %d: .trafficClass %d: .dscp .exp %d, %d\r\n", prof.dropPrecedence, prof.userPriority, prof.trafficClass, prof.dscp, prof.exp);

  for_each_dev (d)
    CRP (cpssDxChPortTxWrrGlobalParamSet
         (d,
          CPSS_PORT_TX_WRR_BYTE_MODE_E,
          CPSS_PORT_TX_WRR_MTU_2K_E));

  for (i = 0; i < 64; i++) {
    dscp_map[i].dscp = i;
    dscp_map[i].queue = i / 8;
  }
  qos_set_dscp_prio (64, dscp_map);

  qos_set_cos_prio (cos_map);

  //thread ->zloop timer
  pthread_create (&tid, NULL, storm_control_thread, NULL);

  __qos_set_prioq_num (1, CPSS_PORT_TX_SCHEDULER_PROFILE_3_E);
  qos_set_prioq_num (8);
  qos_set_wrr_queue_weights (qos_default_wrr_weights);

/* CPU port priorities */
  static uint8_t cpu_weights[8] = { 100, 100, 100, 100, 100, 100, 100, 100};
  __qos_set_prioq_num (3, CPSS_PORT_TX_SCHEDULER_PROFILE_1_E);
  for (i = 0; i < 8; i++)
    CRP (cpssDxChPortTxQWrrProfileSet
          (CPU_DEV, i, cpu_weights[i], CPSS_PORT_TX_SCHEDULER_PROFILE_1_E));

  return ST_OK;
}

enum status
qos_set_dscp_prio (int n, const struct dscp_map *map)
{
  int i, d;

  for (i = 0; i < n; ++i)
    if (map[i].dscp > 63 || map[i].queue > 7)
      return ST_BAD_VALUE;

  for (i = 0; i < n; ++i)
    for_each_dev (d)
      CRP (cpssDxChCosDscpToProfileMapSet (d, map[i].dscp, map[i].queue));

  return ST_OK;
}

enum status
qos_set_cos_prio (const queue_id_t *map)
{
  int i, d;

  for (i = 0; i < 8; i++)
    if (map[i] > 7)
      return ST_BAD_VALUE;

  for (i = 0; i < 8; i++) {
    for_each_dev (d) {
      CRP (cpssDxChCosUpCfiDeiToProfileMapSet (d, 0, i, 0, map[i]));
      CRP (cpssDxChCosUpCfiDeiToProfileMapSet (d, 0, i, 1, map[i]));
    }
  }

  return ST_OK;
}

enum status
qos_set_prioq_num (int num)
{
  return __qos_set_prioq_num (num, CPSS_PORT_TX_SCHEDULER_PROFILE_2_E);
}

enum status
qos_set_wrr_queue_weights (const uint8_t *weights)
{
  int i, d;

  for (i = 0; i < 8; i++)
    for_each_dev (d)
      CRP (cpssDxChPortTxQWrrProfileSet
           (d, i, weights[i], CPSS_PORT_TX_SCHEDULER_PROFILE_2_E));

  return ST_OK;
}

enum status
qos_set_wrtd (int enable)
{
  enum status st;
  int d;

  for_each_dev (d) {
    st = cpssDxChPortTxRandomTailDropEnableSet(d, gt_bool(enable));
    CRP (st);
  }

  return st;
}

enum status
qos_profile_manage (struct qos_profile_mgmt * qpm, qos_profile_id_t *id) {
  int rep, d;
  CPSS_DXCH_COS_PROFILE_STC prof;
  struct qpentry *qpe;

DEBUG(">>>>qos_profile_manage(qpm(cmd %d, id %d, tc %d, dscp %d, cos %d, exp %d), *id %d )\n",
    qpm->cmd, qpm->id ,qpm->tc ,qpm->dscp ,qpm->cos ,qpm->exp, *id);
  switch (qpm->cmd) {
    case QOS_PROFILE_ADD:
      if ((rep = res_pop()) < 0) return ST_BUSY;
DEBUG("====qos_profile_manage(add) rep %d\n", rep);
      *id = rep;

      qpe = (struct qpentry*) malloc (sizeof(*qpe));
      qpe->id = rep;
      qpe->refc = 1;
      HASH_ADD_QP(id, qpe);

      prof.dropPrecedence = CPSS_DP_GREEN_E;
      prof.userPriority = qpm->cos;
      prof.trafficClass = qpm->tc;
      prof.dscp = qpm->dscp;
      prof.exp = qpm->exp;
      for_each_dev (d)
        CRP (cpssDxChCosProfileEntrySet (d, rep, &prof));
      return ST_OK;

    case QOS_PROFILE_DEL:
      HASH_FIND_QP(&qpm->id, qpe);
DEBUG("====qos_profile_manage(del) qpe %p\n", qpe);
      if (!qpe) return ST_DOES_NOT_EXIST;
      HASH_DELETE(hh, qpdb, qpe);
      if (res_push(qpe->id) < 0) return ST_HEX;
      free(qpe);
DEBUG("====qos_profile_manage(del) OK\n");
      return ST_OK;
    case QOS_PROFILE_SET:
      HASH_FIND_QP(&qpm->id, qpe);
DEBUG("====qos_profile_manage(set) qpe %p\n", qpe);
      if (!qpe) return ST_DOES_NOT_EXIST;
      prof.dropPrecedence = CPSS_DP_GREEN_E;
      prof.userPriority = qpm->cos;
      prof.trafficClass = qpm->tc;
      prof.dscp = qpm->dscp;
      prof.exp = qpm->exp;
      for_each_dev (d)
        CRP (cpssDxChCosProfileEntrySet (d, qpm->id, &prof));
      return ST_OK;

    case QOS_PROFILE_CHK_AVAIL:
      if (res_chk_avail(qpm->num)) return ST_OK;
      return ST_BUSY;
    default:
      return ST_BAD_VALUE;
  }
}

void *
storm_control_thread (void *dummy)
{
  int timer_id;
  zloop_t *loop = zloop_new ();
  void *event_push_sock;

  event_push_sock = zsock_new (ZMQ_PUSH);
  assert (event_push_sock);
  zsock_connect (event_push_sock, EVENT_SOCK_EP);

  timer_id = zloop_timer (loop, DELAY, 0, storm_detect, event_push_sock);

  prctl(PR_SET_NAME, "storm-controller", 0, 0, 0);

  zloop_start (loop);

  return NULL;
}

int
storm_detect (zloop_t *loop, int timer_id, void *notify_socket)
{
  uint8_t drop_dev_counter = 0;
  uint64_t drop_pack_value;
  int d;

  for_each_dev (d)
  {
    get_rate_limit_drop_counter (d, &drop_pack_value);
    if (drop_pack_value > 0)
    {
      DEBUG ("drop_pack_value = %llu",drop_pack_value);
      drop_dev_counter++;
      set_rate_limit_drop_counter (d, 0);
    }
  }

  if (drop_dev_counter > 0) {
    send_storm_detected_event(notify_socket);
    }
    //DEBUG ("end drop_dev_counter = %u",drop_dev_counter);
  return 0;
}

void
send_storm_detected_event (void *notify_socket)
{
  zmsg_t *msg;
  msg = make_notify_message (CN_STORM_DETECTED);
  zmsg_send (&msg, notify_socket);
  zmsg_destroy(&msg);
}

