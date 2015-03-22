#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdbHash.h>

#include <mac.h>
#include <zcontext.h>
#include <port.h>
#include <vlan.h>
#include <dev.h>
#include <utils.h>
#include <debug.h>
#include <log.h>


#define FDB_CONTROL_EP "inproc://fdb-control"

enum fdb_ctl_cmd {
  FCC_MAC_OP,
  FCC_OWN_MAC_OP,
  FCC_MC_IP_OP
};

static void *ctl_sock;

static enum status __attribute__ ((unused))
fdb_ctl (int cmd, const void *arg, int size)
{
  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, arg, size);
  zmsg_send (&msg, ctl_sock);

  msg = zmsg_recv (ctl_sock);
  status_t status = *((status_t *) zframe_data (zmsg_first (msg)));
  zmsg_destroy (&msg);

  return status;
}

enum status
mac_mc_ip_op (const struct mc_ip_op_arg *arg)
{
  if (!vlan_valid (arg->vid))
    return ST_BAD_VALUE;

  return fdb_ctl (FCC_MC_IP_OP, arg, sizeof (*arg));
}

enum status
mac_op (const struct mac_op_arg *arg)
{
  if (!vlan_valid (arg->vid))
    return ST_BAD_VALUE;

  return fdb_ctl (FCC_MAC_OP, arg, sizeof (*arg));
}

enum status
mac_op_own (vid_t vid, mac_addr_t mac, int add)
{
  struct mac_op_arg arg;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  arg.vid = vid;
  memcpy (arg.mac, mac, sizeof (arg.mac));
  arg.delete = !add;
  /* Everything else is irrelevant for own MAC addr. */

  return fdb_ctl (FCC_OWN_MAC_OP, &arg, sizeof (arg));
}

enum status
mac_set_aging_time (aging_time_t time)
{
  GT_STATUS rc;
  int d;

  for_each_dev (d) {
    rc = CRP (cpssDxChBrgFdbAgingTimeoutSet (d, time));
    ON_GT_ERROR (rc) break;
  }

  switch (rc) {
  case GT_OK:        return ST_OK;
  case GT_BAD_PARAM: return ST_BAD_VALUE;
  case GT_HW_ERROR:  return ST_HW_ERROR;
  default:           return ST_HEX;
  }
}

CPSS_MAC_UPDATE_MSG_EXT_STC fdb_addrs[FDB_MAX_ADDRS];
GT_U32 fdb_naddrs = 0;

enum status
mac_flush (const struct mac_age_arg *arg, GT_BOOL del_static)
{
  CPSS_FDB_ACTION_MODE_ENT s_act_mode;
  CPSS_MAC_ACTION_MODE_ENT s_mac_mode;
  GT_U32 s_act_dev, s_act_dev_mask, act_dev, act_dev_mask;
  GT_U16 s_act_vid, s_act_vid_mask, act_vid, act_vid_mask;
  GT_U32 s_is_trunk, s_is_trunk_mask;
  GT_U32 s_port, s_port_mask, port, port_mask;
  GT_BOOL done[NDEVS];
  int d, all_done, i;

  if (arg->vid == ALL_VLANS) {
    act_vid = 0;
    act_vid_mask = 0;
  } else {
    if (!vlan_valid (arg->vid))
      return ST_BAD_VALUE;
    act_vid = arg->vid;
    act_vid_mask = 0x0FFF;
  }

  if (arg->port == ALL_PORTS) {
    act_dev = 0;
    act_dev_mask = 0;
    port = 0;
    port_mask = 0;
  } else {
    struct port *p = port_ptr (arg->port);

    if (!p)
      return ST_BAD_VALUE;

    act_dev = phys_dev (p->ldev);
    act_dev_mask = 31;
    port = p->lport;
    port_mask = 0x0000007F;
  }

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbAAandTAToCpuSet (d, GT_FALSE));

    CRP (cpssDxChBrgFdbActionModeGet (d, &s_act_mode));
    CRP (cpssDxChBrgFdbMacTriggerModeGet (d, &s_mac_mode));
    CRP (cpssDxChBrgFdbActionActiveDevGet (d, &s_act_dev, &s_act_dev_mask));
    CRP (cpssDxChBrgFdbActionActiveVlanGet (d, &s_act_vid, &s_act_vid_mask));
    CRP (cpssDxChBrgFdbActionActiveInterfaceGet
         (d, &s_is_trunk, &s_is_trunk_mask, &s_port, &s_port_mask));
    CRP (cpssDxChBrgFdbActionsEnableSet (d, GT_FALSE));

    CRP (cpssDxChBrgFdbActionActiveDevSet (d, act_dev, act_dev_mask));
    CRP (cpssDxChBrgFdbActionActiveVlanSet (d, act_vid, act_vid_mask));
    CRP (cpssDxChBrgFdbActionActiveInterfaceSet (d, 0, 0, port, port_mask));
    CRP (cpssDxChBrgFdbStaticDelEnable (d, del_static));
    CRP (cpssDxChBrgFdbTrigActionStart (d, CPSS_FDB_ACTION_DELETING_E));

    done[d] = GT_FALSE;
  }

  if (act_vid_mask && port_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.key.key.macVlan.vlanId == act_vid
          && fdb[i].me.dstInterface.devPort.devNum == act_dev
          && fdb[i].me.dstInterface.devPort.portNum == port
          && (del_static || !fdb[i].me.isStatic))
        fdb[i].valid = 0;
  } else if (act_vid_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.key.key.macVlan.vlanId == act_vid
          && (del_static || !fdb[i].me.isStatic))
        fdb[i].valid = 0;
  } else if (port_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.dstInterface.devPort.devNum == act_dev
          && fdb[i].me.dstInterface.devPort.portNum == port
          && (del_static || !fdb[i].me.isStatic))
        fdb[i].valid = 0;
  } else {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && (del_static || !fdb[i].me.isStatic))
        fdb[i].valid = 0;
  }

  do {
    all_done = 1;
    for_each_dev (d) {
      if (!done[d]) {
        CRP (cpssDxChBrgFdbTrigActionStatusGet (d, &done[d]));
        all_done &= !!done[d];
      }
    }
    usleep (100);
  } while (!all_done);

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbActionActiveInterfaceSet
         (d, s_is_trunk, s_is_trunk_mask, s_port, s_port_mask));
    CRP (cpssDxChBrgFdbActionActiveDevSet (d, s_act_dev, s_act_dev_mask));
    CRP (cpssDxChBrgFdbActionModeSet (d, s_act_mode));
    CRP (cpssDxChBrgFdbActionActiveVlanSet (d, s_act_vid, s_act_vid_mask));
    CRP (cpssDxChBrgFdbMacTriggerModeSet (d, s_mac_mode));
    CRP (cpssDxChBrgFdbActionsEnableSet (d, GT_TRUE));

    CRP (cpssDxChBrgFdbAAandTAToCpuSet (d, GT_TRUE));
  }

  return ST_OK;
}

/*
 * FDB management.
 */

struct fdb_entry fdb[FDB_MAX_ADDRS];

enum fdb_entry_prio {
  FEP_UNUSED,
  FEP_DYN,
  FEP_STATIC,
  FEP_OWN
};

static inline int
me_key_eq (const CPSS_MAC_ENTRY_EXT_KEY_STC *a,
           const CPSS_MAC_ENTRY_EXT_KEY_STC *b)
{
  if (a->entryType != b->entryType)
    return 0;

  switch (a->entryType) {
  case CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E:
    return (a->key.macVlan.vlanId == b->key.macVlan.vlanId
            && !memcmp (a->key.macVlan.macAddr.arEther,
                        b->key.macVlan.macAddr.arEther,
                        6));

  case CPSS_MAC_ENTRY_EXT_TYPE_IPV4_MCAST_E:
  case CPSS_MAC_ENTRY_EXT_TYPE_IPV6_MCAST_E:
    return (a->key.ipMcast.vlanId == b->key.ipMcast.vlanId
            && !memcmp (a->key.ipMcast.sip, b->key.ipMcast.sip, 4)
            && !memcmp (a->key.ipMcast.dip, b->key.ipMcast.dip, 4));
  }

  /* Should never happen. */
  return 0;
}

#define INVALID_IDX 0xFFFFFFFF
static enum status
fdb_insert (CPSS_MAC_ENTRY_EXT_STC *e, int own)
{
  GT_U32 idx, best_idx = INVALID_IDX;
  int i, d, best_pri = e->userDefined;

  ON_GT_ERROR (CRP (cpssDxChBrgFdbHashCalc (CPU_DEV, &e->key, &idx)))
    return ST_HEX;

  for (i = 0; i < 4; i++, idx++) {
    if (me_key_eq (&e->key, &fdb[idx].me.key)) {
      if (!fdb[idx].valid
          || fdb[idx].me.userDefined <= e->userDefined) {
        best_idx = idx;
        break;
      } else {
        DEBUG ("won't overwrite FDB entry with higher priority\r\n");
        return ST_ALREADY_EXISTS;
      }
    }

    if (best_pri == FEP_UNUSED)
      continue;

    if (!fdb[idx].valid) {
      best_pri = FEP_UNUSED;
      best_idx = idx;
      continue;
    }

    if (best_pri > fdb[idx].me.userDefined) {
      best_pri = fdb[idx].me.userDefined;
      best_idx = idx;
    }
  }

  if (best_idx == INVALID_IDX)
    return ST_DOES_NOT_EXIST;

  memcpy (&fdb[best_idx].me, e, sizeof (*e));
  fdb[best_idx].valid = 1;
  for_each_dev (d) {
    if (own)
      e->dstInterface.devPort.devNum = phys_dev (d);
    CRP (cpssDxChBrgFdbMacEntryWrite (d, best_idx, GT_FALSE, e));
  }

  return ST_OK;
}
#undef INVALID_IDX

static enum status
fdb_remove (CPSS_MAC_ENTRY_EXT_KEY_STC *k)
{
  GT_U32 idx;
  int i, d;

  ON_GT_ERROR (CRP (cpssDxChBrgFdbHashCalc (CPU_DEV, k, &idx)))
    return ST_HEX;

  for (i = 0; i < 4; i++, idx++) {
    if (fdb[idx].valid && me_key_eq (k, &fdb[idx].me.key)) {
      /* DEBUG ("found entry at %u, removing\r\n", idx); */

      for_each_dev (d)
        CRP (cpssDxChBrgFdbMacEntryInvalidate (d, idx));
      fdb[idx].valid = 0;

      return ST_OK;
    }
  }

  DEBUG ("Aged entry not found!\r\n");
  return ST_DOES_NOT_EXIST;
}

static enum status
fdb_mac_add (const struct mac_op_arg *arg, int own)
{
  CPSS_MAC_ENTRY_EXT_STC me;

  memset (&me, 0, sizeof (me));
  me.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (me.key.key.macVlan.macAddr.arEther, arg->mac, sizeof (arg->mac));
  me.key.key.macVlan.vlanId = arg->vid;
  me.isStatic = GT_TRUE;

  if (own) {
    me.userDefined = FEP_OWN;
    me.dstInterface.type = CPSS_INTERFACE_PORT_E;
    /* me.dstInterface.devPort.devNum will be set in fdb_insert() */
    me.dstInterface.devPort.portNum = CPSS_CPU_PORT_NUM_CNS;
    me.appSpecificCpuCode = GT_TRUE;
    me.daCommand = CPSS_MAC_TABLE_FRWRD_E;
    me.saCommand = CPSS_MAC_TABLE_FRWRD_E;
    me.daRoute = GT_TRUE;
  } else {
    me.userDefined = FEP_STATIC;

    if (arg->drop) {
      me.dstInterface.type = CPSS_INTERFACE_VID_E;
      me.dstInterface.vlanId = arg->vid;

      me.daCommand = CPSS_MAC_TABLE_DROP_E;
      me.saCommand = CPSS_MAC_TABLE_DROP_E;
    } else {
      struct port *port = port_ptr (arg->port);

      if (!port)
        return ST_BAD_VALUE;

      me.dstInterface.type = CPSS_INTERFACE_PORT_E;
      me.dstInterface.devPort.devNum = phys_dev (port->ldev);
      me.dstInterface.devPort.portNum = port->lport;

      me.daCommand = CPSS_MAC_TABLE_FRWRD_E;
      me.saCommand = CPSS_MAC_TABLE_FRWRD_E;
    }
  }

  return fdb_insert (&me, own);
}

static enum status
fdb_mac_delete (const struct mac_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_KEY_STC key;

  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (key.key.macVlan.macAddr.arEther, arg->mac, sizeof (arg->mac));
  key.key.macVlan.vlanId = arg->vid;

  return fdb_remove (&key);
}

static enum status
fdb_mac_op (const struct mac_op_arg *arg, int own)
{
  if (arg->delete)
    return fdb_mac_delete (arg);
  else
    return fdb_mac_add (arg, own);
}

static enum status
fdb_mac_mc_ip_delete (const struct mc_ip_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_KEY_STC key;

  memset (&key, 0, sizeof (key));
  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_IPV4_MCAST_E;
  memcpy (key.key.ipMcast.sip, arg->src, sizeof (arg->src));
  memcpy (key.key.ipMcast.dip, arg->dst, sizeof (arg->dst));
  key.key.ipMcast.vlanId = arg->vid;

  return fdb_remove (&key);
}

static enum status
fdb_mac_mc_ip_add (const struct mc_ip_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_STC me;

  memset (&me, 0, sizeof (me));
  me.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_IPV4_MCAST_E;
  memcpy (me.key.key.ipMcast.sip, arg->src, sizeof (arg->src));
  memcpy (me.key.key.ipMcast.dip, arg->dst, sizeof (arg->dst));
  me.key.key.ipMcast.vlanId = arg->vid;
  me.isStatic = GT_TRUE;
  me.userDefined = FEP_STATIC;
  me.dstInterface.type = CPSS_INTERFACE_VIDX_E;
  me.dstInterface.vidx = arg->mcg;
  me.daCommand = CPSS_MAC_TABLE_FRWRD_E;
  me.saCommand = CPSS_MAC_TABLE_FRWRD_E;

  return fdb_insert (&me, 0);
}

static enum status
fdb_mac_mc_ip_op (const struct mc_ip_op_arg *arg)
{
  if (arg->delete)
    return fdb_mac_mc_ip_delete (arg);
  else
    return fdb_mac_mc_ip_add (arg);
}

static void
fdb_new_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u)
{
  u->macEntry.appSpecificCpuCode = GT_FALSE;
  u->macEntry.isStatic           = GT_FALSE;
  u->macEntry.daCommand          = CPSS_MAC_TABLE_FRWRD_E;
  u->macEntry.saCommand          = CPSS_MAC_TABLE_FRWRD_E;
  u->macEntry.daRoute            = GT_FALSE;
  u->macEntry.userDefined        = FEP_DYN;
  u->macEntry.spUnknown          = GT_FALSE;

  fdb_insert (&u->macEntry, 0);
}

static void
fdb_old_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u)
{
  /* DEBUG ("AA msg: " MAC_FMT ", VLAN %d\r\n", */
  /*        MAC_ARG (u->macEntry.key.key.macVlan.macAddr.arEther), */
  /*        u->macEntry.key.key.macVlan.vlanId); */

  fdb_remove (&u->macEntry.key);
}

static void
fdb_upd_for_dev (int d)
{
  GT_U32 num, total;
  GT_STATUS rc;
  CPSS_MAC_UPDATE_MSG_EXT_STC *ptr = fdb_addrs;
  int i;

  total = 0;
  do {
    num = FDB_MAX_ADDRS - total;
    rc = cpssDxChBrgFdbAuMsgBlockGet (d, &num, ptr);
    total += num;
    ptr += num;
  } while (rc == GT_OK && total < FDB_MAX_ADDRS);

  switch (rc) {
  case GT_OK:
  case GT_NO_MORE:
    for (i = 0; i < total; i++) {
      switch (fdb_addrs[i].updType) {
      case CPSS_NA_E:
      case CPSS_SA_E:
        fdb_new_addr (d, &fdb_addrs[i]);
        break;
      case CPSS_AA_E:
        fdb_old_addr (d, &fdb_addrs[i]);
        break;
      default:
        break;
      }
    }
    break;

  default:
    CRP (rc);
  }
}

static int
fdb_evt_handler (zloop_t *loop, zmq_pollitem_t *pi, void *not_sock)
{
  zmsg_t *msg = zmsg_recv (not_sock);
  zframe_t *frame = zmsg_first (msg);
  GT_U8 dev = *((GT_U8 *) zframe_data (frame));
  zmsg_destroy (&msg);

  fdb_upd_for_dev (dev);

  return 0;
}

static int
fdb_upd_timer (zloop_t *loop, zmq_pollitem_t *pi, void *not_sock)
{
  int d;

  for_each_dev (d)
    fdb_upd_for_dev (d);

  return 0;
}

static int
fdb_ctl_handler (zloop_t *loop, zmq_pollitem_t *pi, void *ctl_sock)
{
  zmsg_t *msg = zmsg_recv (ctl_sock);
  zframe_t *frame = zmsg_first (msg);
  int cmd = *((int *) zframe_data (frame));
  void *arg = zframe_data (zmsg_next (msg));
  status_t status = ST_BAD_REQUEST;

  switch (cmd) {
  case FCC_MAC_OP:
    status = fdb_mac_op (arg, 0);
    break;
  case FCC_OWN_MAC_OP:
    status = fdb_mac_op (arg, 1);
    break;
  case FCC_MC_IP_OP:
    status = fdb_mac_mc_ip_op (arg);
  }
  zmsg_destroy (&msg);

  msg = zmsg_new ();
  zmsg_addmem (msg, &status, sizeof (status));
  zmsg_send (&msg, ctl_sock);

  return 0;
}

static volatile int fdb_thread_started = 0;

static void *
fdb_thread (void *_)
{
  void *not_sock, *ctl_sock;
  zloop_t *loop;

  DEBUG ("starting up FDB\r\n");

  loop = zloop_new ();
  assert (loop);

  not_sock = zsocket_new (zcontext, ZMQ_PULL);
  assert (not_sock);
  zsocket_bind (not_sock, FDB_NOTIFY_EP);

  zmq_pollitem_t not_pi = { not_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &not_pi, fdb_evt_handler, not_sock);
  zloop_timer (loop, 1000, 0, fdb_upd_timer, not_sock);

  ctl_sock = zsocket_new (zcontext, ZMQ_REP);
  assert (ctl_sock);
  zsocket_bind (ctl_sock, FDB_CONTROL_EP);

  zmq_pollitem_t ctl_pi = { ctl_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &ctl_pi, fdb_ctl_handler, ctl_sock);

  DEBUG ("FDB startup done\r\n");
  fdb_thread_started = 1;
  zloop_start (loop);

  return NULL;
}

enum status
mac_start (void)
{
  pthread_t tid;
  struct mac_age_arg arg = {
    .vid = ALL_VLANS,
    .port = ALL_PORTS
  };
  GT_U32 bmp = 0, n;
  GT_STATUS rc;
  int d;

  for_each_dev (d)
    bmp |= 1 << phys_dev (d);

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbActionsEnableSet (d, GT_FALSE));

    CRP (cpssDxChBrgFdbActionActiveDevSet (d, phys_dev(d), 0x1F));
    CRP (cpssDxChBrgFdbActionActiveVlanSet (d, 0, 0));
    CRP (cpssDxChBrgFdbActionActiveInterfaceSet (d, 0, 0, 0, 0));
    CRP (cpssDxChBrgFdbStaticDelEnable (d, GT_FALSE));
    CRP (cpssDxChBrgFdbActionModeSet (d, CPSS_FDB_ACTION_AGE_WITHOUT_REMOVAL_E));
    CRP (cpssDxChBrgFdbMacTriggerModeSet (d, CPSS_ACT_AUTO_E));
    CRP (cpssDxChBrgFdbActionsEnableSet (d, GT_TRUE));

    CRP (cpssDxChBrgFdbDeviceTableSet (d, bmp));

    /* Disable AA/TA messages. */
    CRP (cpssDxChBrgFdbAAandTAToCpuSet (d, GT_FALSE));
    CRP (cpssDxChBrgFdbSpAaMsgToCpuSet (d, GT_FALSE));

    /* Flush AU message queue. */
    do {
      n = FDB_MAX_ADDRS;
      rc = cpssDxChBrgFdbAuMsgBlockGet (d, &n, fdb_addrs);
    } while (rc == GT_OK);
    if (rc != GT_NO_MORE)
      CRP (rc);
  }

  mac_flush (&arg, GT_TRUE);

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbAAandTAToCpuSet (d, GT_TRUE));
    CRP (cpssDxChBrgFdbSpAaMsgToCpuSet (d, GT_TRUE));
  }

  memset (fdb, 0, sizeof (fdb));
  pthread_create (&tid, NULL, fdb_thread, NULL);
  DEBUG ("waiting for FDB startup\r\n");
  n = 0;
  while (!fdb_thread_started) {
    n++;
    usleep (10000);
  }
  DEBUG ("FDB startup finished after %d iteractions\r\n", n);

  ctl_sock = zsocket_new (zcontext, ZMQ_REQ);
  assert (ctl_sock);
  zsocket_connect (ctl_sock, FDB_CONTROL_EP);

  return ST_OK;
}
