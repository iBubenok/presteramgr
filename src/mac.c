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
    fprintf (stderr, "drop mac!\r\n");
    me.dstInterface.type = CPSS_INTERFACE_VID_E;
    me.dstInterface.vlanId = arg->vid;

    me.daCommand = CPSS_MAC_TABLE_DROP_E;
    me.saCommand = CPSS_MAC_TABLE_DROP_E;
  } else {
    fprintf (stderr, "add mac!\r\n");

    if (!port_valid (arg->port))
      return ST_BAD_VALUE;

    me.dstInterface.type = CPSS_INTERFACE_PORT_E;
    me.dstInterface.devPort.devNum = ports[arg->port].ldev;
    me.dstInterface.devPort.portNum = ports[arg->port].lport;

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

  fprintf (stderr, "delete mac!\r\n");

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
