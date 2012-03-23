#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <cpss/dxCh/dxChxGen/ipLpmEngine/cpssDxChIpLpmTypes.h>
#include <cpss/dxCh/dxChxGen/ipLpmEngine/cpssDxChIpLpm.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>

#include <route.h>
#include <debug.h>

static GT_STATUS
cpss_lib_init (void)
{
  GT_STATUS                                       rc = GT_OK;
  CPSS_DXCH_IP_TCAM_LPM_MANGER_CAPCITY_CFG_STC    cpssLpmDbCapacity;
  CPSS_DXCH_IP_TCAM_LPM_MANGER_INDEX_RANGE_STC    cpssLpmDbRange;
  CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC                 ucRouteEntry;
  CPSS_DXCH_IP_MC_ROUTE_ENTRY_STC                 mcRouteEntry;
  CPSS_DXCH_IP_TCAM_SHADOW_TYPE_ENT               shadowType;
  CPSS_IP_PROTOCOL_STACK_ENT                      protocolStack = CPSS_IP_PROTOCOL_IPV4V6_E;
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT          defUcLttEntry;
  CPSS_DXCH_IP_LTT_ENTRY_STC                      defMcLttEntry;
  CPSS_DXCH_IP_LPM_VR_CONFIG_STC                  vrConfigInfo;
  GT_BOOL                                         isCh2VrSupported;
  GT_U32                                          lpmDbId = 0;
  GT_U32                                          tcamColumns;
  static GT_BOOL lpmDbInitialized = GT_FALSE;     /* traces after LPM DB creation */
  GT_U8 devs[] = { 0 };

  /* init default UC and MC entries */
  memset (&defUcLttEntry, 0, sizeof (CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT));
  memset (&defMcLttEntry, 0, sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));

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

  memset (&vrConfigInfo, 0, sizeof (CPSS_DXCH_IP_LPM_VR_CONFIG_STC));

  shadowType = CPSS_DXCH_IP_TCAM_XCAT_SHADOW_E;
  tcamColumns = 4;
  isCh2VrSupported = GT_FALSE;

  vrConfigInfo.supportIpv4Uc = GT_TRUE;
  memcpy (&vrConfigInfo.defIpv4UcNextHopInfo.ipLttEntry,
          &defUcLttEntry.ipLttEntry,
          sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));
  vrConfigInfo.supportIpv6Uc = GT_TRUE;
  memcpy (&vrConfigInfo.defIpv6UcNextHopInfo.ipLttEntry,
          &defUcLttEntry.ipLttEntry,
          sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));
  vrConfigInfo.supportIpv6Mc = GT_TRUE;
  vrConfigInfo.supportIpv4Mc = GT_TRUE;
  memcpy (&vrConfigInfo.defIpv4McRouteLttEntry,
          &defMcLttEntry,
          sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));
  memcpy (&vrConfigInfo.defIpv6McRouteLttEntry,
          &defMcLttEntry,
          sizeof (CPSS_DXCH_IP_LTT_ENTRY_STC));

  memset (&ucRouteEntry, 0, sizeof (ucRouteEntry));
  ucRouteEntry.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
  ucRouteEntry.entry.regularEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
  rc = cpssDxChIpUcRouteEntriesWrite (0, 0, &ucRouteEntry, 1);
  if (rc != GT_OK) {
    if (rc == GT_OUT_OF_RANGE) {
      /* The device does not support any IP (not router device). */
      rc = GT_OK;
      DEBUG ("cpssDxChIpUcRouteEntriesWrite : device not supported\n");
    }
    return  rc;
  }

  memset (&mcRouteEntry, 0, sizeof (mcRouteEntry));
  mcRouteEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
  mcRouteEntry.RPFFailCommand = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
  CRPR (cpssDxChIpMcRouteEntriesWrite (0, 1, &mcRouteEntry));

  /********************************************************************/
  /* if lpm db is already created, all that is needed to do is to add */
  /* the device to the lpm db                                         */
  /********************************************************************/
  if (lpmDbInitialized == GT_TRUE) {
    rc = cpssDxChIpLpmDBDevListAdd (lpmDbId, devs, 1);
    if (rc == GT_BAD_PARAM) {
      DEBUG ("cpssDxChIpLpmDBDevListAdd : device not supported\n");
      rc = GT_OK;
    }
    return  rc;
  }

  /*****************/
  /* create LPM DB */
  /*****************/

  /* set parameters */
  cpssLpmDbCapacity.numOfIpv4Prefixes         = 3920;
  cpssLpmDbCapacity.numOfIpv6Prefixes         = 100;
  cpssLpmDbCapacity.numOfIpv4McSourcePrefixes = 100;
  cpssLpmDbRange.firstIndex                   = 100;
  cpssLpmDbRange.lastIndex                    = 1204;

#if 0
  if ((appDemoPpConfigList[dev].devFamily == CPSS_PP_FAMILY_CHEETAH2_E) && (cpssLpmDbRange.lastIndex > 1023))
    cpssLpmDbRange.lastIndex = 1023;
#endif /* 0 */

  CRPR (cpssDxChIpLpmDBCreate (lpmDbId, shadowType,
                               protocolStack, &cpssLpmDbRange,
                               GT_TRUE,
                               &cpssLpmDbCapacity, NULL));

  /* mark the lpm db as created */
  lpmDbInitialized = GT_TRUE;

  /*******************************/
  /* add active device to LPM DB */
  /*******************************/
  rc = CRP (cpssDxChIpLpmDBDevListAdd (lpmDbId, devs, 1));
  if (rc != GT_OK) {
    if(rc == GT_BAD_PARAM) {
      /* the device not support the router tcam */
      DEBUG("cpssDxChIpLpmDBDevListAdd : device not supported \n");
      rc = GT_OK;
    }
    return  rc;
  }

  /*************************/
  /* create virtual router */
  /*************************/
  if (GT_TRUE == isCh2VrSupported) {
    CRPR (cpssDxChIpLpmVirtualRouterAdd(lpmDbId,
                                        0,
                                        &vrConfigInfo));
    vrConfigInfo.defIpv4UcNextHopInfo.ipLttEntry.routeEntryBaseIndex = 1;
    vrConfigInfo.defIpv6UcNextHopInfo.ipLttEntry.routeEntryBaseIndex = 1;

    /*****************************************/
    /* This the Ch2 with Vr support case, so */
    /* create another virtual router in PCL  */
    /*****************************************/
    rc = CRP (cpssDxChIpLpmVirtualRouterAdd(lpmDbId,
                                            1,
                                            &vrConfigInfo));
  } else {
    rc = CRP (cpssDxChIpLpmVirtualRouterAdd(lpmDbId,
                                            0,
                                            &vrConfigInfo));
  }

  return rc;
}

enum status
route_cpss_lib_init (void)
{
  cpss_lib_init ();

  return ST_OK;
}
