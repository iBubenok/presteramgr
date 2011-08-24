#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <presteramgr.h>
#include <debug.h>

#include <gtOs/gtOsInit.h>
#include <gtOs/gtOsGen.h>
#include <gtOs/gtOsExc.h>

#include <cpss/generic/init/cpssInit.h>

#include <cpss/generic/cscd/cpssGenCscd.h>

#include <cpss/generic/config/private/prvCpssConfigTypes.h>
#include <cpss/dxCh/dxChxGen/config/private/prvCpssDxChInfo.h>

#include <cpss/dxCh/dxChxGen/cpssHwInit/cpssDxChHwInit.h>
#include <cpss/dxCh/dxChxGen/config/cpssDxChCfgInit.h>

/* Port library. */
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortStat.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortBufMg.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortStat.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortTx.h>

/* Bridge library. */
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgStp.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgMc.h>

/* PHY library. */
#include <cpss/dxCh/dxChxGen/phy/cpssDxChPhySmi.h>

/* Netif library. */
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>

/* PCL library. */
#include <cpss/dxCh/dxChxGen/pcl/cpssDxChPcl.h>

/* Mirror library. */
#include <cpss/dxCh/dxChxGen/mirror/cpssDxChMirror.h>

/* Policer library. */
#include <cpss/dxCh/dxChxGen/policer/cpssDxChPolicer.h>

/* Trunk library. */
#include <cpss/dxCh/dxChxGen/trunk/cpssDxChTrunk.h>

/* OS binding function prototypes. */
#include <gtOs/gtOsMem.h>
#include <gtOs/gtOsStr.h>
#include <gtOs/gtOsSem.h>
#include <gtOs/gtOsIo.h>
#include <gtOs/gtOsInet.h>
#include <gtOs/gtOsTimer.h>
#include <gtOs/gtOsIntr.h>
#include <gtOs/gtOsRand.h>
#include <gtOs/gtOsTask.h>
#include <gtOs/gtOsStdLib.h>
#include <gtOs/gtOsMsgQ.h>

/* Ext driver function prototypes. */
#include <gtExtDrv/drivers/gtCacheMng.h>
#include <gtExtDrv/drivers/gtSmiHwCtrl.h>
#include <gtExtDrv/drivers/gtTwsiHwCtrl.h>
#include <gtExtDrv/drivers/gtDmaDrv.h>
#include <gtExtDrv/drivers/gtEthPortCtrl.h>
#include <gtExtDrv/drivers/gtHsuDrv.h>
#include <gtExtDrv/drivers/gtIntDrv.h>
#include <gtExtDrv/drivers/gtPciDrv.h>
#include <gtExtDrv/drivers/gtDragoniteDrv.h>

#define RX_DESC_NUM_DEF         200
#define TX_DESC_NUM_DEF         1000
#define AU_DESC_NUM_DEF         2048
#define RX_BUFF_SIZE_DEF        1536
#define RX_BUFF_ALIGN_DEF       1

#define RCC(rc, name) ({                                                \
  GT_STATUS __rc = CRP (rc);                                            \
  if (__rc != GT_OK)                                                    \
    return __rc;                                                        \
    })

extern GT_STATUS extDrvUartInit (void);


static GT_STATUS
init_pci (CPSS_DXCH_PP_PHASE1_INIT_INFO_STC *info)
{
  GT_U16 did = (CPSS_98DX2122_CNS >> 16) & 0xFFFF;
  GT_U16 vid = CPSS_98DX2122_CNS & 0xFFFF;
  GT_U32 ins = 0, bus_no = 0, dev_sel = 0, func_no = 0;
  GT_UINTPTR pci_base_addr, internal_pci_base;
  void *int_vec;
  GT_U32 int_mask;
  GT_STATUS rc;

  rc = extDrvPciFindDev (vid, did, ins, &bus_no, &dev_sel, &func_no);
  RCC (rc, extDrvPciFindDev);
  printf ("Device found: bus no %d, dev sel %d, func no %d\n",
          bus_no, dev_sel, func_no);

  rc = extDrvPciMap (bus_no, dev_sel, func_no, vid, did,
                     &pci_base_addr, &internal_pci_base);
  RCC (rc, extDrvPciMap);
  internal_pci_base &= 0xFFF00000;
  fprintf (stderr, "%08X %08X\n", pci_base_addr, internal_pci_base);

  rc = extDrvGetPciIntVec (GT_PCI_INT_B, &int_vec);
  RCC (rc, extDrvGetPciIntVec);
  fprintf (stderr, "intvec: %d\n", (GT_U32) int_vec);

  rc = extDrvGetIntMask (GT_PCI_INT_B, &int_mask);
  RCC (rc, extDrvGetIntMask);
  fprintf (stderr, "intmask: %08X\n", int_mask);

  info->busBaseAddr = pci_base_addr;
  info->internalPciBase = internal_pci_base;
  info->intVecNum = (GT_U32) int_vec;
  info->intMask = int_mask;

  return GT_OK;
}

static GT_STATUS
post_phase_1 (void)
{
  GT_STATUS rc;
  CPSS_DXCH_IMPLEMENT_WA_ENT wa [CPSS_DXCH_IMPLEMENT_WA_LAST_E];
  GT_U32 wa_info [CPSS_DXCH_IMPLEMENT_WA_LAST_E];

  /* wa [0] = CPSS_DXCH_IMPLEMENT_WA_SDMA_PKTS_FROM_CPU_STACK_E; */
  /* wa_info [0] = 0; */

  wa [0] = CPSS_DXCH_IMPLEMENT_WA_FDB_AU_FIFO_E;
  wa_info [0] = 0;

  rc = cpssDxChHwPpImplementWaInit (0, 1, wa, wa_info);
  RCC (rc, cpssDxChHwPpImplementWaInit);

  return GT_OK;
}

static GT_STATUS
phase2_init (void)
{
  CPSS_DXCH_PP_PHASE2_INIT_INFO_STC info;
  GT_U32 au_desc_size;
  GT_32 int_key;
  GT_STATUS rc;

  osMemSet (&info, 0, sizeof (info));

  info.newDevNum = 0;

  cpssDxChHwAuDescSizeGet (CPSS_98DX2122_CNS, &au_desc_size);
  info.auqCfg.auDescBlockSize = au_desc_size * AU_DESC_NUM_DEF;
  info.auqCfg.auDescBlock = osCacheDmaMalloc ((au_desc_size + 1) * AU_DESC_NUM_DEF);
  if (!info.auqCfg.auDescBlock) {
    fprintf (stderr, "failed to allocate AU desc memory\n");
    return GT_OUT_OF_CPU_MEM;
  }

  info.fuqUseSeparate = GT_FALSE;
  info.useSecondaryAuq = GT_FALSE;
  info.netifSdmaPortGroupId = 0;

  extDrvSetIntLockUnlock (INTR_MODE_LOCK, &int_key);
  rc = cpssDxChHwPpPhase2Init (0, &info);
  extDrvSetIntLockUnlock (INTR_MODE_UNLOCK, &int_key);
  RCC (rc, cpssDxChHwPpPhase2Init);

  return GT_OK;
}

static GT_STATUS
logical_init (void)
{
  CPSS_DXCH_PP_CONFIG_INIT_STC conf;
  GT_STATUS rc;

  osMemSet (&conf, 0, sizeof (conf));

  conf.maxNumOfVirtualRouters = 1;
  conf.maxNumOfIpNextHop = 100;
  conf.maxNumOfIpv4Prefixes = 3920;
  conf.maxNumOfMll = 500;
  conf.maxNumOfIpv4McEntries = 100;
  conf.maxNumOfIpv6Prefixes = 100;
  conf.maxNumOfTunnelEntries = 500;
  conf.maxNumOfIpv4TunnelTerms = 8;
  conf.routingMode = CPSS_DXCH_TCAM_ROUTER_BASED_E;

  rc = cpssDxChCfgPpLogicalInit (0, &conf);
  RCC (rc, cpssDxChCfgPpLogicalInit);

  return rc;
}

#define CHEETAH_CPU_PORT_PROFILE                   CPSS_PORT_RX_FC_PROFILE_1_E
#define CHEETAH_NET_GE_PORT_PROFILE                CPSS_PORT_RX_FC_PROFILE_2_E
#define CHEETAH_NET_10GE_PORT_PROFILE              CPSS_PORT_RX_FC_PROFILE_3_E
#define CHEETAH_CASCADING_PORT_PROFILE             CPSS_PORT_RX_FC_PROFILE_4_E

#define CHEETAH_GE_PORT_XON_DEFAULT                14 /* 28 Xon buffs per port   */
#define CHEETAH_GE_PORT_XOFF_DEFAULT               35 /* 70 Xoff buffs per port  */
#define CHEETAH_GE_PORT_RX_BUFF_LIMIT_DEFAULT      25 /* 100 buffers per port    */

#define CHEETAH_CPU_PORT_XON_DEFAULT               14 /* 28 Xon buffs per port   */
#define CHEETAH_CPU_PORT_XOFF_DEFAULT              35 /* 70 Xoff buffs per port  */
#define CHEETAH_CPU_PORT_RX_BUFF_LIMIT_DEFAULT     25 /* 100 buffers for CPU port */

#define CHEETAH_XG_PORT_XON_DEFAULT                25 /* 50 Xon buffs per port   */
#define CHEETAH_XG_PORT_XOFF_DEFAULT               85 /* 170 Xoff buffs per port */
#define CHEETAH_XG_PORT_RX_BUFF_LIMIT_DEFAULT      56 /* 224 buffers per port    */

static GT_STATUS
dxChPortBufMgInit (IN GT_U8 dev)
{
  GT_U8 port;
  GT_STATUS rc;
  CPSS_PORT_RX_FC_PROFILE_SET_ENT profile;
  GT_U32 buffLimit [4] [3] = { /* 4 profiles : values for Xon,Xoff,rxBuff */
    /* Profile 0 - Set CPU ports profile */
    {
      CHEETAH_CPU_PORT_XON_DEFAULT,
      CHEETAH_CPU_PORT_XOFF_DEFAULT,
      CHEETAH_CPU_PORT_RX_BUFF_LIMIT_DEFAULT
    },
    /* Profile 1 - Set Giga ports profile */
    {
      CHEETAH_GE_PORT_XON_DEFAULT,
      CHEETAH_GE_PORT_XOFF_DEFAULT,
      CHEETAH_GE_PORT_RX_BUFF_LIMIT_DEFAULT
    },
    /* Profile 2 - Set XG and Cascade ports profile */
    {
      CHEETAH_XG_PORT_XON_DEFAULT,
      CHEETAH_XG_PORT_XOFF_DEFAULT,
      CHEETAH_XG_PORT_RX_BUFF_LIMIT_DEFAULT
    },
    /* Profile 3 - Set XG and Cascade ports profile */
    {
      CHEETAH_XG_PORT_XON_DEFAULT,
      CHEETAH_XG_PORT_XOFF_DEFAULT,
      CHEETAH_XG_PORT_RX_BUFF_LIMIT_DEFAULT
    }
  };

  /* CPSS should config profile 0 and 1. */
  /* Set default settings for Flow Control Profiles: */
  for (profile = CPSS_PORT_RX_FC_PROFILE_1_E ; profile <= CPSS_PORT_RX_FC_PROFILE_4_E ; profile++) {
    RCC ((rc = cpssDxChPortXonLimitSet (dev, profile, buffLimit [profile] [0])),
         cpssDxChPortXonLimitSet);
    RCC ((rc = cpssDxChPortXoffLimitSet (dev, profile, buffLimit [profile] [1])),
         cpssDxChPortXoffLimitSet);
    RCC ((rc = cpssDxChPortRxBufLimitSet (dev, profile, buffLimit [profile] [2])),
         cpssDxChPortRxBufLimitSet);
  }

  /* set the buffer limit profile association for network ports */
  for (port = 0; port < PRV_CPSS_PP_MAC (dev)->numOfPorts; port++) {
    if (PRV_CPSS_PP_MAC (dev)->phyPortInfoArray [port].portType ==
        PRV_CPSS_PORT_NOT_EXISTS_E)
      continue;

    profile =
      (PRV_CPSS_PP_MAC (dev)->phyPortInfoArray [port].portType >= PRV_CPSS_PORT_XG_E)
      ? CHEETAH_NET_10GE_PORT_PROFILE
      : CHEETAH_NET_GE_PORT_PROFILE;

    RCC ((rc = cpssDxChPortRxFcProfileSet(dev, port, profile)),
         cpssDxChPortRxFcProfileSet);
  }

  /* set the buffer limit profile association for CPU port */
  profile = CHEETAH_CPU_PORT_PROFILE;
  RCC ((rc = cpssDxChPortRxFcProfileSet (dev, (GT_U8) CPSS_CPU_PORT_NUM_CNS, profile)),
       cpssDxChPortRxFcProfileSet);

  /* Enable HOL system mode for revision 3 in DxCh2, DxCh3, XCAT. */
  /* if (appDemoPpConfigList[dev].flowControlDisable) */
  /*   { */
  /*     rc = cpssDxChPortFcHolSysModeSet(dev, CPSS_DXCH_PORT_HOL_E); */
  /*   } */
  /*   else */
  /*   { */
  RCC ((rc = cpssDxChPortFcHolSysModeSet (dev, CPSS_DXCH_PORT_FC_E)),
       cpssDxChPortFcHolSysModeSet);

  return GT_OK;
}

static GT_STATUS
port_lib_init (void)
{
  GT_STATUS rc;

  RCC ((rc = cpssDxChPortStatInit (0)), cpssDxChPortStatInit);
  RCC ((rc = dxChPortBufMgInit (0)), dxChPortBufMgInit);
  RCC ((rc = cpssDxChPortTxInit (0)), cpssDxChPortTxInit);

  return GT_OK;
}

static GT_STATUS
phy_lib_init (void)
{
  GT_STATUS rc;

  RCC ((rc = cpssDxChPhyPortSmiInit (0)),
       cpssDxChPhyPortSmiInit);

  return GT_OK;
}

static GT_STATUS
dxChBrgFdbInit (IN GT_U8 dev)
{
  GT_STATUS rc;
  GT_U8 hw_dev;    /* HW device number */

  RCC ((rc = cpssDxChBrgFdbInit (dev)),
       cpssDxChBrgFdbInit);

  /* Set lookUp mode and lookup length. */
  RCC ((rc = cpssDxChBrgFdbHashModeSet (dev, CPSS_MAC_HASH_FUNC_XOR_E)),
       cpssDxChBrgFdbHashModeSet);

  RCC ((rc = cpssDxChBrgFdbMaxLookupLenSet (dev, 4)),
       cpssDxChBrgFdbMaxLookupLenSet);

  /******************************/
  /* do specific cheetah coding */
  /******************************/

  /* the trunk entries and the multicast entries are registered on device 31
     that is to support the "renumbering" feature , but the next configuration
     should not effect the behavior on other systems that not use a
     renumbering ..
  */
  /* age trunk entries on a device that registered from all device
     since we registered the trunk entries on device 31 (and auto learn set it
     on "own device" */
  /* Set Action Active Device Mask and Action Active Device. This is needed
     in order to enable aging only on own device.  */

  RCC ((rc = cpssDxChCfgHwDevNumGet (dev, &hw_dev)),
       cpssDxChCfgHwDevNumGet);

  RCC ((rc = cpssDxChBrgFdbActionActiveDevSet (dev, hw_dev, 0x1F)),
       cpssDxChBrgFdbActionActiveDevSet);

  RCC ((rc = cpssDxChBrgFdbAgeOutAllDevOnTrunkEnable (dev, GT_TRUE)),
       cpssDxChBrgFdbAgeOutAllDevOnTrunkEnable);

  RCC ((rc = cpssDxChBrgFdbAgeOutAllDevOnNonTrunkEnable (dev, GT_FALSE)),
       cpssDxChBrgFdbAgeOutAllDevOnNonTrunkEnable);

  return GT_OK;
}

static GT_STATUS
bridge_lib_init (void)
{
  GT_STATUS rc;
  GT_U32 stp_entry [CPSS_DXCH_STG_ENTRY_SIZE_CNS];

  /* Init VLAN */
  RCC ((rc = cpssDxChBrgVlanInit (0)),
       cpssDxChBrgVlanInit);

  /* STP */
  RCC ((rc = cpssDxChBrgStpInit (0)),
       cpssDxChBrgStpInit);

  RCC ((rc = dxChBrgFdbInit (0)),
       dxChBrgFdbInit);

  RCC ((rc = cpssDxChBrgMcInit (0)),
       cpssDxChBrgMcInit);

  /* set first entry in STP like default entry */
  osMemSet (stp_entry, 0, sizeof (stp_entry));
  RCC ((rc = cpssDxChBrgStpEntryWrite (0, 0, stp_entry)),
       cpssDxChBrgStpEntryWrite);

  return GT_OK;
}

static GT_STATUS
netif_lib_init (void)
{
  GT_STATUS rc;
  CPSS_DXCH_NETIF_MII_INIT_STC init;

  init.numOfTxDesc = 1000;
  init.txInternalBufBlockSize = 16000;
  init.txInternalBufBlockPtr = cpssOsCacheDmaMalloc (init.txInternalBufBlockSize);
  if (init.txInternalBufBlockPtr == NULL) {
    fprintf (stderr, "failed to allocate TX buffers\n");
    return GT_FAIL;
  }

  init.bufferPercentage[0] = 13;
  init.bufferPercentage[1] = 13;
  init.bufferPercentage[2] = 13;
  init.bufferPercentage[3] = 13;
  init.bufferPercentage[4] = 12;
  init.bufferPercentage[5] = 12;
  init.bufferPercentage[6] = 12;
  init.bufferPercentage[7] = 12;

  init.rxBufSize = 1536;
  init.headerOffset = 0;
  init.rxBufBlockSize = 307200;
  init.rxBufBlockPtr = cpssOsCacheDmaMalloc (init.rxBufBlockSize);
  if (init.rxBufBlockPtr == NULL) {
    fprintf (stderr, "failed to allocate RX buffers\n");
    return GT_FAIL;
  }

  RCC ((rc = cpssDxChNetIfMiiInit (0, &init)),
       cpssDxChNetIfMiiInit);

  return GT_OK;
}

static GT_STATUS
pcl_lib_init (void)
{
  GT_STATUS rc;

  RCC ((rc = cpssDxChPclInit (0)),
       cpssDxChPclInit);
  RCC ((rc = cpssDxChPclIngressPolicyEnable (0, GT_TRUE)),
       cpssDxChPclIngressPolicyEnable);

  return GT_OK;
}

static GT_STATUS
mirror_lib_init (void)
{
  GT_STATUS rc;
  GT_U8 hw_dev;
  CPSS_DXCH_MIRROR_ANALYZER_INTERFACE_STC interface;

  RCC ((rc = cpssDxChCfgHwDevNumGet (0, &hw_dev)),
       cpssDxChCfgHwDevNumGet);

  interface.interface.type = CPSS_INTERFACE_PORT_E;
  interface.interface.devPort.devNum = hw_dev;
  interface.interface.devPort.portNum = 0;

  RCC ((rc = cpssDxChMirrorAnalyzerInterfaceSet (0, 0, &interface)),
       cpssDxChMirrorAnalyzerInterfaceSet);
  RCC ((rc = cpssDxChMirrorAnalyzerInterfaceSet (0, 1, &interface)),
       cpssDxChMirrorAnalyzerInterfaceSet);

  return GT_OK;
}

static GT_STATUS
policer_lib_init (void)
{
  GT_STATUS rc;

  if (PRV_CPSS_DXCH_PP_MAC (0)->fineTuning.featureInfo.iplrSecondStageSupported
      != GT_TRUE) {
    fprintf (stderr, "policer_lib_init: doing nothing\n");
    return GT_OK;
  }

  RCC ((rc = cpssDxCh3PolicerMeteringEnableSet (0,
                                                CPSS_DXCH_POLICER_STAGE_INGRESS_1_E,
                                                GT_FALSE)),
       cpssDxCh3PolicerMeteringEnableSet);

  RCC ((rc = cpssDxChPolicerCountingModeSet (0,
                                             CPSS_DXCH_POLICER_STAGE_INGRESS_1_E,
                                             CPSS_DXCH_POLICER_COUNTING_DISABLE_E)),
       cpssDxChPolicerCountingModeSet);

  return GT_OK;
}

static GT_STATUS
trunk_lib_init (void)
{
  GT_STATUS rc;
  GT_U8 max = 127;

  RCC ((rc = cpssDxChTrunkInit (0, max, CPSS_DXCH_TRUNK_MEMBERS_MODE_NATIVE_E)),
       cpssDxChTrunkInit);

  return rc;
}

#if 0
static GT_STATUS
prvDxCh2Ch3IpLibInit (void)
{
  GT_STATUS                                       rc = GT_OK;
  CPSS_DXCH_IP_TCAM_LPM_MANGER_CAPCITY_CFG_STC    cpssLpmDbCapacity;
  CPSS_DXCH_IP_TCAM_LPM_MANGER_INDEX_RANGE_STC    cpssLpmDbRange;
  CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC                 ucRouteEntry;
  CPSS_DXCH_IP_MC_ROUTE_ENTRY_STC                 mcRouteEntry;
  CPSS_DXCH_IP_TCAM_SHADOW_TYPE_ENT               shadowType;
  CPSS_IP_PROTOCOL_STACK_ENT                      protocolStack;
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT          defUcLttEntry;
  CPSS_DXCH_IP_LTT_ENTRY_STC                      defMcLttEntry;
  CPSS_DXCH_IP_LPM_VR_CONFIG_STC                  vrConfigInfo;
  CPSS_DXCH_IP_TCAM_LPM_MANGER_CHEETAH2_VR_SUPPORT_CFG_STC pclTcamCfg;
  CPSS_DXCH_IP_TCAM_LPM_MANGER_CHEETAH2_VR_SUPPORT_CFG_STC routerTcamCfg;
  GT_BOOL                                         isCh2VrSupported;
  GT_U32                                          lpmDbId = 0;
  GT_U32                                          tcamColumns;
  static GT_BOOL lpmDbInitialized = GT_FALSE;     /* traces after LPM DB creation */

  /* init default UC and MC entries */
  cpssOsMemSet (&defUcLttEntry, 0, sizeof (CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT));
  cpssOsMemSet (&defMcLttEntry, 0, sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));

  defUcLttEntry.ipLttEntry.ipv6MCGroupScopeLevel    = CPSS_IPV6_PREFIX_SCOPE_GLOBAL_E;
  defUcLttEntry.ipLttEntry.numOfPaths               = 0;
  defUcLttEntry.ipLttEntry.routeEntryBaseIndex      = 0;
  defUcLttEntry.ipLttEntry.routeType                = CPSS_DXCH_IP_ECMP_ROUTE_ENTRY_GROUP_E;
  defUcLttEntry.ipLttEntry.sipSaCheckMismatchEnable = GT_FALSE;
  defUcLttEntry.ipLttEntry.ucRPFCheckEnable         = GT_FALSE;

  defMcLttEntry.ipv6MCGroupScopeLevel    = CPSS_IPV6_PREFIX_SCOPE_GLOBAL_E;
  defMcLttEntry.numOfPaths               = 0;
  defMcLttEntry.routeEntryBaseIndex      = 1;
  defMcLttEntry.routeType                = CPSS_DXCH_IP_ECMP_ROUTE_ENTRY_GROUP_E;
  defMcLttEntry.sipSaCheckMismatchEnable = GT_FALSE;
  defMcLttEntry.ucRPFCheckEnable         = GT_FALSE;

  cpssOsMemSet (&vrConfigInfo, 0, sizeof (CPSS_DXCH_IP_LPM_VR_CONFIG_STC));

  shadowType = CPSS_DXCH_IP_TCAM_XCAT_SHADOW_E;
  tcamColumns = 4;
  isCh2VrSupported = GT_FALSE;

  vrConfigInfo.supportIpv4Uc = GT_TRUE;
  cpssOsMemCpy (&vrConfigInfo.defIpv4UcNextHopInfo.ipLttEntry,
                &defUcLttEntry.ipLttEntry,
                sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));
  vrConfigInfo.supportIpv6Uc = GT_TRUE;
  cpssOsMemCpy (&vrConfigInfo.defIpv6UcNextHopInfo.ipLttEntry,
                &defUcLttEntry.ipLttEntry,
                sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));
  vrConfigInfo.supportIpv6Mc = GT_TRUE;
  vrConfigInfo.supportIpv4Mc = GT_TRUE;
  cpssOsMemCpy (&vrConfigInfo.defIpv4McRouteLttEntry,
                &defMcLttEntry,
                sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));
  cpssOsMemCpy (&vrConfigInfo.defIpv6McRouteLttEntry,
                &defMcLttEntry,
                sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));

  cpssOsMemSet (&ucRouteEntry, 0, sizeof (ucRouteEntry));
  ucRouteEntry.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
  ucRouteEntry.entry.regularEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
  rc = cpssDxChIpUcRouteEntriesWrite (0, 0, &ucRouteEntry, 1);
  if (rc != GT_OK) {
    if (rc == GT_OUT_OF_RANGE) {
      /* The device does not support any IP (not router device). */
      rc = GT_OK;
      fprintf (stderr "cpssDxChIpUcRouteEntriesWrite : device not supported\n");
    }
    return  rc;
  }

  cpssOsMemSet (&mcRouteEntry, 0, sizeof (mcRouteEntry));
  mcRouteEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
  mcRouteEntry.RPFFailCommand = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
  RCC ((rc = cpssDxChIpMcRouteEntriesWrite (0, 1, &mcRouteEntry)),
       cpssDxChIpMcRouteEntriesWrite);

  /********************************************************************/
  /* if lpm db is already created, all that is needed to do is to add */
  /* the device to the lpm db                                         */
  /********************************************************************/
  if (lpmDbInitialized == GT_TRUE) {
    rc = cpssDxChIpLpmDBDevListAdd (lpmDbId, &dev, 1);
    if (rc == GT_BAD_PARAM) {
      fprintf (stderr, "cpssDxChIpLpmDBDevListAdd : device not supported\n");
      rc = GT_OK;
    }
    return  rc;
  }

  /*****************/
  /* create LPM DB */
  /*****************/

  /* set parameters */
  cpssLpmDbCapacity.numOfIpv4Prefixes         = sysConfigParamsPtr->maxNumOfIpv4Prefixes;
  cpssLpmDbCapacity.numOfIpv6Prefixes         = sysConfigParamsPtr->maxNumOfIpv6Prefixes;
  cpssLpmDbCapacity.numOfIpv4McSourcePrefixes = sysConfigParamsPtr->maxNumOfIpv4McEntries;
  cpssLpmDbRange.firstIndex                   = sysConfigParamsPtr->lpmDbFirstTcamLine;
  cpssLpmDbRange.lastIndex                    = sysConfigParamsPtr->lpmDbLastTcamLine;

    if ((appDemoPpConfigList[dev].devFamily == CPSS_PP_FAMILY_CHEETAH2_E) && (cpssLpmDbRange.lastIndex > 1023))
    {
        cpssLpmDbRange.lastIndex = 1023;
    }

    rc = cpssDxChIpLpmDBCreate (lpmDbId, shadowType,
                                protocolStack, &cpssLpmDbRange,
                                sysConfigParamsPtr->lpmDbPartitionEnable,
                                &cpssLpmDbCapacity, NULL);
        CPSS_ENABLER_DBG_TRACE_RC_MAC("cpssDxChIpLpmDBCreate", rc);
    }
    if (rc != GT_OK)
    {
        return rc;
    }

    /* mark the lpm db as created */
    lpmDbInitialized = GT_TRUE;

    /*******************************/
    /* add active device to LPM DB */
    /*******************************/
    rc = cpssDxChIpLpmDBDevListAdd(lpmDbId, &dev, 1);
    if (rc != GT_OK)
    {
        if(rc == GT_BAD_PARAM)
        {
            /* the device not support the router tcam */
            osPrintf("cpssDxChIpLpmDBDevListAdd : device[%d] not supported \n",dev);
            rc = GT_OK;
        }

        return  rc;
    }

    /*************************/
    /* create virtual router */
    /*************************/
    if (GT_TRUE == isCh2VrSupported)
    {
        rc = cpssDxChIpLpmVirtualRouterAdd(lpmDbId,
                                           0,
                                           &vrConfigInfo);
        CPSS_ENABLER_DBG_TRACE_RC_MAC("cpssDxChIpLpmVirtualRouterAdd", rc);
        if (rc != GT_OK)
        {
            return  rc;
        }
        vrConfigInfo.defIpv4UcNextHopInfo.ipLttEntry.routeEntryBaseIndex = 1;
        vrConfigInfo.defIpv6UcNextHopInfo.ipLttEntry.routeEntryBaseIndex = 1;

        /*****************************************/
        /* This the Ch2 with Vr support case, so */
        /* create another virtual router in PCL  */
        /*****************************************/
        rc = cpssDxChIpLpmVirtualRouterAdd(lpmDbId,
                                           1,
                                           &vrConfigInfo);
    }
    else
    {
        rc = cpssDxChIpLpmVirtualRouterAdd(lpmDbId,
                                           0,
                                           &vrConfigInfo);
    }
    CPSS_ENABLER_DBG_TRACE_RC_MAC("cpssDxChIpLpmVirtualRouterAdd", rc);
    return rc;
}
#endif

static GT_STATUS
lib_init (void)
{
  GT_STATUS rc;

  fprintf (stderr, "doing port library init\n");
  RCC ((rc = port_lib_init ()), port_lib_init);
  fprintf (stderr, "port library init done\n");

  fprintf (stderr, "doing phy library init\n");
  RCC ((rc = phy_lib_init ()), phy_lib_init);
  fprintf (stderr, "phy library init done\n");

  fprintf (stderr, "doing bridge library init\n");
  RCC ((rc = bridge_lib_init ()), bridge_lib_init);
  fprintf (stderr, "bridge library init done\n");

  fprintf (stderr, "doing netif library init\n");
  RCC ((rc = netif_lib_init ()), netif_lib_init);
  fprintf (stderr, "netif library init done\n");

  fprintf (stderr, "doing mirror library init\n");
  RCC ((rc = mirror_lib_init ()), mirror_lib_init);
  fprintf (stderr, "mirror library init done\n");

  fprintf (stderr, "doing pcl library init\n");
  RCC ((rc = pcl_lib_init ()), pcl_lib_init);
  fprintf (stderr, "pcl library init done\n");

  fprintf (stderr, "doing policer library init\n");
  RCC ((rc = policer_lib_init ()), policer_lib_init);
  fprintf (stderr, "policer library init done\n");

  fprintf (stderr, "doing trunk library init\n");
  RCC ((rc = trunk_lib_init ()), trunk_lib_init);
  fprintf (stderr, "trunk library init done\n");

  return GT_OK;
}

static GT_STATUS
after_phase2 (void)
{
  GT_STATUS rc;
  GT_U8 portNum;

  RCC ((rc = cpssDxChCscdDsaSrcDevFilterSet (0, GT_FALSE)),
       cpssDxChCscdDsaSrcDevFilterSet);

  /* configure PHY SMI Auto Poll ports number */
  /* configure SMI0 to 16 ports and SMI1 to 8 ports */
  RCC ((rc = cpssDxChPhyAutoPollNumOfPortsSet (0,
                                               CPSS_DXCH_PHY_SMI_AUTO_POLL_NUM_OF_PORTS_16_E,
                                               CPSS_DXCH_PHY_SMI_AUTO_POLL_NUM_OF_PORTS_8_E)),
       cpssDxChPhyAutoPollNumOfPortsSet);

  /* port 0-23*/
  for (portNum = 0; portNum < 24; portNum++) {
    if (!PRV_CPSS_PHY_PORT_IS_EXIST_MAC (0, portNum))
      continue;

    RCC ((rc = cpssDxChPhyPortAddrSet (0, portNum, (GT_U8) (portNum % 16))),
         cpssDxChPhyPortAddrSet);
  }

  for (portNum = 24; portNum < 28; portNum++) {
    RCC ((rc = cpssDxChPhyPortSmiInterfaceSet (0, portNum, CPSS_PHY_SMI_INTERFACE_1_E)),
         cpssDxChPhyPortSmiInterfaceSet);

    RCC ((rc = cpssDxChPhyPortAddrSet (0, portNum, 0x10 + (portNum - 24) * 2)),
         cpssDxChPhyPortAddrSet);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x16, 0x6)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x14, 0x8205)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x16, 0x4)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x1A, 0xB002)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x1B, 0x7C03)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x00, 0x9140)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x16, 0x1)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x00, 0x9140)),
         cpssDxChPhyPortSmiRegisterWrite);

    RCC ((rc = cpssDxChPhyPortAddrSet (0, portNum, 0x11 + (portNum - 24) * 2)),
         cpssDxChPhyPortAddrSet);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x16, 0x6)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x14, 0x8207)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x16, 0x4)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x00, 0x9140)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x16, 0xFF)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x18, 0x2800)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x17, 0x2001)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x18, 0x1F70)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x17, 0x2004)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x16, 0x3)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x10, 0x1AA7)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x16, 0x0)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x1A, 0x9040)),
         cpssDxChPhyPortSmiRegisterWrite);
    RCC ((rc = cpssDxChPhyPortSmiRegisterWrite (0, portNum, 0x00, 0x9140)),
         cpssDxChPhyPortSmiRegisterWrite);

    RCC ((rc = cpssDxChPhyPortAddrSet (0, portNum, 0x10 + (portNum - 24) * 2)),
         cpssDxChPhyPortAddrSet);
  }

  return GT_OK;
}

static GT_STATUS
linux_ip_setup (GT_U8 dev)
{
  GT_STATUS rc;
#define CHECK(a) do {                                               \
    rc = (a);                                                       \
    printf ("*** " #a ": %04X\n", rc);                              \
    if (rc != GT_OK)                                                \
      return rc;                                                    \
  } while (0)

  /* CHECK (cpssDxChBrgGenIeeeReservedMcastTrapEnable (dev, GT_TRUE)); */
  /* CHECK (cpssDxChBrgGenIeeeReservedMcastProtCmdSet (0, 0, 0, CPSS_PACKET_CMD_TRAP_TO_CPU_E)); */
  /* CHECK (cpssDxChBrgGenPortIeeeReservedMcastProfileIndexSet (0, 13, 0)); */
  CHECK (cpssDxChBrgGenArpBcastToCpuCmdSet (dev, CPSS_PACKET_CMD_MIRROR_TO_CPU_E));
  CHECK (cpssDxChBrgGenArpTrapEnable (dev, 13, GT_TRUE));

#undef CHECK
  return rc;
}

static GT_STATUS
after_init (void)
{
  GT_STATUS rc;
  GT_U8 port;

  /* set ports 24-27 to SGMII mode */
  for (port = 24; port < 28; port++) {
    printf ("*** configure GE port %02d => SGMII mode\n", port);
    rc = port_set_sgmii_mode (0, port);
    if (rc != GT_OK)
      return rc;
  }

  for (port = 0; port < 28; ++port) {
    rc = cpssDxChPortEnableSet (0, port, GT_TRUE);
    if (rc != GT_OK)
      return rc;
  }
  RCC ((rc = cpssDxChCscdPortTypeSet (0, 63,  CPSS_CSCD_PORT_DSA_MODE_EXTEND_E)),
       cpssDxChCscdPortTypeSet);
  if (rc != GT_OK)
    return rc;
  rc = cpssDxChPortEnableSet (0, 63, GT_TRUE);
  if (rc != GT_OK)
    return rc;

  rc = cpssDxChCfgDevEnable (0, GT_TRUE);
  if (rc != GT_OK)
    return rc;

  vlan_init ();
  rc = linux_ip_setup (0);

  return rc;
}

static GT_STATUS
init_cpss (void)
{
  GT_STATUS rc;
  CPSS_DXCH_PP_PHASE1_INIT_INFO_STC ph1_info;
  CPSS_PP_DEVICE_TYPE dev_type;

  rc = extDrvEthRawSocketModeSet (GT_TRUE);
  if (rc != GT_OK)
    return rc;

  CRPR (extsvc_bind ());
  rc = cpssPpInit ();
  RCC (rc, cpssPpInit ());

  osFatalErrorInit (NULL);
  osMemInit (2048 * 1024, GT_TRUE);
  extDrvUartInit ();

  osMemSet (&ph1_info, 0, sizeof (ph1_info));

  ph1_info.devNum = 0;
  ph1_info.coreClock = CPSS_DXCH_AUTO_DETECT_CORE_CLOCK_CNS;
  ph1_info.mngInterfaceType = CPSS_CHANNEL_PEX_E;
  ph1_info.ppHAState = CPSS_SYS_HA_MODE_ACTIVE_E;
  ph1_info.serdesRefClock = CPSS_DXCH_PP_SERDES_REF_CLOCK_INTERNAL_125_E;
  ph1_info.initSerdesDefaults = GT_TRUE;
  ph1_info.isExternalCpuConnected = GT_FALSE;

  rc = init_pci (&ph1_info);
  RCC (rc, init_pci);

  fprintf (stderr, "doing phase1 config\n");
  rc = cpssDxChHwPpPhase1Init (&ph1_info, &dev_type);
  RCC (rc, cpssDxChHwPpPhase1Init);
  fprintf (stderr, "device type: %08X\n", dev_type);

  fprintf (stderr, "initializing workarounds\n");
  rc = post_phase_1 ();
  RCC (rc, post_phase_1);

  fprintf (stderr, "doing phase2 config\n");
  rc = phase2_init ();
  RCC (rc, phase2_init);
  fprintf (stderr, "phase2 config done\n");

  fprintf (stderr, "after phase2 config\n");
  rc = after_phase2 ();
  RCC (rc, after_phase2);
  fprintf (stderr, "after config done\n");

  fprintf (stderr, "doing logical init\n");
  rc = logical_init ();
  RCC (rc, logical_init);
  fprintf (stderr, "logical init done\n");

  fprintf (stderr, "doing library init\n");
  rc = lib_init ();
  RCC (rc, lib_init);
  fprintf (stderr, "library init done\n");

  fprintf (stderr, "doing after init\n");
  rc = after_init ();
  RCC (rc, after_init);
  fprintf (stderr, "after init done\n");

  return GT_OK;
}


void
cpss_start (void)
{
  if (osWrapperOpen (NULL) != GT_OK) {
    fprintf (stderr, "osWrapper initialization failure!\n");
    return;
  }

  fprintf (stderr, "\n\n");
  init_cpss ();

  fprintf (stderr, "init mgmt interface\n");
  mgmt_init ();

  fprintf (stderr, "start handling events\n");
  event_enter_loop ();
}
