#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>

#include <presteramgr.h>
#include <debug.h>
#include <string.h>

int
vlan_init (void)
{
  GT_STATUS rc;

  rc = CRP (cpssDxChBrgVlanBridgingModeSet (0, CPSS_BRG_MODE_802_1D_E));

  return rc != GT_OK;
}

GT_STATUS
vlan_set_mac_addr (GT_U16 vid, const unsigned char *addr)
{
  CPSS_MAC_ENTRY_EXT_STC mac_entry;
  GT_STATUS rc;

  rc = CRP (cpssDxChBrgVlanIpCntlToCpuSet
            (0, vid, CPSS_DXCH_BRG_IP_CTRL_IPV4_IPV6_E));
  if (rc != GT_OK)
    return rc;

  memset (&mac_entry, 0, sizeof (mac_entry));
  mac_entry.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  mac_entry.key.key.macVlan.vlanId = vid;
  memcpy (mac_entry.key.key.macVlan.macAddr.arEther, addr, 6);
  mac_entry.dstInterface.type = CPSS_INTERFACE_PORT_E;
  mac_entry.dstInterface.devPort.devNum = 0;
  mac_entry.dstInterface.devPort.portNum = 63;
  mac_entry.isStatic = GT_TRUE;
  mac_entry.daCommand = CPSS_MAC_TABLE_CNTL_E;
  mac_entry.saCommand = CPSS_MAC_TABLE_FRWRD_E;
  return CRP (cpssDxChBrgFdbMacEntrySet (0, &mac_entry));
}
