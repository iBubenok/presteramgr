#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <cpss/dxCh/dxChxGen/cscd/cpssDxChCscd.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/dxCh/dxChxGen/trunk/cpssDxChTrunk.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIpCtrl.h>

#include <pthread.h>
#include <string.h>

#include <sysdeps.h>
#include <stack.h>
#include <dev.h>
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
    .dev_id     = CPSS_98DX2122_CNS,
    .int_num    = GT_PCI_INT_B,
    .n_ic_ports = 0,
    .ph1_info   = {
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

#elif defined (VARIANT_SM_12F)

static struct dev_info __dev_info[] = {
  {
    .dev_id     = CPSS_98DX2122_CNS,
    .int_num    = GT_PCI_INT_B,
    .n_ic_ports = 0,
    .ph1_info   = {
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

#elif defined (VARIANT_ARLAN_3424GE)

static unsigned xg_phys[] = {0x18, 0x19, 0x1A, 0x1B};

static struct dev_info __dev_info[] = {
  {
    .dev_id     = CPSS_98DX4122_CNS,
    .int_num    = GT_PCI_INT_B,
    .n_ic_ports = 0,
    .n_xg_phys  = 4,
    .xg_phys    = xg_phys
    .ph1_info   = {
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

#elif defined (VARIANT_ARLAN_3448PGE)

static int ic_ports_0[] = {26, 27};
static unsigned xg_phys_0[] = {0x18, 0x19};
static int ic_ports_1[] = {24, 25};
static unsigned xg_phys_1[] = {0x1A, 0x1B};

static struct dev_info __dev_info[] = {
  {
    .dev_id     = CPSS_98DX5248_CNS,
    .int_num    = GT_PCI_INT_A,
    .n_ic_ports = 2,
    .ic_ports   = ic_ports_0,
    .n_xg_phys  = 2,
    .xg_phys    = xg_phys_0,
    .ph1_info   = {
      .devNum                 = 0,
      .coreClock              = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS,
      .mngInterfaceType       = CPSS_CHANNEL_PEX_E,
      .ppHAState              = CPSS_SYS_HA_MODE_ACTIVE_E,
      .serdesRefClock         = CPSS_DXCH_PP_SERDES_REF_CLOCK_EXTERNAL_125_DIFF_E,
      .initSerdesDefaults     = GT_TRUE,
      .isExternalCpuConnected = GT_TRUE
    }
  },
  {
    .dev_id     = CPSS_98DX4122_CNS,
    .int_num    = GT_PCI_INT_B,
    .n_ic_ports = 2,
    .ic_ports   = ic_ports_1,
    .n_xg_phys  = 2,
    .xg_phys    = xg_phys_1,
    .ph1_info   = {
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

/* TODO: maybe the CPU code setup must be done for all variants. */
static void
sysd_setup_cpu_codes (void)
{
  int d;

  for_each_dev (d) {
    CPSS_DXCH_NET_CPU_CODE_TABLE_ENTRY_STC cce = {
      .tc = 7,
      .dp = CPSS_DP_GREEN_E,
      .truncate = GT_FALSE,
      .cpuRateLimitMode = CPSS_NET_CPU_CODE_RATE_LIMIT_AGGREGATE_E,
      .cpuCodeRateLimiterIndex = 0,
      .cpuCodeStatRateLimitIndex = 0,
      .designatedDevNumIndex = CPU_DEV
    };


    CRP (cpssDxChNetIfCpuCodeDesignatedDeviceTableSet
         (d, 1, phys_dev (CPU_DEV)));

    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_CONTROL_SRC_DST_MAC_TRAP_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_INTERVENTION_ARP_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_INTERVENTION_IGMP_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_IPV4_IPV6_LINK_LOCAL_MC_DIP_TRP_MRR_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_IEEE_RSRVD_MULTICAST_ADDR_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_IPV4_BROADCAST_PACKET_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_UDP_BC_MIRROR_TRAP0_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_ROUTED_PACKET_FORWARD_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_MAIL_FROM_NEIGHBOR_CPU_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_BRIDGED_PACKET_FORWARD_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_CPU_TO_CPU_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_ROUTE_ENTRY_TRAP_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_IPV4_UC_ROUTE1_TRAP_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_ARP_BC_TO_ME_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_CPU_TO_ALL_CPUS_E, &cce));
    CRP (cpssDxChNetIfCpuCodeTableSet
         (d, CPSS_NET_FIRST_USER_DEFINED_E, &cce));
  }
}

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
      CPSS_PORTS_BMP_PORT_SET_MAC (&tp, dp[d][p]);

      CRP (cpssDxChCscdPortTypeSet
           (d, dp[d][p], CPSS_CSCD_PORT_DSA_MODE_EXTEND_E));
      CRP (cpssDxChPortInterfaceModeSet
           (d, dp[d][p], CPSS_PORT_INTERFACE_MODE_XGMII_E));
      CRP (cpssDxChPortSpeedSet
           (d, dp[d][p], CPSS_PORT_SPEED_10000_E));
      CRP (cpssDxChPortSerdesPowerStatusSet
           (d, dp[d][p], CPSS_PORT_DIRECTION_BOTH_E, 0x0F, GT_TRUE));
      CRP (cpssDxChPortMruSet (d, dp[d][p], 12000));
      CRP (cpssDxChIpPortRoutingEnable
           (d, dp[d][p], CPSS_IP_UNICAST_E, CPSS_IP_PROTOCOL_IPV4_E,
            GT_FALSE));

      DEBUG ("*** setup device %d cascade trunk port %d\r\n", d, dp[d][p]);
    }

    CRP (cpssDxChTrunkCascadeTrunkPortsSet (d, SYSD_CSCD_TRUNK, &tp));
  }

  CRP (cpssDxChCscdDevMapTableSet
       (0, phys_dev (1), 0, &cl, CPSS_DXCH_CSCD_TRUNK_LINK_HASH_IS_SRC_PORT_E));
  CRP (cpssDxChCscdDevMapTableSet
       (1, phys_dev (0), 0, &cl, CPSS_DXCH_CSCD_TRUNK_LINK_HASH_IS_SRC_PORT_E));

  sysd_setup_cpu_codes ();
}

int
sysd_hw_dev_num (int ldev)
{
  switch (ldev) {
  case 0:
    return stack_id + 15;
  case 1:
    return stack_id;
  default:
    EMERG ("invalid logical device number %d\r\n", ldev);
    abort ();
  }
}

#endif /* VARIANT_* */

struct dev_info *dev_info = __dev_info;
