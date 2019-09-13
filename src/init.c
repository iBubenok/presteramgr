#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <sysdeps.h>

#include <presteramgr.h>
#include <env.h>
#include <debug.h>
#include <utils.h>
#include <diag.h>
#include <log.h>

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

/* Mirror library. */
#include <cpss/dxCh/dxChxGen/mirror/cpssDxChMirror.h>

/* Policer library. */
#include <cpss/dxCh/dxChxGen/policer/cpssDxChPolicer.h>

#include <cpss/dxCh/dxChxGen/cscd/cpssDxChCscd.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgGen.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>

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

#include <extsvc.h>
#include <control.h>
#include <vif.h>
#include <port.h>
#include <gif.h>
#include <vlan.h>
#include <qos.h>
#include <pdsa.h>
#include <mac.h>
#include <sec.h>
#include <wnct.h>
#include <ip.h>
#include <route.h>
#include <qt2025-phy.h>
#include <monitor.h>
#include <pcl.h>
#include <mgmt.h>
#include <dgasp.h>
#include <stack.h>
#include <dev.h>
#include <tipc.h>
#include <trunk.h>
#include <presteramgr.h>
#include <mll.h>
#include <ipsg.h>

int just_reset = 0;

#define RX_DESC_NUM_DEF         200
#define TX_DESC_NUM_DEF         1000
#define RX_BUFF_SIZE_DEF        1536
#define RX_BUFF_ALIGN_DEF       1

#define RCC(rc, name) ({                                                \
      GT_STATUS __rc = CRP (rc);                                        \
      if (__rc != GT_OK)                                                \
        return __rc;                                                    \
    })

extern GT_STATUS extDrvUartInit (void);

static enum status
pci_find_dev (struct dev_info *info)
{
  GT_U16 did = (info->dev_id >> 16) & 0xFFFF;
  GT_U16 vid = info->dev_id & 0xFFFF;
  GT_U32 ins = 0, bus_no = 0, dev_sel = 0, func_no = 0;
  GT_UINTPTR pci_base_addr, internal_pci_base;
  void *int_vec;
  GT_U32 int_mask;
  GT_STATUS rc;

  rc = CRP (extDrvPciFindDev (vid, did, ins, &bus_no, &dev_sel, &func_no));
  ON_GT_ERROR (rc) goto out;
  DEBUG ("Device found: id %X, bus no %d, dev sel %d, func no %d\r\n",
         info->dev_id, bus_no, dev_sel, func_no);

  rc = CRP (extDrvPciMap (bus_no, dev_sel, func_no, vid, did,
                          &pci_base_addr, &internal_pci_base));
  ON_GT_ERROR (rc) goto out;
  internal_pci_base &= 0xFFF00000;
  DEBUG ("%08X %08X\r\n", pci_base_addr, internal_pci_base);

  rc = CRP (extDrvGetPciIntVec (info->int_num, &int_vec));
  ON_GT_ERROR (rc) goto out;
  DEBUG ("intvec: %d\r\n", (GT_U32) int_vec);

  rc = extDrvGetIntMask (info->int_num, &int_mask);
  ON_GT_ERROR (rc) goto out;
  DEBUG ("intmask: %08X\r\n", int_mask);

  info->ph1_info.busBaseAddr = pci_base_addr;
  info->ph1_info.internalPciBase = internal_pci_base;
  info->ph1_info.intVecNum = (GT_U32) int_vec;
  info->ph1_info.intMask = int_mask;

  diag_pci_base_addr = (uint32_t) pci_base_addr;

  uint32_t r;
  diag_reg_read(0x0000004C, &r);
  info->chip_revision[0] = r;
  DEBUG("DEVINFO: 0x0000004C == 0x%08X\n", r);
  diag_reg_read(0x00000050, &r);
  info->chip_revision[1] = r;
  DEBUG("DEVINFO: 0x00000050 == 0x%08X\n", r);
  diag_reg_read(0x00000054, &r);
  info->chip_revision[2] = r;
  DEBUG("DEVINFO: 0x00000054 == 0x%08X\n", r);
  diag_reg_read(0x0C0002B0, &r);
  info->chip_revision[3] = r;
  DEBUG("DEVINFO: 0x0C0002B0 == 0x%08X\n", r);

 out:
  return (rc == GT_OK) ? ST_OK : ST_HEX;
}

static GT_STATUS
post_phase_1 (int d)
{
  /* GT_STATUS rc; */
  /* CPSS_DXCH_IMPLEMENT_WA_ENT wa [CPSS_DXCH_IMPLEMENT_WA_LAST_E]; */
  /* GT_U32 wa_info [CPSS_DXCH_IMPLEMENT_WA_LAST_E]; */

  /* wa [0] = CPSS_DXCH_IMPLEMENT_WA_SDMA_PKTS_FROM_CPU_STACK_E; */
  /* wa_info [0] = 0; */

  /* wa [0] = CPSS_DXCH_IMPLEMENT_WA_FDB_AU_FIFO_E; */
  /* wa_info [0] = 0; */

  /* rc = cpssDxChHwPpImplementWaInit (d, 1, wa, wa_info); */
  /* RCC (rc, cpssDxChHwPpImplementWaInit); */

  return GT_OK;
}

static GT_STATUS
phase2_init (int d)
{
  CPSS_DXCH_PP_PHASE2_INIT_INFO_STC info;
  GT_U32 au_desc_size;
  GT_32 int_key;
  GT_STATUS rc;
  int hw_dev_num = sysd_hw_dev_num (d);

  osMemSet (&info, 0, sizeof (info));

  info.newDevNum = d;

  cpssDxChHwAuDescSizeGet (dev_info[d].dev_id, &au_desc_size);
  info.auqCfg.auDescBlockSize = au_desc_size * FDB_MAX_ADDRS * 2;
  info.auqCfg.auDescBlock =
    osCacheDmaMalloc (au_desc_size * (FDB_MAX_ADDRS * 2 + 1));
  if (!info.auqCfg.auDescBlock) {
    ERR ("failed to allocate AU desc memory\n");
    return GT_OUT_OF_CPU_MEM;
  }

  info.useDoubleAuq = GT_TRUE;
  info.fuqUseSeparate = GT_FALSE;
  info.useSecondaryAuq = GT_FALSE;
  info.netifSdmaPortGroupId = 0;

  extDrvSetIntLockUnlock (INTR_MODE_LOCK, &int_key);
  rc = cpssDxChHwPpPhase2Init (d, &info);

  DEBUG ("*** set hw dev num %d => %d\r\n", d, hw_dev_num);
  CRP (cpssDxChCfgHwDevNumSet (d, hw_dev_num));
//  dev_set_map (d, hw_dev_num);

  extDrvSetIntLockUnlock (INTR_MODE_UNLOCK, &int_key);
  RCC (rc, cpssDxChHwPpPhase2Init);

  return GT_OK;
}

static GT_STATUS
logical_init (int d)
{
  CPSS_DXCH_PP_CONFIG_INIT_STC conf;
  GT_STATUS rc;

  INFO ("devFamily: %d (%d)\n",
        PRV_CPSS_PP_MAC(d)->devFamily,
        PRV_CPSS_PP_MAC(d)->devFamily == CPSS_PP_FAMILY_DXCH_XCAT2_E);

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

  rc = cpssDxChCfgPpLogicalInit (d, &conf);
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

  CRP (cpssDxChPortBuffersModeSet (dev, CPSS_DXCH_PORT_BUFFERS_MODE_SHARED_E));

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
  /* RCC ((rc = cpssDxChPortFcHolSysModeSet (dev, CPSS_DXCH_PORT_FC_E)), */
  /*      cpssDxChPortFcHolSysModeSet); */

  return GT_OK;
}

static void
port_setup_td (int d)
{
  CPSS_PORT_TX_Q_TAIL_DROP_PROF_TC_PARAMS tdp;
  int i;

#ifdef VARIANT_FE
  CRP (cpssDxChPortTxMcastPcktDescrLimitSet (d, 896 / 128));
#else //VARIANT_GE
  CRP (cpssDxChPortTxMcastPcktDescrLimitSet (d, 1664 / 128));
#endif
  CRP (cpssDxChPortTxSharingGlobalResourceEnableSet (d, GT_TRUE));
#ifdef VARIANT_FE
  CRP (cpssDxChPortTxSharedGlobalResourceLimitsSet (d, 1056, 1056));
#else //VARIANT_GE
  CRP (cpssDxChPortTxSharedGlobalResourceLimitsSet (d, 1848, 1848));
#endif
  CRP (cpssDxChPortTxDp1SharedEnableSet (d, GT_FALSE));
  CRP (cpssDxChPortTxSharedPolicySet
       (d, CPSS_DXCH_PORT_TX_SHARED_POLICY_CONSTRAINED_E));

  for (i = 0; i < 8; i++) {
    int j;

    for (j = CPSS_PORT_TX_DROP_PROFILE_1_E;
         j < CPSS_PORT_TX_DROP_PROFILE_5_E;
         j++)
      CRP (cpssDxChPortTxTcSharedProfileEnableSet
           (d, j, i, CPSS_PORT_TX_SHARED_DP_MODE_ALL_E));
  }

  memset (&tdp, 0, sizeof (tdp));

  /* Network ports. */
#ifdef VARIANT_FE
  CRP (cpssDxChPortTxTailDropProfileSet
       (d, CPSS_PORT_TX_DROP_PROFILE_2_E, GT_FALSE, 264, 44));
  tdp.dp0MaxBuffNum = 12;
  tdp.dp0MaxDescrNum = 12;
  tdp.dp1MaxBuffNum = 7;
  tdp.dp1MaxDescrNum = 7;
#else //VARIANT_GE
  CRP (cpssDxChPortTxTailDropProfileSet
       (d, CPSS_PORT_TX_DROP_PROFILE_2_E, GT_FALSE, 528, 88));
  tdp.dp0MaxBuffNum = 18;
  tdp.dp0MaxDescrNum = 18;
  tdp.dp1MaxBuffNum = 12;
  tdp.dp1MaxDescrNum = 12;
#endif
  for (i = 0; i < 8; i++)
    CRP (cpssDxChPortTx4TcTailDropProfileSet
         (d, CPSS_PORT_TX_DROP_PROFILE_2_E, i, &tdp));

  /* Cascade ports. */
#ifdef VARIANT_FE
  CRP (cpssDxChPortTxTailDropProfileSet
       (d, CPSS_PORT_TX_DROP_PROFILE_3_E, GT_FALSE, 1056, 176));
  tdp.dp0MaxBuffNum = 36;
  tdp.dp0MaxDescrNum = 24;
  tdp.dp1MaxBuffNum = 36;
  tdp.dp1MaxDescrNum = 24;
#else //VARIANT_GE
  CRP (cpssDxChPortTxTailDropProfileSet
       (d, CPSS_PORT_TX_DROP_PROFILE_3_E, GT_FALSE, 1848, 308));
  tdp.dp0MaxBuffNum = 40;
  tdp.dp0MaxDescrNum = 28;
  tdp.dp1MaxBuffNum = 40;
  tdp.dp1MaxDescrNum = 28;
#endif
  for (i = 0; i < 8; i++)
    CRP (cpssDxChPortTx4TcTailDropProfileSet
         (d, CPSS_PORT_TX_DROP_PROFILE_3_E, i, &tdp));

  /* CPU port. */
  CRP (cpssDxChPortTxTailDropProfileSet
       (d, CPSS_PORT_TX_DROP_PROFILE_1_E, GT_FALSE, 264, 44));
  tdp.dp0MaxBuffNum = 8;
  tdp.dp0MaxDescrNum = 8;
  tdp.dp1MaxBuffNum = 8;
  tdp.dp1MaxDescrNum = 8;
  for (i = 0; i < 7; i++)
    CRP (cpssDxChPortTx4TcTailDropProfileSet
         (d, CPSS_PORT_TX_DROP_PROFILE_1_E, i, &tdp));
  tdp.dp0MaxBuffNum = 40;
  tdp.dp0MaxDescrNum = 40;
  tdp.dp1MaxBuffNum = 40;
  tdp.dp1MaxDescrNum = 40;
  CRP (cpssDxChPortTx4TcTailDropProfileSet
       (d, CPSS_PORT_TX_DROP_PROFILE_1_E, 7, &tdp));

  CRP (cpssDxChPortTxTailDropUcEnableSet (d, GT_TRUE));
  CRP (cpssDxChPortTxBufferTailDropEnableSet (d, GT_TRUE));
  CRP (cpssDxChPortTxRandomTailDropEnableSet (d, GT_TRUE));
}

static GT_STATUS
port_lib_init (int d)
{
  GT_STATUS rc;

  if (d == 0) {
    vif_init ();
    port_init ();
    vif_post_port_init ();
    gif_init ();
  }

  RCC ((rc = cpssDxChPortStatInit (d)), cpssDxChPortStatInit);
  RCC ((rc = dxChPortBufMgInit (d)), dxChPortBufMgInit);
  RCC ((rc = cpssDxChPortTxInit (d)), cpssDxChPortTxInit);
  port_setup_td (d);

  return GT_OK;
}

static GT_STATUS
phy_lib_init (int d)
{
  GT_STATUS rc;

  RCC ((rc = cpssDxChPhyPortSmiInit (d)),
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

  RCC ((rc = cpssDxChBrgFdbMacVlanLookupModeSet (dev, CPSS_IVL_E)),
        cpssDxChBrgFdbMacVlanLookupModeSet);

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
bridge_lib_init (int d)
{
  GT_STATUS rc;
  GT_U32 stp_entry [CPSS_DXCH_STG_ENTRY_SIZE_CNS];

  /* Init VLAN */
  RCC ((rc = cpssDxChBrgVlanInit (d)),
       cpssDxChBrgVlanInit);

  /* STP */
  RCC ((rc = cpssDxChBrgStpInit (d)),
       cpssDxChBrgStpInit);

  RCC ((rc = dxChBrgFdbInit (d)),
       dxChBrgFdbInit);

  RCC ((rc = cpssDxChBrgMcInit (d)),
       cpssDxChBrgMcInit);

  /* set first entry in STP like default entry */
  osMemSet (stp_entry, 0, sizeof (stp_entry));
  RCC ((rc = cpssDxChBrgStpEntryWrite (d, 0, stp_entry)),
       cpssDxChBrgStpEntryWrite);

  return GT_OK;
}

static GT_STATUS __attribute__ ((unused))
netif_lib_init (int d)
{
  GT_STATUS rc;
  CPSS_DXCH_NETIF_MII_INIT_STC init;

  init.numOfTxDesc = 1000;
  init.txInternalBufBlockSize = 16000;
  init.txInternalBufBlockPtr = cpssOsCacheDmaMalloc (init.txInternalBufBlockSize);
  if (init.txInternalBufBlockPtr == NULL) {
    ALERT ("failed to allocate TX buffers\n");
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
    ALERT ("failed to allocate RX buffers\n");
    return GT_FAIL;
  }

  RCC ((rc = cpssDxChNetIfMiiInit (d, &init)),
       cpssDxChNetIfMiiInit);

  return GT_OK;
}

static GT_STATUS
policer_lib_init (int d)
{
  GT_STATUS rc;

  if (PRV_CPSS_DXCH_PP_MAC (d)->fineTuning.featureInfo.iplrSecondStageSupported
      != GT_TRUE) {
    DEBUG ("policer_lib_init: doing nothing\n");
    return GT_OK;
  }

  RCC ((rc = cpssDxCh3PolicerMeteringEnableSet (d,
                                                CPSS_DXCH_POLICER_STAGE_INGRESS_1_E,
                                                GT_FALSE)),
       cpssDxCh3PolicerMeteringEnableSet);

  RCC ((rc = cpssDxChPolicerCountingModeSet (d,
                                             CPSS_DXCH_POLICER_STAGE_INGRESS_1_E,
                                             CPSS_DXCH_POLICER_COUNTING_DISABLE_E)),
       cpssDxChPolicerCountingModeSet);

  return GT_OK;
}

static GT_STATUS
lib_init (int d)
{
  GT_STATUS rc;

  DEBUG ("doing port library init\n");
  RCC ((rc = port_lib_init (d)), port_lib_init);
  DEBUG ("port library init done\n");

  DEBUG ("doing phy library init\n");
  RCC ((rc = phy_lib_init (d)), phy_lib_init);
  DEBUG ("phy library init done\n");

  DEBUG ("doing bridge library init\n");
  RCC ((rc = bridge_lib_init (d)), bridge_lib_init);
  DEBUG ("bridge library init done\n");

  if (d == CPU_DEV) {
    DEBUG ("doing netif library init\n");
    RCC ((rc = netif_lib_init (d)), netif_lib_init);
    DEBUG ("netif library init done\n");
  }

  DEBUG ("doing monitor init\n");
  mon_cpss_lib_init (d);
  DEBUG ("monitor init done\n");

  DEBUG ("doing policer library init\n");
  RCC ((rc = policer_lib_init (d)), policer_lib_init);
  DEBUG ("policer library init done\n");

  return GT_OK;
}

static GT_STATUS
after_phase2 (int d)
{
  CRP (cpssDxChCscdDsaSrcDevFilterSet
       (d, stack_active () ? GT_TRUE : GT_FALSE));

  return GT_OK;
}

static void
rate_limit_init (int d)
{
  CPSS_DXCH_BRG_GEN_RATE_LIMIT_STC cfg = {
    .dropMode    = CPSS_DROP_MODE_HARD_E,
    .rMode       = CPSS_RATE_LIMIT_BYTE_BASED_E,
    .win10Mbps   = 10000,
    .win100Mbps  = 10000,
    .win1000Mbps = 10000,
    .win10Gbps   = 1000
  };

  CRP (cpssDxChBrgGenRateLimitGlobalCfgSet (d, &cfg));
}

static GT_STATUS
after_init (int d)
{
  return cpssDxChCfgDevEnable (d, GT_TRUE);
}

static void
do_reset (void)
{
#ifdef VARIANT_ARLAN_3050PGE
  return; /* TODO  3050 only */
#endif
  CPSS_PP_DEVICE_TYPE dev_type;
  int i;

  DEBUG ("*** reset requested\r\n");

#if defined (VARIANT_ARLAN_3448GE)
  for (i = NDEVS-1; i >= 0; i--) {
#else
  for_each_dev (i) {
#endif
    pci_find_dev (&dev_info[i]);

    DEBUG ("doing phase1 config\n");
    CRP (cpssDxChHwPpPhase1Init (&dev_info[i].ph1_info, &dev_type));
    DEBUG ("device type: %08X\n", dev_type);

    DEBUG ("dev %d: doing soft reset", i);

    /* TODO: investigate why the CPU device does not forward
       traffic after register reset and restore "register reset
       skip" flag. */
    CRP (cpssDxChHwPpSoftResetSkipParamSet
         (i, CPSS_HW_PP_RESET_SKIP_TYPE_REGISTER_E, GT_TRUE));
    CRP (cpssDxChHwPpSoftResetSkipParamSet
         (i, CPSS_HW_PP_RESET_SKIP_TYPE_TABLE_E, GT_FALSE));
    CRP (cpssDxChHwPpSoftResetSkipParamSet
         (i, CPSS_HW_PP_RESET_SKIP_TYPE_EEPROM_E, GT_TRUE));
    CRP (cpssDxChHwPpSoftResetSkipParamSet
         (i, CPSS_HW_PP_RESET_SKIP_TYPE_PEX_E, GT_TRUE));

    CRP (cpssDxChHwPpSoftResetTrigger (i));

    sleep (1);
  }

  DEBUG ("*** reset done\r\n");
}

static GT_STATUS
init_cpss (void)
{
  GT_STATUS rc;
  CPSS_PP_DEVICE_TYPE dev_type;
  int i;

  rc = extDrvEthRawSocketModeSet (GT_TRUE);
  if (rc != GT_OK)
    return rc;

  CRPR (extsvc_bind ());
  rc = cpssPpInit ();
  RCC (rc, cpssPpInit ());

  osFatalErrorInit (NULL);
  osMemInit (2048 * 1024, GT_TRUE);
  extDrvUartInit ();

  if (just_reset) {
    do_reset ();
    return GT_OK;
  }

  for_each_dev(i) {
    int hw_dev_num = sysd_hw_dev_num (i);
    dev_set_map(i, hw_dev_num);
  }

  stack_init ();

  for_each_dev (i) {
    pci_find_dev (&dev_info[i]);

    DEBUG ("doing phase1 config\n");
    rc = cpssDxChHwPpPhase1Init (&dev_info[i].ph1_info, &dev_type);
    RCC (rc, cpssDxChHwPpPhase1Init);
    DEBUG ("device type: %08X\n", dev_type);

    DEBUG ("initializing workarounds\n");
    rc = post_phase_1 (i);
    RCC (rc, post_phase_1);

    DEBUG ("doing phase2 config\n");
    rc = phase2_init (i);
    RCC (rc, phase2_init);
    DEBUG ("phase2 config done\n");

    DEBUG ("after phase2 config\n");
    rc = after_phase2 (i);
    RCC (rc, after_phase2);
    DEBUG ("after config done\n");

    DEBUG ("doing logical init\n");
    rc = logical_init (i);
    RCC (rc, logical_init);
    DEBUG ("logical init done\n");

    DEBUG ("doing library init\n");
    rc = lib_init (i);
    RCC (rc, lib_init);
    DEBUG ("library init done\n");

    if (!i) {
      DEBUG ("doing pcl library pre-init\n");
      rc = pcl_cpss_lib_pre_init ();
      RCC (rc, pcl_cpss_lib_pre_init);
      DEBUG ("pcl library pre-init done\n");
    }

    DEBUG ("doing pcl library init\n");
    rc = pcl_cpss_lib_init (i);
    RCC (rc, pcl_cpss_lib_init);
    DEBUG ("pcl library init done\n");
  }

  trunk_init ();
  sysd_setup_ic ();

  DEBUG ("doing IP library init\n");
  ip_cpss_lib_init ();
  DEBUG ("IP library init done\n");

#if defined (VARIANT_GE)
  qt2025_phy_load_fw ();
#endif /* VARIANT_GE */
  port_disable_all ();
  mac_start ();
  vlan_init ();
  wnct_start ();
  qos_start ();
  for_each_dev (i)
    rate_limit_init (i);
  stack_start ();
  port_start ();
  dgasp_init ();
  pdsa_init ();
  mll_init ();
  ip_start ();

  for_each_dev (i) {
    DEBUG ("doing after init\n");
    rc = after_init (i);
    RCC (rc, after_init);
    DEBUG ("after init done\n");
  }

  for_each_dev (i) {
    CPSS_DXCH_CFG_DEV_INFO_STC devinfo;
    CRP (cpssDxChCfgDevInfoGet(i, &devinfo));
    DEBUG("\nDEVINFO: devtype: %08x, revision: %hhd, devFamily: %d, maxPortNum: %d, numOfVirtPorts: %d, exPortsBMP: %08x:%08x\n",
        devinfo.genDevInfo.devType,
        devinfo.genDevInfo.revision,
        devinfo.genDevInfo.devFamily,
        devinfo.genDevInfo.maxPortNum,
        devinfo.genDevInfo.numOfVirtPorts,
        devinfo.genDevInfo.existingPorts.ports[0],
        devinfo.genDevInfo.existingPorts.ports[1]
        );
  }

  return GT_OK;
}

void
cpss_start (void)
{
  INFO ("init environment");
  env_init ();

  if (osWrapperOpen (NULL) != GT_OK) {
    ALERT ("osWrapper initialization failure!\n");
    return;
  }

  if (just_reset) {
    init_cpss ();
    exit (EXIT_SUCCESS);
  }

  event_start_notify_thread();

  control_pre_mac_init();

  init_cpss ();

  event_init ();

  INFO ("init security breach subsystem\n");
  sec_init ();

  INFO ("init control interface\n");
  control_init ();

  ipsg_init ();

  INFO ("init mgmt interface\n");
  mgmt_init ();

  INFO ("start tipc interface\n");
  tipc_start ();

  INFO ("start control interface\n");
  control_start ();

  ipsg_start ();

  INFO ("start security breach subsystem\n");
  sec_start ();

  INFO ("start routing subsystem");
  route_start ();

  INFO ("start handling events\n");
  event_enter_loop ();
}
