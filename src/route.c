#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <cpss/dxCh/dxChxGen/ipLpmEngine/cpssDxChIpLpmTypes.h>
#include <cpss/dxCh/dxChxGen/ipLpmEngine/cpssDxChIpLpm.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIpCtrl.h>
#include <cpss/generic/cpssHwInit/cpssHwInit.h>

#include <net/if.h>

#include <route.h>
#include <ret.h>
#include <vlan.h>
#include <fib.h>
#include <debug.h>

#include <uthash.h>


void *arpc_sock;

#define HASH_FIND_PFX(head, findpfx, out)                       \
  HASH_FIND (hh, head, findpfx, sizeof (struct route_pfx), out)
#define HASH_ADD_PFX(head, pfxfield, add)                       \
  HASH_ADD (hh, head, pfxfield, sizeof (struct route_pfx), add)

struct gw_by_pfx {
  struct route_pfx pfx;
  struct gw gw;
  UT_hash_handle hh;
};
static struct gw_by_pfx *gw_by_pfx;

#define HASH_FIND_GW(head, findgw, out)                 \
  HASH_FIND (hh, head, findgw, sizeof (struct gw), out)
#define HASH_ADD_GW(head, gwfield, add)                 \
  HASH_ADD (hh, head, gwfield, sizeof (struct gw), add)

struct pfx_by_pfx {
  struct route_pfx pfx;
  UT_hash_handle hh;
};

struct pfxs_by_gw {
  struct gw gw;
  struct pfx_by_pfx *pfxs;
  UT_hash_handle hh;
};
static struct pfxs_by_gw *pfxs_by_gw;


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
  defUcLttEntry.ipLttEntry.routeEntryBaseIndex      = DEFAULT_UC_RE_IDX;
  defUcLttEntry.ipLttEntry.routeType                = CPSS_DXCH_IP_ECMP_ROUTE_ENTRY_GROUP_E;
  defUcLttEntry.ipLttEntry.sipSaCheckMismatchEnable = GT_FALSE;
  defUcLttEntry.ipLttEntry.ucRPFCheckEnable         = GT_FALSE;

  defMcLttEntry.ipv6MCGroupScopeLevel    = CPSS_IPV6_PREFIX_SCOPE_GLOBAL_E;
  defMcLttEntry.numOfPaths               = 0;
  defMcLttEntry.routeEntryBaseIndex      = DEFAULT_MC_RE_IDX;
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
  ucRouteEntry.entry.regularEntry.cmd = CPSS_PACKET_CMD_DROP_HARD_E;
  rc = cpssDxChIpUcRouteEntriesWrite (0, DEFAULT_UC_RE_IDX, &ucRouteEntry, 1);
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
  CRPR (cpssDxChIpMcRouteEntriesWrite (0, DEFAULT_MC_RE_IDX, &mcRouteEntry));

  CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
  memset (&rt, 0, sizeof (rt));
  rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
  rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
  CRP (cpssDxChIpUcRouteEntriesWrite (0, MGMT_IP_RE_IDX, &rt, 1));

  /* Set up the unknown dst trap entry. */
  rt.entry.regularEntry.cpuCodeIdx = CPSS_DXCH_IP_CPU_CODE_IDX_1_E;
  CRP (cpssDxChIpUcRouteEntriesWrite (0, TRAP_RE_IDX, &rt, 1));

  /* Set up the unknown dst drop entry. */
  rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_DROP_HARD_E;
  CRP (cpssDxChIpUcRouteEntriesWrite (0, DROP_RE_IDX, &rt, 1));

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

  CRPR (cpssDxChIpLpmDBCreate (lpmDbId, shadowType,
                               protocolStack, &cpssLpmDbRange,
                               GT_TRUE,
                               &cpssLpmDbCapacity, NULL));

  /* mark the lpm db as created */
  lpmDbInitialized = GT_TRUE;

  /*******************************/
  /* add active device to LPM DB */
  /*******************************/
  CRPR (cpssDxChIpLpmDBDevListAdd (lpmDbId, devs, 1));

  /*************************/
  /* create virtual router */
  /*************************/
  rc = CRP (cpssDxChIpLpmVirtualRouterAdd (lpmDbId, 0, &vrConfigInfo));

  return rc;
}

enum status
route_set_router_mac_addr (mac_addr_t addr)
{
  GT_ETHERADDR ra;
  GT_STATUS rc;

  memcpy (ra.arEther, addr, sizeof (addr));
  rc = CRP (cpssDxChIpRouterMacSaBaseSet (0, &ra));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}

enum status
route_test (void)
{
  GT_ETHERADDR ra = {
    .arEther = { 0, 0xa, 0xb, 0xc, 0xd, 0xe }
  };

  DEBUG ("set router MAC addr");
  CRP (cpssDxChIpRouterMacSaBaseSet (0, &ra));

  DEBUG ("enable routing");
  CRP (cpssDxChIpRoutingEnable (0, GT_TRUE));

  return ST_OK;
}

enum status
route_add (const struct route *rt)
{
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;

  memset (&re, 0, sizeof (re));
  if (rt->gw.u32Ip == 0) {
    /* Connected route. */
    fib_add (ntohl (rt->pfx.addr.u32Ip), rt->pfx.alen, rt->vid, 0);
    re.ipLttEntry.routeEntryBaseIndex = TRAP_RE_IDX;
  } else {
    /* Via gateway. */
    fib_add (ntohl (rt->pfx.addr.u32Ip), rt->pfx.alen, rt->vid, ntohl (rt->gw.u32Ip));
    re.ipLttEntry.routeEntryBaseIndex = TRAP_RE_IDX;
  }

  CRP (cpssDxChIpLpmIpv4UcPrefixAdd
       (0, 0, rt->pfx.addr, rt->pfx.alen, &re, GT_TRUE));

  return ST_OK;
}

void
route_update_table (const struct gw *gw, int idx)
{
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
  struct pfxs_by_gw *pbg;
  struct pfx_by_pfx *pbp, *tmp;

  HASH_FIND_GW (pfxs_by_gw, gw, pbg);
  if (!pbg)
    return;

  memset (&re, 0, sizeof (re));
  re.ipLttEntry.routeEntryBaseIndex = idx;
  HASH_ITER (hh, pbg->pfxs, pbp, tmp) {
    if (pbp->pfx.alen != 0) {
      DEBUG ("install prefix %d.%d.%d.%d/%d via %d",
             pbp->pfx.addr.arIP[0], pbp->pfx.addr.arIP[1],
             pbp->pfx.addr.arIP[2], pbp->pfx.addr.arIP[3],
             pbp->pfx.alen, idx);
      CRP (cpssDxChIpLpmIpv4UcPrefixAdd
           (0, 0, pbp->pfx.addr, pbp->pfx.alen, &re, GT_TRUE));
    }
  }
}

enum status
route_del (const struct route *rt)
{
  struct gw_by_pfx *gbp;
  struct pfxs_by_gw *pbg;
  struct pfx_by_pfx *pbp;

  if (rt->pfx.alen != 0)
    CRP (cpssDxChIpLpmIpv4UcPrefixDel (0, 0, rt->pfx.addr, rt->pfx.alen));

  HASH_FIND_PFX (gw_by_pfx, &rt->pfx, gbp);
  if (!gbp) {
    DEBUG ("prefix not found!");
    return ST_HEX;
  }

  HASH_FIND_GW (pfxs_by_gw, &gbp->gw, pbg);
  if (!pbg) {
    DEBUG ("gateway not found!");
    return ST_HEX;
  }

  HASH_FIND_PFX (pbg->pfxs, &rt->pfx, pbp);
  if (!pbp) {
    DEBUG ("prefix not found!");
    return ST_HEX;
  }
  DEBUG ("%d prefixes for gateway", HASH_COUNT (pbg->pfxs));
  HASH_DEL (pbg->pfxs, pbp);
  free (pbp);

  DEBUG ("%d prefixes for gateway", HASH_COUNT (pbg->pfxs));
  if (HASH_COUNT (pbg->pfxs) == 0) {
    HASH_DEL (pfxs_by_gw, pbg);
    free (pbg);
  }

  HASH_DEL (gw_by_pfx, gbp);
  ret_unref (&gbp->gw, rt->pfx.alen == 0);
  free (gbp);

  return ST_OK;
}

enum status
route_add_mgmt_ip (ip_addr_t addr)
{
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
  GT_IPADDR ga;
  GT_STATUS rc;

  memcpy (ga.arIP, addr, 4);
  memset (&re, 0, sizeof (re));
  re.ipLttEntry.routeEntryBaseIndex = MGMT_IP_RE_IDX;
  rc = CRP (cpssDxChIpLpmIpv4UcPrefixAdd (0, 0, ga, 32, &re, GT_TRUE));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}

enum status
route_del_mgmt_ip (ip_addr_t addr)
{
  GT_STATUS rc;
  GT_IPADDR ga;

  memcpy (ga.arIP, addr, 4);
  rc = CRP (cpssDxChIpLpmIpv4UcPrefixDel (0, 0, ga, 32));
  switch (rc) {
  case GT_OK:        return ST_OK;
  case GT_NOT_FOUND: return ST_DOES_NOT_EXIST;
  default:           return ST_HEX;
  }
}

enum status
route_cpss_lib_init (void)
{
  ret_init ();
  cpss_lib_init ();

  return ST_OK;
}


static void
route_prefix_set_drop (uint32_t ip, int len)
{
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
  GT_IPADDR addr;

  addr.u32Ip = htonl (ip);
  memset (&re, 0, sizeof (re));
  re.ipLttEntry.routeEntryBaseIndex = DROP_RE_IDX;
  CRP (cpssDxChIpLpmIpv4UcPrefixAdd (0, 0, addr, len, &re, GT_TRUE));
}

#define MIN_IPv4_PKT_LEN (12 + 2 + 20)

void
route_handle_udt (const uint8_t *data, int len)
{
  uint32_t daddr, rt;
  int alen;
  const struct fib_entry *e;

  if (len < MIN_IPv4_PKT_LEN) {
    DEBUG ("frame length %d too small\r\n", len);
    return;
  }

  if ((data[12] != 0x08) ||
      (data[13] != 0x00)) {
    DEBUG ("invalid ethertype 0x%02X%02X\r\n", data[12], data[13]);
    return;
  }

  daddr = ntohl (*((uint32_t *) (data + 30)));
  e = fib_route (daddr);
  if (!e) {
    DEBUG ("can't route for %d.%d.%d.%d\r\n",
           (daddr >> 24) & 0xFF, (daddr >> 16) & 0xFF,
           (daddr >> 8) & 0xFF, daddr & 0xFF);
    return;
  }

  rt = fib_entry_get_gw (e);
  if (rt)
    alen = fib_entry_get_len (e);
  else {
    rt = daddr;
    alen = 32;
  }
  route_prefix_set_drop (rt, alen);

  DEBUG ("got packet to %d.%d.%d.%d, gw %d.%d.%d.%d\r\n",
         (daddr >> 24) & 0xFF, (daddr >> 16) & 0xFF,
         (daddr >> 8) & 0xFF, daddr & 0xFF,
         (rt >> 24) & 0xFF, (rt >> 16) & 0xFF,
         (rt >> 8) & 0xFF, rt & 0xFF);
}
