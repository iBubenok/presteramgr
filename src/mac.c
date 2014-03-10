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


/*
 * TODO: maintain shadow FDB.
 */

#undef DEBUG_LIST_MACS

static enum status
mac_mc_ip_delete (const struct mc_ip_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_KEY_STC key;
  GT_STATUS result;

  memset (&key, 0, sizeof (key));

  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_IPV4_MCAST_E;
  memcpy (key.key.ipMcast.sip, arg->src, sizeof (arg->src));
  memcpy (key.key.ipMcast.dip, arg->src, sizeof (arg->dst));
  key.key.ipMcast.vlanId = arg->vid;

  result = CRP (cpssDxChBrgFdbMacEntryDelete (0, &key));
  switch (result) {
  case GT_OK:           return ST_OK;
  case GT_BAD_PARAM:    return ST_BAD_VALUE;
  case GT_HW_ERROR:     return ST_HW_ERROR;
  case GT_OUT_OF_RANGE: return ST_BAD_VALUE;
  case GT_BAD_STATE:    return ST_BUSY;
  default:              return ST_HEX;
  }
}

static enum status
mac_mc_ip_add (const struct mc_ip_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_STC me;
  GT_STATUS result;

  memset (&me, 0, sizeof (me));

  me.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_IPV4_MCAST_E;
  memcpy (me.key.key.ipMcast.sip, arg->src, sizeof (arg->src));
  memcpy (me.key.key.ipMcast.dip, arg->src, sizeof (arg->dst));
  me.key.key.ipMcast.vlanId = arg->vid;

  me.isStatic = GT_TRUE;
  me.dstInterface.type = CPSS_INTERFACE_VIDX_E;
  me.dstInterface.vidx = arg->mcg;
  me.daCommand = CPSS_MAC_TABLE_FRWRD_E;
  me.saCommand = CPSS_MAC_TABLE_FRWRD_E;

  result = CRP (cpssDxChBrgFdbMacEntrySet (0, &me));
  switch (result) {
  case GT_OK:           return ST_OK;
  case GT_BAD_PARAM:    return ST_BAD_VALUE;
  case GT_HW_ERROR:     return ST_HW_ERROR;
  case GT_OUT_OF_RANGE: return ST_BAD_VALUE;
  case GT_BAD_STATE:    return ST_BUSY;
  default:              return ST_HEX;
  }
}

enum status
mac_mc_ip_op (const struct mc_ip_op_arg *arg)
{
  if (!vlan_valid (arg->vid))
    return ST_BAD_VALUE;

  if (arg->delete)
    return mac_mc_ip_delete (arg);
  else
    return mac_mc_ip_add (arg);
}

static enum status
mac_add (const struct mac_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_STC me;
  GT_STATUS result;

  memset (&me, 0, sizeof (me));
  me.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (me.key.key.macVlan.macAddr.arEther, arg->mac, sizeof (arg->mac));
  me.key.key.macVlan.vlanId = arg->vid;
  me.isStatic = GT_TRUE;

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

  result = CRP (cpssDxChBrgFdbMacEntrySet (0, &me));
  switch (result) {
  case GT_OK:        return ST_OK;
  case GT_HW_ERROR:  return ST_HW_ERROR;
  case GT_BAD_STATE: return ST_BUSY;
  default:           return ST_HEX;
  }
}

static enum status
mac_delete (const struct mac_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_KEY_STC key;
  GT_STATUS result;

  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (key.key.macVlan.macAddr.arEther, arg->mac, sizeof (arg->mac));
  key.key.macVlan.vlanId = arg->vid;

  result = CRP (cpssDxChBrgFdbMacEntryDelete (0, &key));
  switch (result) {
  case GT_OK:        return ST_OK;
  case GT_HW_ERROR:  return ST_HW_ERROR;
  case GT_BAD_STATE: return ST_BUSY;
  default:           return ST_HEX;
  }
}

enum status
mac_op (const struct mac_op_arg *arg)
{
  if (!vlan_valid (arg->vid))
    return ST_BAD_VALUE;

  if (arg->delete)
    return mac_delete (arg);
  else
    return mac_add (arg);
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
mac_list (void)
{
  CPSS_FDB_ACTION_MODE_ENT s_act_mode;
  CPSS_MAC_ACTION_MODE_ENT s_mac_mode;
  GT_U32 s_act_dev, s_act_dev_mask;
  GT_U16 s_act_vid, s_act_vid_mask;
  GT_U32 s_is_trunk, s_is_trunk_mask;
  GT_U32 s_port, s_port_mask, num, total;
  GT_BOOL done = GT_FALSE, end;
  CPSS_MAC_UPDATE_MSG_EXT_STC *ptr = fdb_addrs;

  CRP (cpssDxChBrgFdbActionModeGet (0, &s_act_mode));
  CRP (cpssDxChBrgFdbMacTriggerModeGet (0, &s_mac_mode));
  CRP (cpssDxChBrgFdbActionActiveDevGet (0, &s_act_dev, &s_act_dev_mask));
  CRP (cpssDxChBrgFdbActionActiveVlanGet (0, &s_act_vid, &s_act_vid_mask));
  CRP (cpssDxChBrgFdbActionActiveInterfaceGet
       (0, &s_is_trunk, &s_is_trunk_mask, &s_port, &s_port_mask));
  CRP (cpssDxChBrgFdbActionsEnableSet (0, GT_FALSE));

  CRP (cpssDxChBrgFdbActionActiveVlanSet (0, 0, 0));
  CRP (cpssDxChBrgFdbActionActiveInterfaceSet (0, 0, 0, 0, 0));
  CRP (cpssDxChBrgFdbUploadEnableSet (0, GT_TRUE));
  CRP (cpssDxChBrgFdbTrigActionStart (0, CPSS_FDB_ACTION_AGE_WITHOUT_REMOVAL_E));

  /* FIXME: use event system instead of polling. */
  while (1) {
    CRP (cpssDxChBrgFdbTrigActionStatusGet (0, &done));
    if (done)
      break;
    usleep (100);
  };

  CRP (cpssDxChBrgFdbUploadEnableSet (0, GT_FALSE));
  CRP (cpssDxChBrgFdbActionActiveInterfaceSet
       (0, s_is_trunk, s_is_trunk_mask, s_port, s_port_mask));
  CRP (cpssDxChBrgFdbActionActiveDevSet (0, s_act_dev, s_act_dev_mask));
  CRP (cpssDxChBrgFdbActionModeSet (0, s_act_mode));
  CRP (cpssDxChBrgFdbActionActiveVlanSet (0, s_act_vid, s_act_vid_mask));
  CRP (cpssDxChBrgFdbMacTriggerModeSet (0, s_mac_mode));
  CRP (cpssDxChBrgFdbActionsEnableSet (0, GT_TRUE));

  CRP (cpssDxChBrgFdbAuqFuqMessagesNumberGet
       (0, CPSS_DXCH_FDB_QUEUE_TYPE_FU_E, &total, &end));
  DEBUG ("FU entries: %u, end: %d\r", total, end);

  num = total;
  CRP (cpssDxChBrgFdbFuMsgBlockGet (0, &num, ptr));
  fdb_naddrs = num;
  DEBUG ("got %u MAC addrs\r\n", num);
  if (num < total) {
    ptr += num;
    num = total - num;
    CRP (cpssDxChBrgFdbFuMsgBlockGet (0, &num, ptr));
    DEBUG ("got %u MAC addrs more\r\n", num);
    fdb_naddrs += num;
  }
  DEBUG ("got %u MAC addrs total\r\n", fdb_naddrs);

  return ST_OK;
}

enum status
mac_flush (const struct mac_age_arg *arg, GT_BOOL del_static)
{
  CPSS_FDB_ACTION_MODE_ENT s_act_mode;
  CPSS_MAC_ACTION_MODE_ENT s_mac_mode;
  GT_U32 s_act_dev, s_act_dev_mask;
  GT_U16 s_act_vid, s_act_vid_mask, act_vid, act_vid_mask;
  GT_U32 s_is_trunk, s_is_trunk_mask;
  GT_U32 s_port, s_port_mask, port, port_mask;
  GT_BOOL done = GT_FALSE;

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
    port = 0;
    port_mask = 0;
  } else {
    if (!port_valid (arg->port))
      return ST_BAD_VALUE;
    port = port_ptr (arg->port)->lport;
    port_mask = 0x0000007F;
  }

  CRP (cpssDxChBrgFdbActionModeGet (0, &s_act_mode));
  CRP (cpssDxChBrgFdbMacTriggerModeGet (0, &s_mac_mode));
  CRP (cpssDxChBrgFdbActionActiveDevGet (0, &s_act_dev, &s_act_dev_mask));
  CRP (cpssDxChBrgFdbActionActiveVlanGet (0, &s_act_vid, &s_act_vid_mask));
  CRP (cpssDxChBrgFdbActionActiveInterfaceGet
       (0, &s_is_trunk, &s_is_trunk_mask, &s_port, &s_port_mask));
  CRP (cpssDxChBrgFdbActionsEnableSet (0, GT_FALSE));

  CRP (cpssDxChBrgFdbActionActiveVlanSet (0, act_vid, act_vid_mask));
  CRP (cpssDxChBrgFdbActionActiveInterfaceSet (0, 0, 0, port, port_mask));
  CRP (cpssDxChBrgFdbStaticDelEnable (0, del_static));
  CRP (cpssDxChBrgFdbTrigActionStart (0, CPSS_FDB_ACTION_DELETING_E));

  /* FIXME: use event system instead of polling. */
  do {
    CRP (cpssDxChBrgFdbTrigActionStatusGet (0, &done));
    usleep (100);
  } while (!done);

  CRP (cpssDxChBrgFdbActionActiveInterfaceSet
       (0, s_is_trunk, s_is_trunk_mask, s_port, s_port_mask));
  CRP (cpssDxChBrgFdbActionActiveDevSet (0, s_act_dev, s_act_dev_mask));
  CRP (cpssDxChBrgFdbActionModeSet (0, s_act_mode));
  CRP (cpssDxChBrgFdbActionActiveVlanSet (0, s_act_vid, s_act_vid_mask));
  CRP (cpssDxChBrgFdbMacTriggerModeSet (0, s_mac_mode));
  CRP (cpssDxChBrgFdbActionsEnableSet (0, GT_TRUE));

  return ST_OK;
}

/*
 * FDB management.
 */

struct fdb_entry fdb[FDB_MAX_ADDRS];

#define DYN_FDB_ENTRY 0
#define OWN_FDB_ENTRY 10

static inline int
me_key_eq (const CPSS_MAC_ENTRY_EXT_STC *a,
           const CPSS_MAC_ENTRY_EXT_STC *b)
{
  return ((a->key.key.macVlan.vlanId == b->key.key.macVlan.vlanId)
          && !memcmp (a->key.key.macVlan.macAddr.arEther,
                      b->key.key.macVlan.macAddr.arEther,
                      6));
}

static void
fdb_new_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u)
{
  GT_STATUS rc;
  GT_U32 idx;
  int i;

  /* DEBUG ("NA/SA msg: " MAC_FMT ", index %u, offset %u\r\n", */
  /*        MAC_ARG (u->macEntry.key.key.macVlan.macAddr.arEther), */
  /*        u->macEntryIndex, u->entryOffset); */

  rc = CRP (cpssDxChBrgFdbHashCalc (d, &u->macEntry.key, &idx));
  ON_GT_ERROR (rc) return;

  for (i = 0; i < 4; i++, idx++) {
    if (!fdb[idx].valid) {
      int d;

      /* DEBUG ("writing entry at %d\r\n", idx); */

      memcpy (&fdb[idx].me, &u->macEntry, sizeof (u->macEntry));
      fdb[idx].me.appSpecificCpuCode = GT_FALSE;
      fdb[idx].me.isStatic           = GT_FALSE;
      fdb[idx].me.daCommand          = CPSS_MAC_TABLE_FRWRD_E;
      fdb[idx].me.saCommand          = CPSS_MAC_TABLE_FRWRD_E;
      fdb[idx].me.daRoute            = GT_FALSE;
      fdb[idx].me.userDefined        = DYN_FDB_ENTRY;
      fdb[idx].valid = 1;
      for_each_dev (d)
        CRP (cpssDxChBrgFdbMacEntryWrite (d, idx, GT_FALSE, &fdb[idx].me));

      return;
    }
  }

  DEBUG ("no FDB space left\r\n");
}

static void
fdb_old_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u)
{
  GT_STATUS rc;
  GT_U32 idx;
  int i;

  DEBUG ("AA msg: " MAC_FMT ", VLAN %d\r\n",
         MAC_ARG (u->macEntry.key.key.macVlan.macAddr.arEther),
         u->macEntry.key.key.macVlan.vlanId);

  rc = CRP (cpssDxChBrgFdbHashCalc (d, &u->macEntry.key, &idx));
  ON_GT_ERROR (rc) return;

  for (i = 0; i < 4; i++, idx++) {
    if (fdb[idx].valid && me_key_eq (&u->macEntry, &fdb[idx].me)) {
      int d;

      DEBUG ("found entry at %u, removing\r\n", idx);
      for_each_dev (d)
        CRP (cpssDxChBrgFdbMacEntryInvalidate (d, idx));
      fdb[idx].valid = 0;

      return;
    }
  }

  DEBUG ("Aged entry not found!\r\n");
}

static volatile int fdb_thread_started = 0;

static void *
fdb_thread (void *_)
{
  void *fdb_sock;

  DEBUG ("starting up FDB\r\n");

  fdb_sock = zsocket_new (zcontext, ZMQ_PULL);
  assert (fdb_sock);
  zsocket_bind (fdb_sock, FDB_NOTIFY_EP);

  DEBUG ("FDB startup done\r\n");
  fdb_thread_started = 1;

  while (1) {
    GT_U32 num, total;
    GT_STATUS rc;
    CPSS_MAC_UPDATE_MSG_EXT_STC *ptr = fdb_addrs;
    int i;

    zmsg_t *msg = zmsg_recv (fdb_sock);
    zframe_t *frame = zmsg_first (msg);
    GT_U8 dev = *((GT_U8 *) zframe_data (frame));
    zmsg_destroy (&msg);

    total = 0;
    do {
      num = FDB_MAX_ADDRS - total;
      rc = cpssDxChBrgFdbAuMsgBlockGet (dev, &num, ptr);
      total += num;
      ptr += num;
    } while (rc == GT_OK && total < FDB_MAX_ADDRS);

    switch (rc) {
    case GT_OK:
    case GT_NO_MORE:
      /* DEBUG ("got %lu MAC addrs total\r\n", total); */
      for (i = 0; i < total; i++) {
        /* DEBUG ("AU msg type %d\r\n", fdb_addrs[i].updType); */
        switch (fdb_addrs[i].updType) {
        case CPSS_NA_E:
        case CPSS_SA_E:
          fdb_new_addr (dev, &fdb_addrs[i]);
          break;
        case CPSS_AA_E:
          fdb_old_addr (dev, &fdb_addrs[i]);
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
  int d;

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbAAandTAToCpuSet (d, GT_TRUE));
    CRP (cpssDxChBrgFdbSpAaMsgToCpuSet (d, GT_TRUE));
  }

  memset (fdb, 0, sizeof (fdb));
  pthread_create (&tid, NULL, fdb_thread, NULL);
  DEBUG ("waiting for FDB startup\r\n");
  int n = 0;
  while (!fdb_thread_started) {
    n++;
    usleep (10000);
  }
  DEBUG ("FDB startup finished after %d iteractions\r\n", n);

#ifdef DEBUG_LIST_MACS
  int i, j;
  for (i = 0; i < 250; i++) {
    for (j = 0; j < 250; j++) {
      struct mac_op_arg arg = {
        .vid = 1,
        .port = 10,
        .drop = 0,
        .delete = 0,
        .mac = { 0, 1, 2, 3, i, j }
      };
      mac_add (&arg);
    }
  }

  return mac_flush (&arg, GT_FALSE);
#else /* !DEBUG_LIST_MACS */
  return mac_flush (&arg, GT_TRUE);
#endif /* DEBUG_LIST_MACS */
}
