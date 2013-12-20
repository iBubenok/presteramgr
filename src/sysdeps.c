#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <cpss/dxCh/dxChxGen/cscd/cpssDxChCscd.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/dxCh/dxChxGen/trunk/cpssDxChTrunk.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgStp.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>

#include <pthread.h>
#include <string.h>

#include <sysdeps.h>
#include <log.h>
#include <debug.h>

size_t sysdeps_default_stack_size;

static void __attribute__ ((constructor))
get_system_params (void)
{
  pthread_attr_t attr;

  pthread_attr_init (&attr);
  pthread_attr_getstacksize (&attr, &sysdeps_default_stack_size);
}

#if defined (VARIANT_ARLAN_3424FE) || defined (VARIANT_ARLAN_3424PFE)

static struct dev_info __dev_info[] = {
  {
    .dev_id   = CPSS_98DX2122_CNS,
    .int_num  = GT_PCI_INT_B,
    .ph1_info = {
      .devNum                 = 0,
      .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,
      .mngInterfaceType       = CPSS_CHANNEL_PEX_E,
      .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,
      .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_INTERNAL_125_E,
      .initSerdesDefaults     = GT_TRUE,
      .isExternalCpuConnected = GT_FALSE
    }
  }
};

void
sysd_setup_ic (void)
{
}

void
sysd_vlan_add (vid_t vid)
{
}

#elif defined (VARIANT_SM_12F)

static struct dev_info __dev_info[] = {
  {
    .dev_id   = CPSS_98DX2122_CNS,
    .int_num  = GT_PCI_INT_B,
    .ph1_info = {
      .devNum                 = 0,
      .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,
      .mngInterfaceType       = CPSS_CHANNEL_PEX_E,
      .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,
      .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_INTERNAL_125_E,
      .initSerdesDefaults     = GT_TRUE,
      .isExternalCpuConnected = GT_FALSE
    }
  }
};

void
sysd_setup_ic (void)
{
}

void
sysd_vlan_add (vid_t vid)
{
}

#elif defined (VARIANT_ARLAN_3424GE)

static struct dev_info __dev_info[] = {
  {
    .dev_id   = CPSS_98DX4122_CNS,
    .int_num  = GT_PCI_INT_B,
    .ph1_info = {
      .devNum                 = 0,
      .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,
      .mngInterfaceType       = CPSS_CHANNEL_PEX_E,
      .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,
      .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_INTERNAL_125_E,
      .initSerdesDefaults     = GT_TRUE,
      .isExternalCpuConnected = GT_FALSE
    }
  }
};

void
sysd_setup_ic (void)
{
}

void
sysd_vlan_add (vid_t vid)
{
}

#elif defined (VARIANT_ARLAN_3448PGE)

static struct dev_info __dev_info[] = {
  {
    .dev_id   = CPSS_98DX5248_CNS,
    .int_num  = GT_PCI_INT_A,
    .ph1_info = {
      .devNum                 = 0,
      .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,
      .mngInterfaceType       = CPSS_CHANNEL_PEX_E,
      .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,
      .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_EXTERNAL_125_DIFF_E,
      .initSerdesDefaults     = GT_TRUE,
      .isExternalCpuConnected = GT_FALSE
    }
  },
  {
    .dev_id   = CPSS_98DX4122_CNS,
    .int_num  = GT_PCI_INT_B,
    .ph1_info = {
      .devNum                 = 1,
      .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,
      .mngInterfaceType       = CPSS_CHANNEL_PEX_E,
      .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,
      .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_EXTERNAL_125_DIFF_E,
      .initSerdesDefaults     = GT_TRUE,
      .isExternalCpuConnected = GT_FALSE
    }
  }
};

void
sysd_setup_ic (void)
{
  CPSS_CSCD_LINK_TYPE_STC cl = {
    .linkNum  = SYSD_CSCD_TRUNK,
    .linkType = CPSS_CSCD_LINK_TYPE_TRUNK_E
  };
  GT_U8 dp[2][2] = {
    {26, 27},
    {24, 25}
  };
  int d, p;

  for (d = 0; d < 2; d++) {
    CPSS_PORTS_BMP_STC tp;

    memset (&tp, 0, sizeof (tp));

    for (p = 0; p < 2; p++) {
      stp_id_t stg;

      CPSS_PORTS_BMP_PORT_SET_MAC (&tp, dp[d][p]);

      CRP (cpssDxChCscdPortTypeSet
           (d, dp[d][p], CPSS_CSCD_PORT_DSA_MODE_EXTEND_E));
      CRP (cpssDxChPortInterfaceModeSet
           (d, dp[d][p], CPSS_PORT_INTERFACE_MODE_XGMII_E));
      CRP (cpssDxChPortSpeedSet
           (d, dp[d][p], CPSS_PORT_SPEED_10000_E));
      CRP (cpssDxChPortSerdesPowerStatusSet
           (d, dp[d][p], CPSS_PORT_DIRECTION_BOTH_E, 0x0F, GT_TRUE));

      CRP (cpssDxChPortEnableSet (d, dp[d][p], GT_TRUE));
      for (stg = 0; stg < 256; stg++)
        CRP (cpssDxChBrgStpStateSet (d, dp[d][p], stg, CPSS_STP_FRWRD_E));

      DEBUG ("*** setup device %d cascade trunk port %d\r\n", d, dp[d][p]);
    }

    CRP (cpssDxChTrunkCascadeTrunkPortsSet (d, SYSD_CSCD_TRUNK, &tp));
  }

  CRP (cpssDxChCscdDevMapTableSet
       (0, 1, 0, &cl, CPSS_DXCH_CSCD_TRUNK_LINK_HASH_IS_SRC_PORT_E));
  CRP (cpssDxChCscdDevMapTableSet
       (1, 0, 0, &cl, CPSS_DXCH_CSCD_TRUNK_LINK_HASH_IS_SRC_PORT_E));
}

void
sysd_vlan_add (vid_t vid)
{
  GT_U8 dp[2][2] = {
    {26, 27},
    {24, 25}
  };
  int d, p;

  for (d = 0; d < 2; d++) {
    for (p = 0; p < 2; p++) {
      CRP (cpssDxChBrgVlanMemberSet
           (d, vid, dp[d][p], GT_TRUE, GT_TRUE,
            CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E));
    }
  }

  DEBUG ("*** setup cascade trunk ports vlan %d\r\n", vid);
}

#endif /* VARIANT_* */

struct dev_info *dev_info = __dev_info;
