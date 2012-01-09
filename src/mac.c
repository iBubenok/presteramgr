#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>

#include <mac.h>
#include <port.h>
#include <vlan.h>
#include <debug.h>


/*
 * TODO: maintain shadow FDB.
 */

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
    me.dstInterface.devPort.devNum = port->ldev;
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

  if (!port_valid (arg->port))
    return ST_BAD_VALUE;

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

  rc = CRP (cpssDxChBrgFdbAgingTimeoutSet (0, time));

  switch (rc) {
  case GT_OK:        return ST_OK;
  case GT_BAD_PARAM: return ST_BAD_VALUE;
  case GT_HW_ERROR:  return ST_HW_ERROR;
  default:           return ST_HEX;
  }
}

enum status
mac_list (void)
{
  CPSS_FDB_ACTION_MODE_ENT s_act_mode;
  CPSS_MAC_ACTION_MODE_ENT s_mac_mode;
  GT_U32 s_act_dev, s_act_dev_mask;
  GT_U16 s_act_vid, s_act_vid_mask;
  GT_U32 s_is_trunk, s_is_trunk_mask;
  GT_U32 s_port, s_port_mask;
  GT_BOOL done = GT_FALSE;
  GT_U32 naddrs = 20000, i;
  static CPSS_MAC_UPDATE_MSG_EXT_STC addrs[20000];

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
  do {
    CRP (cpssDxChBrgFdbTrigActionStatusGet (0, &done));
    usleep (100);
  } while (!done);

  CRP (cpssDxChBrgFdbUploadEnableSet (0, GT_FALSE));
  CRP (cpssDxChBrgFdbActionActiveInterfaceSet
       (0, s_is_trunk, s_is_trunk_mask, s_port, s_port_mask));
  CRP (cpssDxChBrgFdbActionActiveDevSet (0, s_act_dev, s_act_dev_mask));
  CRP (cpssDxChBrgFdbActionModeSet (0, s_act_mode));
  CRP (cpssDxChBrgFdbActionActiveVlanSet (0, s_act_vid, s_act_vid_mask));
  CRP (cpssDxChBrgFdbMacTriggerModeSet (0, s_mac_mode));
  CRP (cpssDxChBrgFdbActionsEnableSet (0, GT_TRUE));

  CRP (cpssDxChBrgFdbFuMsgBlockGet (0, &naddrs, addrs));
  fprintf (stderr, "*** GOT %lu MAC addrs\r\n", naddrs);

  for (i = 0; i < naddrs; i++) {
    port_num_t n = 0;

    if (!addrs[i].skip) {
      fprintf (stderr, "aging: %d\r\n", addrs[i].aging);

      if (((addrs[i].updType == CPSS_FU_E) &&
           (addrs[i].macEntry.key.entryType == CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E) &&
           (addrs[i].macEntry.dstInterface.type == CPSS_INTERFACE_PORT_E) &&
           (n = port_num (addrs[i].macEntry.dstInterface.devPort.devNum,
                          addrs[i].macEntry.dstInterface.devPort.portNum))) ||
          addrs[i].aging) {
        fprintf (stderr, "VLAN %4d, port %2d: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                 addrs[i].macEntry.key.key.macVlan.vlanId, n,
                 addrs[i].macEntry.key.key.macVlan.macAddr.arEther[0],
                 addrs[i].macEntry.key.key.macVlan.macAddr.arEther[1],
                 addrs[i].macEntry.key.key.macVlan.macAddr.arEther[2],
                 addrs[i].macEntry.key.key.macVlan.macAddr.arEther[3],
                 addrs[i].macEntry.key.key.macVlan.macAddr.arEther[4],
                 addrs[i].macEntry.key.key.macVlan.macAddr.arEther[5]);
      }
    }
  }

  return ST_OK;
}

enum status
mac_flush_dynamic (const struct mac_age_arg *arg)
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

  CRP (cpssDxChBrgFdbAAandTAToCpuSet (0, GT_FALSE));

  CRP (cpssDxChBrgFdbActionModeGet (0, &s_act_mode));
  CRP (cpssDxChBrgFdbMacTriggerModeGet (0, &s_mac_mode));
  CRP (cpssDxChBrgFdbActionActiveDevGet (0, &s_act_dev, &s_act_dev_mask));
  CRP (cpssDxChBrgFdbActionActiveVlanGet (0, &s_act_vid, &s_act_vid_mask));
  CRP (cpssDxChBrgFdbActionActiveInterfaceGet
       (0, &s_is_trunk, &s_is_trunk_mask, &s_port, &s_port_mask));
  CRP (cpssDxChBrgFdbActionsEnableSet (0, GT_FALSE));

  CRP (cpssDxChBrgFdbActionActiveVlanSet (0, act_vid, act_vid_mask));
  CRP (cpssDxChBrgFdbActionActiveInterfaceSet (0, 0, 0, port, port_mask));
  CRP (cpssDxChBrgFdbStaticDelEnable (0, GT_FALSE));
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
