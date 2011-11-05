#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <control-proto.h>
#include <data.h>

int
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

int
data_decode_stp_state (CPSS_STP_STATE_ENT *cs, enum port_stp_state state)
{
  static CPSS_STP_STATE_ENT csm[] = {
    [STP_STATE_DISABLED] = CPSS_STP_DISABLED_E,
    [STP_STATE_DISCARDING] = CPSS_STP_BLCK_LSTN_E,
    [STP_STATE_LEARNING] =  CPSS_STP_LRN_E,
    [STP_STATE_FORWARDING] = CPSS_STP_FRWRD_E
  };

  if (state < 0 || state >= STP_STATE_MAX)
    return EC_BAD_VALUE;

  *cs = csm[state];

  return 0;
}
