#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <data.h>
#include <mac.h>
#include <port.h>

enum status
data_encode_port_state (struct port_link_state *state,
                        const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  static enum port_speed psm[] = {
    [CPSS_PORT_SPEED_10_E]    = PORT_SPEED_10,
    [CPSS_PORT_SPEED_100_E]   = PORT_SPEED_100,
    [CPSS_PORT_SPEED_1000_E]  = PORT_SPEED_1000,
    [CPSS_PORT_SPEED_10000_E] = PORT_SPEED_10000,
    [CPSS_PORT_SPEED_12000_E] = PORT_SPEED_12000,
    [CPSS_PORT_SPEED_2500_E]  = PORT_SPEED_2500,
    [CPSS_PORT_SPEED_5000_E]  = PORT_SPEED_5000,
    [CPSS_PORT_SPEED_13600_E] = PORT_SPEED_13600,
    [CPSS_PORT_SPEED_20000_E] = PORT_SPEED_20000,
    [CPSS_PORT_SPEED_40000_E] = PORT_SPEED_40000,
    [CPSS_PORT_SPEED_16000_E] = PORT_SPEED_16000,
    [CPSS_PORT_SPEED_NA_E]    = PORT_SPEED_NA
  };

  state->link = attrs->portLinkUp == GT_TRUE;
  state->duplex = attrs->portDuplexity == CPSS_PORT_FULL_DUPLEX_E;
  assert (attrs->portSpeed >= 0 && attrs->portSpeed <= CPSS_PORT_SPEED_NA_E);
  state->speed = psm[attrs->portSpeed];

  return 0;
}

enum status
data_decode_port_speed (CPSS_PORT_SPEED_ENT *outp, enum port_speed inp)
{
  static CPSS_PORT_SPEED_ENT psm[] = {
    [PORT_SPEED_10]    = CPSS_PORT_SPEED_10_E,
    [PORT_SPEED_100]   = CPSS_PORT_SPEED_100_E,
    [PORT_SPEED_1000]  = CPSS_PORT_SPEED_1000_E,
    [PORT_SPEED_10000] = CPSS_PORT_SPEED_10000_E,
    [PORT_SPEED_12000] = CPSS_PORT_SPEED_12000_E,
    [PORT_SPEED_2500]  = CPSS_PORT_SPEED_2500_E,
    [PORT_SPEED_5000]  = CPSS_PORT_SPEED_5000_E,
    [PORT_SPEED_13600] = CPSS_PORT_SPEED_13600_E,
    [PORT_SPEED_20000] = CPSS_PORT_SPEED_20000_E,
    [PORT_SPEED_40000] = CPSS_PORT_SPEED_40000_E,
    [PORT_SPEED_16000] = CPSS_PORT_SPEED_16000_E
  };

  if (inp < PORT_SPEED_10 || inp >= PORT_SPEED_NA)
    return ST_BAD_VALUE;

  *outp = psm[inp];
  return ST_OK;
}

enum status
data_decode_stp_state (CPSS_STP_STATE_ENT *cs, enum port_stp_state state)
{
  static CPSS_STP_STATE_ENT csm[] = {
    [STP_STATE_DISABLED] = CPSS_STP_DISABLED_E,
    [STP_STATE_DISCARDING] = CPSS_STP_BLCK_LSTN_E,
    [STP_STATE_LEARNING] =  CPSS_STP_LRN_E,
    [STP_STATE_FORWARDING] = CPSS_STP_FRWRD_E
  };

  if (state < 0 || state >= STP_STATE_MAX)
    return ST_BAD_VALUE;

  *cs = csm[state];

  return ST_OK;
}

void
data_encode_fdb_addrs (zmsg_t *msg)
{
  GT_U32 i;

  for (i = 0; i < fdb_naddrs; i++) {
    if (!fdb_addrs[i].skip &&
        fdb_addrs[i].updType == CPSS_FU_E &&
        (fdb_addrs[i].aging || fdb_addrs[i].macEntry.isStatic) &&
        fdb_addrs[i].macEntry.key.entryType == CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E &&
        fdb_addrs[i].macEntry.dstInterface.type == CPSS_INTERFACE_PORT_E &&
        port_id (fdb_addrs[i].macEntry.dstInterface.devPort.devNum,
                 fdb_addrs[i].macEntry.dstInterface.devPort.portNum)) {
      struct mac_entry me;

      memcpy (me.mac, fdb_addrs[i].macEntry.key.key.macVlan.macAddr.arEther, 6);
      me.dynamic = !fdb_addrs[i].macEntry.isStatic;

      zmsg_addmem (msg, &me, sizeof (me));
    }
  }
}
