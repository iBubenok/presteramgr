#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <data.h>
#include <mac.h>
#include <port.h>
#include <log.h>

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
data_encode_fdb_addrs (zmsg_t *msg, vid_t vid)
{
  GT_U32 i;
  port_id_t pid;

  for (i = 0; i < fdb_naddrs; i++) {
    if (!fdb_addrs[i].skip &&
        fdb_addrs[i].updType == CPSS_FU_E &&
        (fdb_addrs[i].aging || fdb_addrs[i].macEntry.isStatic) &&
        fdb_addrs[i].macEntry.key.entryType == CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E &&
        (vid == ALL_VLANS || fdb_addrs[i].macEntry.key.key.macVlan.vlanId == vid) &&
        fdb_addrs[i].macEntry.dstInterface.type == CPSS_INTERFACE_PORT_E &&
        (pid = port_id (fdb_addrs[i].macEntry.dstInterface.devPort.devNum,
                        fdb_addrs[i].macEntry.dstInterface.devPort.portNum))) {
      struct {
        struct mac_entry me;
        port_id_t ports[1];
      } __attribute__ ((packed)) tmp;

      memcpy (tmp.me.mac, fdb_addrs[i].macEntry.key.key.macVlan.macAddr.arEther, 6);
      tmp.me.vid = fdb_addrs[i].macEntry.key.key.macVlan.vlanId;
      tmp.me.dynamic = !fdb_addrs[i].macEntry.isStatic;
      tmp.ports[0] = pid;

      zmsg_addmem (msg, &tmp, sizeof (tmp));
    }
  }
}

void
data_encode_vct_cable_status (struct vct_cable_status *d,
                              const CPSS_VCT_CABLE_STATUS_STC *s,
                              int fe)
{
  int i, num = fe ? 2 : 4;
  static const uint8_t tsm[] = {
    [CPSS_VCT_TEST_FAIL_E]          = VS_TEST_FAILED,
    [CPSS_VCT_NORMAL_CABLE_E]       = VS_NORMAL_CABLE,
    [CPSS_VCT_OPEN_CABLE_E]         = VS_OPEN_CABLE,
    [CPSS_VCT_SHORT_CABLE_E]        = VS_SHORT_CABLE,
    [CPSS_VCT_IMPEDANCE_MISMATCH_E] = VS_IMPEDANCE_MISMATCH,
    [CPSS_VCT_SHORT_WITH_PAIR0_E]   = VS_SHORT_WITH_PAIR0,
    [CPSS_VCT_SHORT_WITH_PAIR1_E]   = VS_SHORT_WITH_PAIR1,
    [CPSS_VCT_SHORT_WITH_PAIR2_E]   = VS_SHORT_WITH_PAIR2,
    [CPSS_VCT_SHORT_WITH_PAIR3_E]   = VS_SHORT_WITH_PAIR3
  };
  static const uint8_t clm[] = {
    [CPSS_VCT_LESS_THAN_50M_E] = CL_LESS_THAN_50M,
    [CPSS_VCT_50M_80M_E]       = CL_50M_80M,
    [CPSS_VCT_80M_110M_E]      = CL_80M_110M,
    [CPSS_VCT_110M_140M_E]     = CL_110M_140M,
    [CPSS_VCT_MORE_THAN_140_E] = CL_MORE_THAN_140M,
    [CPSS_VCT_UNKNOWN_LEN_E]   = CL_UNKNOWN
  };

  d->ok = 1;
  d->npairs = num;
  for (i = 0; i < num; i++) {
    d->pair_status[i].status = tsm[s->cableStatus[i].testStatus];
    d->pair_status[i].length = s->cableStatus[i].errCableLen;
    if (s->cableStatus[i].testStatus != CPSS_VCT_NORMAL_CABLE_E)
      d->ok = 0;
  }

  d->length = d->ok ? clm[s->normalCableLen] : CL_UNKNOWN;

  switch (s->phyType) {
  case CPSS_VCT_PHY_100M_E:
    d->phy_type = PT_100;
    break;
  case  CPSS_VCT_PHY_10000M_E:
    d->phy_type = PT_10000;
    break;
  default:
    d->phy_type = PT_1000;
  }
}
