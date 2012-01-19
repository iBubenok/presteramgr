#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdint.h>
#include <stdarg.h>

#include <gtOs/gtOsGen.h>
#include <cpss/generic/cpssCommonDefs.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/generic/port/cpssPortCtrl.h>
#include <debug.h>


static const char *invalid = "invalid";
#define str(s, v) ({                                                \
      uint32_t __v = (uint32_t) v;                                  \
      const char *__s = invalid;                                    \
      if (!(__v >= sizeof (s) / sizeof (s[0]) || s[__v] == NULL))   \
        __s = s[__v];                                               \
      __s;                                                          \
    })

#define DEFSHOW(type, maps...)                       \
  const char *show_##type (type v)                   \
  {                                                  \
    static const char *s[] = {maps};                 \
    return str (s, v);                               \
  }

#define S(e) [e] = #e

DEFSHOW (GT_BOOL,
         S (GT_FALSE),
         S (GT_TRUE));

DEFSHOW (CPSS_PACKET_CMD_ENT,
         S (CPSS_PACKET_CMD_FORWARD_E),
         S (CPSS_PACKET_CMD_MIRROR_TO_CPU_E),
         S (CPSS_PACKET_CMD_TRAP_TO_CPU_E),
         S (CPSS_PACKET_CMD_DROP_HARD_E),
         S (CPSS_PACKET_CMD_DROP_SOFT_E),
         S (CPSS_PACKET_CMD_ROUTE_E),
         S (CPSS_PACKET_CMD_ROUTE_AND_MIRROR_E),
         S (CPSS_PACKET_CMD_BRIDGE_AND_MIRROR_E),
         S (CPSS_PACKET_CMD_BRIDGE_E),
         S (CPSS_PACKET_CMD_NONE_E));

DEFSHOW (CPSS_DXCH_BRG_IP_CTRL_TYPE_ENT,
         S (CPSS_DXCH_BRG_IP_CTRL_NONE_E),
         S (CPSS_DXCH_BRG_IP_CTRL_IPV4_E),
         S (CPSS_DXCH_BRG_IP_CTRL_IPV6_E),
         S (CPSS_DXCH_BRG_IP_CTRL_IPV4_IPV6_E));

DEFSHOW (CPSS_BRG_IPM_MODE_ENT,
         S (CPSS_BRG_IPM_SGV_E),
         S (CPSS_BRG_IPM_GV_E));

DEFSHOW (CPSS_IP_SITE_ID_ENT,
         S (CPSS_IP_SITE_ID_INTERNAL_E),
         S (CPSS_IP_SITE_ID_EXTERNAL_E));

DEFSHOW (CPSS_DXCH_BRG_VLAN_FLOOD_VIDX_MODE_ENT,
         S (CPSS_DXCH_BRG_VLAN_FLOOD_VIDX_MODE_UNREG_MC_E),
         S (CPSS_DXCH_BRG_VLAN_FLOOD_VIDX_MODE_ALL_FLOODED_TRAFFIC_E));

DEFSHOW (CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_CMD_ENT,
         S (CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_DISABLE_CMD_E),
         S (CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_L2_CMD_E),
         S (CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_L3_CMD_E),
         S (CPSS_DXCH_BRG_VLAN_PORT_ISOLATION_L2_L3_CMD_E));

DEFSHOW (CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT,
         S (CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E),
         S (CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E),
         S (CPSS_DXCH_BRG_VLAN_PORT_TAG1_CMD_E),
         S (CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E),
         S (CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG1_INNER_TAG0_CMD_E),
         S (CPSS_DXCH_BRG_VLAN_PORT_PUSH_TAG0_CMD_E),
         S (CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E));

DEFSHOW (CPSS_PORT_SPEED_ENT,
         S (CPSS_PORT_SPEED_10_E),
         S (CPSS_PORT_SPEED_100_E),
         S (CPSS_PORT_SPEED_1000_E),
         S (CPSS_PORT_SPEED_10000_E),
         S (CPSS_PORT_SPEED_12000_E),
         S (CPSS_PORT_SPEED_2500_E),
         S (CPSS_PORT_SPEED_5000_E),
         S (CPSS_PORT_SPEED_13600_E),
         S (CPSS_PORT_SPEED_20000_E),
         S (CPSS_PORT_SPEED_40000_E),
         S (CPSS_PORT_SPEED_16000_E),
         S (CPSS_PORT_SPEED_NA_E));

DEFSHOW (CPSS_PORT_DUPLEX_ENT,
         S (CPSS_PORT_FULL_DUPLEX_E),
         S (CPSS_PORT_HALF_DUPLEX_E));

DEFSHOW (GT_STATUS,
         S (GT_OK),
         S (GT_FAIL),
         S (GT_BAD_VALUE),
         S (GT_OUT_OF_RANGE),
         S (GT_BAD_PARAM),
         S (GT_BAD_PTR),
         S (GT_BAD_SIZE),
         S (GT_BAD_STATE),
         S (GT_SET_ERROR),
         S (GT_GET_ERROR),
         S (GT_CREATE_ERROR),
         S (GT_NOT_FOUND),
         S (GT_NO_MORE),
         S (GT_NO_SUCH),
         S (GT_TIMEOUT),
         S (GT_NO_CHANGE),
         S (GT_NOT_SUPPORTED),
         S (GT_NOT_IMPLEMENTED),
         S (GT_NOT_INITIALIZED),
         S (GT_NO_RESOURCE),
         S (GT_FULL),
         S (GT_EMPTY),
         S (GT_INIT_ERROR),
         S (GT_NOT_READY),
         S (GT_ALREADY_EXIST),
         S (GT_OUT_OF_CPU_MEM),
         S (GT_ABORTED),
         S (GT_NOT_APPLICABLE_DEVICE),
         /* S (GT_UNFIXABLE_ECC_ERROR), */
         /* S (GT_UNFIXABLE_BIST_ERROR), */
         S (GT_CHECKSUM_ERROR),
         S (GT_DSA_PARSING_ERROR));

static int
msg_print_early (const char *format, ...)
{
  va_list ap;
  int result;

  va_start (ap, format);
  result = vfprintf (stderr, format, ap);
  va_end (ap);

  return result;
}

int msg_min_prio = MSG_MIN_PRIO;
int (*msg_out) (const char *, ...) = msg_print_early;

void
msg_set_output_function (int (*func) (const char *, ...))
{
  msg_out = func;
}
