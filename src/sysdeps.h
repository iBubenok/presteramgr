#ifndef __SYSDEPS_H__
#define __SYSDEPS_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>
#include <gtExtDrv/drivers/gtPciDrv.h>
#include <cpss/dxCh/dxChxGen/cpssHwInit/cpssDxChHwInit.h>

#include <control-proto.h>

#define FDB_MAX_ADDRS 16384

struct dev_info {
  uint32_t dev_id;
  GT_PCI_INT int_num;
  CPSS_DXCH_PP_PHASE1_INIT_INFO_STC ph1_info;
};
#define DECLARE_DEV_INFO(name) struct dev_info name[]

struct pm {
  uint8_t dev;
  uint8_t port;
};

#if defined (VARIANT_ARLAN_3424FE) || defined (VARIANT_ARLAN_3424PFE)

#define DEV_INFO                                                                \
  {                                                                             \
    {                                                                           \
      .dev_id   = CPSS_98DX2122_CNS,                                            \
      .int_num  = GT_PCI_INT_B,                                                 \
      .ph1_info = {                                                             \
        .devNum                 = 0,                                            \
        .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,         \
        .mngInterfaceType       = CPSS_CHANNEL_PEX_E,                           \
        .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,                    \
        .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_INTERNAL_125_E, \
        .initSerdesDefaults     = GT_TRUE,                                      \
        .isExternalCpuConnected = GT_FALSE                                      \
      }                                                                         \
    }                                                                           \
  }

#define NDEVS 1
#define NPORTS 28
#define DECLARE_PORT_MAP(name)                     \
  struct pm name[NPORTS] = {                       \
    {0, 1},  {0, 0},  {0, 3},  {0, 2},             \
    {0, 5},  {0, 4},  {0, 7},  {0, 6},             \
    {0, 9},  {0, 8},  {0, 11}, {0, 10},            \
    {0, 13}, {0, 12}, {0, 15}, {0, 14},            \
    {0, 17}, {0, 16}, {0, 19}, {0, 18},            \
    {0, 21}, {0, 20}, {0, 23}, {0, 22},            \
    {0, 26}, {0, 27}, {0, 24}, {0, 25}             \
  }

static inline int IS_FE_PORT (int n)
{
  return n >= 0 && n < 24;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 24 && n < 28;
}

static inline int IS_XG_PORT (int n)
{
  return 0;
}

#elif defined (VARIANT_SM_12F)

#define DEV_INFO                                                                \
  {                                                                             \
    {                                                                           \
      .dev_id   = CPSS_98DX2122_CNS,                                            \
      .int_num  = GT_PCI_INT_B,                                                 \
      .ph1_info = {                                                             \
        .devNum                 = 0,                                            \
        .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,         \
        .mngInterfaceType       = CPSS_CHANNEL_PEX_E,                           \
        .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,                    \
        .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_INTERNAL_125_E, \
        .initSerdesDefaults     = GT_TRUE,                                      \
        .isExternalCpuConnected = GT_FALSE                                      \
      }                                                                         \
    }                                                                           \
  }

#define NDEVS 1
#define NPORTS 16
#define DECLARE_PORT_MAP(name)                              \
  struct pm name[NPORTS] = {                                \
    {0, 12}, {0, 13}, {0, 14}, {0, 15},                     \
    {0, 16}, {0, 17}, {0, 18}, {0, 19},                     \
    {0, 20}, {0, 21}, {0, 22}, {0, 23},                     \
    {0, 27}, {0, 26}, {0, 25}, {0, 24}                      \
  }

static inline int IS_FE_PORT (int n)
{
  return n >= 0 && n < 12;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 12 && n < 16;
}

static inline int IS_XG_PORT (int n)
{
  return 0;
}

#elif defined (VARIANT_ARLAN_3424GE)

#define DEV_INFO                                                                \
  {                                                                             \
    {                                                                           \
      .dev_id   = CPSS_98DX4122_CNS,                                            \
      .int_num  = GT_PCI_INT_B,                                                 \
      .ph1_info = {                                                             \
        .devNum                 = 0,                                            \
        .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,         \
        .mngInterfaceType       = CPSS_CHANNEL_PEX_E,                           \
        .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,                    \
        .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_INTERNAL_125_E, \
        .initSerdesDefaults     = GT_TRUE,                                      \
        .isExternalCpuConnected = GT_FALSE                                      \
      }                                                                         \
    }                                                                           \
  }

#define NDEVS 1
#define NPORTS 28
#define DECLARE_PORT_MAP(name)                  \
  struct pm name[NPORTS] = {                    \
    {0, 0},  {0, 1},  {0, 2},  {0, 3},          \
    {0, 4},  {0, 5},  {0, 6},  {0, 7},          \
    {0, 8},  {0, 9},  {0, 10}, {0, 11},         \
    {0, 12}, {0, 13}, {0, 14}, {0, 15},         \
    {0, 16}, {0, 17}, {0, 18}, {0, 19},         \
    {0, 20}, {0, 21}, {0, 22}, {0, 23},         \
    {0, 26}, {0, 27}, {0, 24}, {0, 25}          \
  }

static inline int IS_FE_PORT (int n)
{
  return 0;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 0 && n < 24;
}

static inline int IS_XG_PORT (int n)
{
  return n >= 24 && n < 28;
}

#elif defined (VARIANT_ARLAN_3448PGE)

#define DEV_INFO                                                                     \
  {                                                                                  \
    {                                                                                \
      .dev_id   = CPSS_98DX5248_CNS,                                                 \
      .int_num  = GT_PCI_INT_A,                                                      \
      .ph1_info = {                                                                  \
        .devNum                 = 0,                                                 \
        .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,              \
        .mngInterfaceType       = CPSS_CHANNEL_PEX_E,                                \
        .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,                         \
        .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_EXTERNAL_125_DIFF_E, \
        .initSerdesDefaults     = GT_TRUE,                                           \
        .isExternalCpuConnected = GT_FALSE                                           \
      }                                                                              \
    },                                                                               \
    {                                                                                \
      .dev_id   = CPSS_98DX4122_CNS,                                                 \
      .int_num  = GT_PCI_INT_B,                                                      \
      .ph1_info = {                                                                  \
        .devNum                 = 1,                                                 \
        .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,              \
        .mngInterfaceType       = CPSS_CHANNEL_PEX_E,                                \
        .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,                         \
        .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_EXTERNAL_125_DIFF_E, \
        .initSerdesDefaults     = GT_TRUE,                                           \
        .isExternalCpuConnected = GT_FALSE                                           \
      }                                                                              \
    }                                                                                \
  }

#define NDEVS 2
#define NPORTS 48
#define DECLARE_PORT_MAP(name)                  \
  struct pm name[NPORTS] = {                    \
    {1, 0},  {1, 1},  {1, 2},  {1, 3},          \
    {1, 4},  {1, 5},  {1, 6},  {1, 7},          \
    {1, 8},  {1, 9},  {1, 10}, {1, 11},         \
    {1, 12}, {1, 13}, {1, 14}, {1, 15},         \
    {1, 16}, {1, 17}, {1, 18}, {1, 19},         \
    {1, 20}, {1, 21}, {1, 22}, {1, 23},         \
    {0, 0},  {0, 1},  {0, 2},  {0, 3},          \
    {0, 4},  {0, 5},  {0, 6},  {0, 7},          \
    {0, 8},  {0, 9},  {0, 10}, {0, 11},         \
    {0, 12}, {0, 13}, {0, 14}, {0, 15},         \
    {0, 16}, {0, 17}, {0, 18}, {0, 19},         \
    {0, 20}, {0, 21}, {0, 22}, {0, 23},         \
  }


static inline int IS_FE_PORT (int n)
{
  return 0;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 0 && n < 48;
}

static inline int IS_XG_PORT (int n)
{
  return n >= 48 && n < 52;
}

#else
#error Undefined or unsupported variant.
#endif /* VARIANT_* */

extern size_t sysdeps_default_stack_size;

#define SYSD_CSCD_TRUNK 1
extern void sysd_setup_ic (void);
extern void sysd_vlan_add (vid_t);


#endif /* __SYSDEPS_H__ */
