#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdint.h>

#include <gtOs/gtOsGen.h>
#include <cpss/generic/cpssCommonDefs.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/generic/port/cpssPortCtrl.h>


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
