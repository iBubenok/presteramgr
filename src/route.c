#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <cpss/dxCh/dxChxGen/ipLpmEngine/cpssDxChIpLpmTypes.h>
#include <cpss/dxCh/dxChxGen/ipLpmEngine/cpssDxChIpLpm.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIpCtrl.h>
#include <cpss/generic/cpssHwInit/cpssHwInit.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgGen.h>

#include <net/if.h>

#include <route.h>
#include <ret.h>
#include <vlan.h>
#include <fib.h>
#include <fib_ipv6.h>
#include <arpc.h>
#include <sysdeps.h>
#include <mcg.h>
#include <mac.h>
#include <stack.h>
#include <debug.h>
#include <utils.h>
#include <ret_group.h>

#include <uthash.h>
#include <utlist.h>


#define HASH_FIND_PFX(head, findpfx, out)                       \
  HASH_FIND (hh, head, findpfx, sizeof (struct route_pfx), out)
#define HASH_ADD_PFX(head, pfxfield, add)                       \
  HASH_ADD (hh, head, pfxfield, sizeof (struct route_pfx), add)

#define HASH_FIND_GW(head, findgw, out)                 \
  HASH_FIND (hh, head, findgw, sizeof (struct gw), out)
#define HASH_ADD_GW(head, gwfield, add)                 \
  HASH_ADD (hh, head, gwfield, sizeof (struct gw), add)

struct pfx_ipv6_by_pfx {
  struct route_pfx pfx;
  GT_IPV6ADDR udaddr;
  UT_hash_handle hh;
};


struct pfxs_ipv6_by_gw {
  struct gw_v6 gw;
  struct pfx_ipv6_by_pfx *pfxs;
  UT_hash_handle hh;
};
static struct pfxs_ipv6_by_gw *pfxs_ipv6_by_gw;

uint8_t route_mac_lsb;

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
  GT_U8 devs[] = { 0 };
  int d;

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

  cpssLpmDbCapacity.numOfIpv4Prefixes         = 3920;
  cpssLpmDbCapacity.numOfIpv6Prefixes         = 100;
  cpssLpmDbCapacity.numOfIpv4McSourcePrefixes = 100;
  cpssLpmDbRange.firstIndex                   = 100;
  cpssLpmDbRange.lastIndex                    = 1204;

  CRPR (cpssDxChIpLpmDBCreate (lpmDbId, shadowType,
                               protocolStack, &cpssLpmDbRange,
                               GT_TRUE,
                               &cpssLpmDbCapacity, NULL));

  for_each_dev (d) {
    memset (&ucRouteEntry, 0, sizeof (ucRouteEntry));
    ucRouteEntry.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    ucRouteEntry.entry.regularEntry.cmd = CPSS_PACKET_CMD_DROP_HARD_E;
    CRP (cpssDxChIpUcRouteEntriesWrite (d, DEFAULT_UC_RE_IDX, &ucRouteEntry, 1));

    memset (&mcRouteEntry, 0, sizeof (mcRouteEntry));
    // mcRouteEntry.cmd = CPSS_PACKET_CMD_DROP_HARD_E;
    // mcRouteEntry.RPFFailCommand = CPSS_PACKET_CMD_DROP_HARD_E;
    mcRouteEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    mcRouteEntry.RPFFailCommand = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    mcRouteEntry.cpuCodeIdx = CPSS_DXCH_IP_CPU_CODE_IDX_0_E;
    CRP (cpssDxChIpMcRouteEntriesWrite (d, DEFAULT_MC_RE_IDX, &mcRouteEntry));

    CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;

    /* Set up the management IP addr route entry. */
    memset (&rt, 0, sizeof (rt));
    rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    rt.entry.regularEntry.appSpecificCpuCodeEnable = GT_TRUE;
    rt.entry.regularEntry.trapMirrorArpBcEnable = GT_TRUE;
    CRP (cpssDxChIpUcRouteEntriesWrite (d, MGMT_IP_RE_IDX, &rt, 1));

    /* Set up the unknown dst trap entry. */
    memset (&rt, 0, sizeof (rt));
    rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    rt.entry.regularEntry.appSpecificCpuCodeEnable = GT_TRUE;
    rt.entry.regularEntry.cpuCodeIdx = CPSS_DXCH_IP_CPU_CODE_IDX_1_E;
    CRP (cpssDxChIpUcRouteEntriesWrite (d, TRAP_RE_IDX, &rt, 1));

    /* Set up the unknown dst drop entry. */
    memset (&rt, 0, sizeof (rt));
    rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_DROP_HARD_E;
    CRP (cpssDxChIpUcRouteEntriesWrite (d, DROP_RE_IDX, &rt, 1));

    devs[0] = d;
    CRP (cpssDxChIpLpmDBDevListAdd (lpmDbId, devs, 1));

    CRP (cpssDxChIpEcmpUcRpfCheckEnableSet(d, GT_TRUE));

  }

  rc = CRP (cpssDxChIpLpmVirtualRouterAdd (lpmDbId, 0, &vrConfigInfo));

  return rc;
}

enum status
route_set_router_mac_addr (mac_addr_t addr)
{
  GT_ETHERADDR ra;
  int d;

  route_mac_lsb = addr[5];
//  memcpy (ra.arEther, addr, sizeof (addr));  XXX to check other same applications
  memcpy (ra.arEther, addr, 6);
  for_each_dev (d) {
    CRP (cpssDxChIpRouterMacSaBaseSet (d, &ra));
    vlan_set_mac_lsb();
  }


  return ST_OK;
}

enum status
route_set_solicited_cmd (solicited_cmd_t cmd) {
  CPSS_PACKET_CMD_ENT cpssCmd = cmd;

  int d;
  for_each_dev (d){
    CRP (cpssDxChBrgGenIpV6SolicitedCmdSet(d, cpssCmd));
  }

  return ST_OK;
}

enum status
route_start (void)
{
  int d;

  arpc_start ();

  DEBUG ("enable routing");
  for_each_dev (d)
    CRP (cpssDxChIpRoutingEnable (d, GT_TRUE));

  return ST_OK;
}


static void
route_ipv6_register (GT_IPV6ADDR addr, int alen, GT_IPV6ADDR gwaddr, vid_t vid, GT_IPV6ADDR udaddr)
{
  // GT_IPV6ADDR ip;
  struct gw_v6 gw;
  struct route_pfx pfx;
  struct pfxs_ipv6_by_gw *pbg;
  struct pfx_ipv6_by_pfx *pbp;
  memset(&gw, 0, sizeof(struct gw_v6));
  memset(&pfx, 0, sizeof(struct route_pfx));

  // DEBUG ("register ipv6 pfx "IPv6_FMT"/%d gw "IPv6_FMT"\r\n",
  //        IPv6_ARG(addr.arIP),
  //        alen,
  //        IPv6_ARG(gwaddr.arIP));

  // ip = gwaddr;
  route_ipv6_fill_gw (&gw, &gwaddr, vid);
  HASH_FIND_GW (pfxs_ipv6_by_gw, &gw, pbg);
  if (!pbg) {
    pbg = calloc (1, sizeof (*pbg));
    pbg->gw = gw;
    HASH_ADD_GW (pfxs_ipv6_by_gw, gw, pbg);
  }

  pfx.addrv6 = addr;
  pfx.alen = alen;
  HASH_FIND_PFX (pbg->pfxs, &pfx, pbp);
  if (!pbp) {
    pbp = calloc (1, sizeof (*pbp));
    pbp->pfx = pfx;
    pbp->udaddr = udaddr;
    HASH_ADD_PFX (pbg->pfxs, pfx, pbp);
  }
}


static void
route_ipv6_unregister (GT_IPV6ADDR addr, int alen, GT_IPV6ADDR gwaddr, vid_t vid)
{
  struct gw_v6 gw;
  struct route_pfx pfx;
  struct pfxs_ipv6_by_gw *pbg;
  struct pfx_ipv6_by_pfx *pbp;
  memset(&gw, 0, sizeof(struct gw_v6));
  memset(&pfx, 0, sizeof(struct route_pfx));

  DEBUG ("register ipv6 pfx "IPv6_FMT"/%d gw "IPv6_FMT"\r\n",
        IPv6_ARG(addr.arIP),
        alen,
        IPv6_ARG(gwaddr.arIP));

  route_ipv6_fill_gw (&gw, &gwaddr, vid);
  HASH_FIND_GW (pfxs_ipv6_by_gw, &gw, pbg);
  if (!pbg)
    return;

  pfx.addrv6 = addr;
  pfx.alen = alen;
  HASH_FIND_PFX (pbg->pfxs, &pfx, pbp);
  if (!pbp)
    return;

  HASH_DEL (pbg->pfxs, pbp);
  free (pbp);

  ret_ipv6_unref (&gw, alen == 0);

  if (!pbg->pfxs) {
    HASH_DEL (pfxs_ipv6_by_gw, pbg);
    free (pbg);
  }
}

enum status
route_add (const struct route *rt)
{


  DEBUG("route_add %hhu \n", rt->gw_count);

  fib_add(
    ntohl (rt->pfx.addr.u32Ip),
    rt->pfx.alen,
    rt->gw_count,
    rt->gw);

  DEBUG ("add route to %hhu.%hhu.%hhu.%hhu/%u via count %hhu\r\n",
         rt->pfx.addr.arIP[0], rt->pfx.addr.arIP[1],
         rt->pfx.addr.arIP[2], rt->pfx.addr.arIP[3],
         rt->pfx.alen,
         rt->gw_count);

  if (rt->pfx.alen == 0) {
    /* Default route. */
    CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
    int d;

    memset (&rt, 0, sizeof (rt));
    rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    rt.entry.regularEntry.cpuCodeIdx = CPSS_DXCH_IP_CPU_CODE_IDX_1_E;
    for_each_dev (d)
      CRP (cpssDxChIpUcRouteEntriesWrite (d, DEFAULT_UC_RE_IDX, &rt, 1));
  } else {
    CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;

    memset (&re, 0, sizeof (re));
    re.ipLttEntry.routeEntryBaseIndex = TRAP_RE_IDX;
    CRP (cpssDxChIpLpmIpv4UcPrefixAdd
         (0, 0, rt->pfx.addr, rt->pfx.alen, &re, GT_TRUE));
  }

  return ST_OK;
}

enum status
route_add_change (const struct route *rt)
{


  DEBUG("route_add_change %hhu \n", rt->gw_count);

  fib_add(
    ntohl (rt->pfx.addr.u32Ip),
    rt->pfx.alen,
    rt->gw_count,
    rt->gw);

  DEBUG ("add route to %hhu.%hhu.%hhu.%hhu/%u via count %hhu\r\n",
         rt->pfx.addr.arIP[0], rt->pfx.addr.arIP[1],
         rt->pfx.addr.arIP[2], rt->pfx.addr.arIP[3],
         rt->pfx.alen,
         rt->gw_count);

  if (rt->pfx.alen == 0) {
    /* Default route. */
    CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
    int d;

    memset (&rt, 0, sizeof (rt));
    rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    rt.entry.regularEntry.cpuCodeIdx = CPSS_DXCH_IP_CPU_CODE_IDX_1_E;
    for_each_dev (d)
      CRP (cpssDxChIpUcRouteEntriesWrite (d, DEFAULT_UC_RE_IDX, &rt, 1));
  } else {
    CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;

    memset (&re, 0, sizeof (re));
    re.ipLttEntry.routeEntryBaseIndex = DROP_RE_IDX;
    CRP (cpssDxChIpLpmIpv4UcPrefixAdd
         (0, 0, rt->pfx.addr, rt->pfx.alen, &re, GT_TRUE));
  }

  return ST_OK;
}

enum status
route_add_v6 (const struct route *rt)
{
// DEBUG(">>>>route_add_v6(pfx.addr== %x, pfx.alen== %d, vid== %d, gw== %x)\n",
//     ntohl (rt->pfx.addr.u32Ip), rt->pfx.alen, rt->vid, ntohl (rt->gw.u32Ip));

  fib_ipv6_add (
    rt->pfx.addrv6,
    rt->pfx.alen,
    rt->vid,
    rt->gw_v6);

  DEBUG ("add route v6 to " IPv6_FMT "/%d via "IPv6_FMT"\r\n",
         IPv6_ARG(rt->pfx.addrv6.arIP),
         rt->pfx.alen,
         IPv6_ARG(rt->gw_v6.arIP));

  if (rt->pfx.alen == 0) {
    /* Default route. */
    CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
    int d;

    memset (&rt, 0, sizeof (rt));
    rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    rt.entry.regularEntry.cpuCodeIdx = CPSS_DXCH_IP_CPU_CODE_IDX_1_E;
    for_each_dev (d)
      CRP (cpssDxChIpUcRouteEntriesWrite (d, DEFAULT_UC_RE_IDX, &rt, 1));
  } else {
    CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;

    memset (&re, 0, sizeof (re));
    re.ipLttEntry.routeEntryBaseIndex = TRAP_RE_IDX;
    CRP (cpssDxChIpLpmIpv6UcPrefixAdd
         (0, 0, rt->pfx.addrv6, rt->pfx.alen, &re, GT_TRUE, GT_FALSE));
  }

  return ST_OK;
}

void
route_ipv6_update_table (const struct gw_v6 *gw, int idx)
{
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
  struct pfxs_ipv6_by_gw *pbg;
  struct pfx_ipv6_by_pfx *pbp, *tmp;

  HASH_FIND_GW (pfxs_ipv6_by_gw, gw, pbg);
  if (!pbg)
    return;

  memset (&re, 0, sizeof (re));
  re.ipLttEntry.routeEntryBaseIndex = idx;
  HASH_ITER (hh, pbg->pfxs, pbp, tmp) {
    if (pbp->pfx.alen != 0) {
      DEBUG ("install prefix "IPv6_FMT"/%d via %d\r\n",
             IPv6_ARG(pbp->pfx.addrv6.arIP),
             pbp->pfx.alen, idx);
      CRP (cpssDxChIpLpmIpv6UcPrefixAdd
           (0, 0, pbp->pfx.addrv6, pbp->pfx.alen, &re, GT_TRUE, GT_FALSE));
    }
  }
}

void
route_del_fib_entry (struct fib_entry *e, bool_t is_children)
{
  GT_IPADDR pfx;
  uint32_t addr_pfx = fib_entry_get_pfx (e);
  int len = fib_entry_get_len (e);
  int group_id = fib_entry_get_group_id(e);

  ret_group_del(group_id, addr_pfx, len, is_children);


  pfx.u32Ip = htonl (addr_pfx);

  if (fib_entry_get_len (e) != 0) {
    CRP (cpssDxChIpLpmIpv4UcPrefixDel (0, 0, pfx, fib_entry_get_len (e)));
  }
  if (fib_entry_get_len (e) == 0) {
    CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
    memset (&re, 0, sizeof (re));
    re.ipLttEntry.routeEntryBaseIndex = DEFAULT_UC_RE_IDX;
    CRP (cpssDxChIpLpmIpv4UcPrefixAdd (0, 0, pfx, fib_entry_get_len (e), &re, GT_TRUE));
  }

  DEBUG ("done\r\n");
}

void
route_del_fib_ipv6_entry (struct fib_entry_ipv6 *e)
{
  GT_IPV6ADDR pfx, gwip;

  pfx = fib_entry_ipv6_get_pfx (e);
  gwip = fib_entry_ipv6_get_gw (e);

  DEBUG ("delete route v6 prefix "IPv6_FMT"/%d via "IPv6_FMT"\r\n",
         IPv6_ARG(pfx.arIP),
         fib_entry_ipv6_get_len (e),
         IPv6_ARG(gwip.arIP));


  if (fib_entry_ipv6_get_len (e) != 0)
    CRP (cpssDxChIpLpmIpv6UcPrefixDel (0, 0, pfx, fib_entry_ipv6_get_len (e)));

  route_ipv6_unregister (fib_entry_ipv6_get_pfx (e), fib_entry_ipv6_get_len (e),
                fib_entry_ipv6_get_gw (e), fib_entry_ipv6_get_vid (e));

  DEBUG ("done\r\n");
}

enum status
route_del (const struct route *rt)
{
  DEBUG("route_del " IPv4_FMT "/%d \n", IPv4_ARG(((uint8_t *)&rt->pfx.addr)), rt->pfx.alen);
// DEBUG(">>>>route_del(pfx.addr== %x, pfx.alen== %d, vid== %d, gw== %x)\n",
//     ntohl (rt->pfx.addr.u32Ip), rt->pfx.alen, rt->vid, ntohl (rt->gw.u32Ip));

  if (!fib_del (ntohl (rt->pfx.addr.u32Ip), rt->pfx.alen))
    DEBUG ("prefix %d.%d.%d.%d/%d not found\r\n",
         rt->pfx.addr.arIP[0], rt->pfx.addr.arIP[1],
         rt->pfx.addr.arIP[2], rt->pfx.addr.arIP[3],
         rt->pfx.alen);

  return ST_OK;
}

 enum status
route_del_v6 (const struct route *rt)
{
DEBUG ("del route v6 to " IPv6_FMT "/%d via "IPv6_FMT"\r\n ",
        IPv6_ARG(rt->pfx.addrv6.arIP),
        rt->pfx.alen,
        IPv6_ARG(rt->gw_v6.arIP));    

  if (!fib_ipv6_del (rt->pfx.addrv6, rt->pfx.alen))
    DEBUG ("prefix "IPv6_FMT"/%d not found\r\n",
         IPv6_ARG(rt->pfx.addrv6.arIP),
         rt->pfx.alen);

  return ST_OK;
}

enum status
route_change(const struct route *rt) {
  struct fib_entry *fib = NULL;
  int group_id = 0;
  fib = fib_get(ntohl(rt->pfx.addr.u32Ip), rt->pfx.alen);
  if (fib) {
    group_id = fib_entry_get_group_id(fib);
  }
  route_del(rt);
  if (group_id) {
    route_add_change(rt);
    route_handle_udaddr(ntohl(rt->pfx.addr.u32Ip));

  } else {
    route_add(rt);
  }

  return ST_OK;
}

enum status
route_add_mgmt_ip (ip_addr_t addr)
{
DEBUG(">>>>route_add_mgmt_ip (" IPv4_FMT ")\n", IPv4_ARG(addr));

  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
  GT_IPADDR ga;
  GT_STATUS rc;

  memcpy (ga.arIP, addr, 4);
  memset (&re, 0, sizeof (re));
  re.ipLttEntry.routeEntryBaseIndex = MGMT_IP_RE_IDX;
  rc = CRP (cpssDxChIpLpmIpv4UcPrefixAdd (0, 0, ga, 32, &re, GT_TRUE));
  switch (rc) {
  case GT_OK:
    return ST_OK;
  default:
    return ST_HEX;
  }
}

enum status
route_del_mgmt_ip (ip_addr_t addr)
{
DEBUG(">>>>route_del_mgmt_ip (" IPv4_FMT ")\n", IPv4_ARG(addr));
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
route_add_mgmt_ipv6 (ip_addr_v6_t addr)
{
DEBUG(">>>>route_add_mgmt_ip (" IPv6_FMT ")\n", IPv6_ARG(addr));

  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
  GT_IPV6ADDR ga;
  GT_STATUS rc;

  memcpy (ga.arIP, addr, 16);
  memset (&re, 0, sizeof (re));
  re.ipLttEntry.routeEntryBaseIndex = MGMT_IP_RE_IDX;

  rc = cpssDxChIpLpmIpv6UcPrefixAdd (0, 0, ga, 128, &re, GT_TRUE, GT_TRUE);

  switch (rc) {
  case GT_OK:
    return ST_OK;
  default:
    return ST_HEX;
  }
}

enum status
route_del_mgmt_ipv6 (ip_addr_v6_t addr)
{
DEBUG(">>>>route_del_mgmt_ip (" IPv6_FMT ")", IPv6_ARG(addr));
  GT_STATUS rc;
  GT_IPV6ADDR ga;

  memcpy (ga.arIP, addr, 16);
  rc = CRP (cpssDxChIpLpmIpv6UcPrefixDel (0, 0, ga, 128));
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
  route_mutex_init();

  return ST_OK;
}


static void
__attribute__ ((unused))
route_prefix_set_drop (uint32_t ip, int len)
{
  if (len) {
    CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
    GT_IPADDR addr;

    addr.u32Ip = htonl (ip);
    memset (&re, 0, sizeof (re));
    re.ipLttEntry.routeEntryBaseIndex = DROP_RE_IDX;
    CRP (cpssDxChIpLpmIpv4UcPrefixAdd (0, 0, addr, len, &re, GT_TRUE));
  } else {
    /* Default route. */
    CPSS_DXCH_IP_UC_ROUTE_ENTRY_STC rt;
    int d;

    memset (&rt, 0, sizeof (rt));
    rt.type = CPSS_DXCH_IP_UC_ROUTE_ENTRY_E;
    rt.entry.regularEntry.cmd = CPSS_PACKET_CMD_DROP_HARD_E;
    for_each_dev (d)
      CRP (cpssDxChIpUcRouteEntriesWrite (d, DEFAULT_UC_RE_IDX, &rt, 1));
  }
}

static void
route_request_mac_addr (struct list_uint32 *gwip, struct list_vid *vid, uint32_t ip, int alen, struct list_gw *ret_key, struct fib_entry *fib)
{
  // struct gw gw;
  // GT_IPADDR gwaddr;
  // int ix;
  struct gw gw_arr[8];
  struct gw *ret_key_arr[8];
  struct list_uint32 *gwip_el = NULL;
  int gwip_len;
  struct list_vid *vid_el = NULL;
  int vid_len;
  struct list_gw *ret_key_el = NULL;
  int ret_key_len;
  int i = 0;
  uint8_t *a;
  DL_FOREACH(gwip, gwip_el) {
    a = (uint8_t *)&gwip_el->val;
    gw_arr[i].addr.u32Ip = htonl (gwip_el->val);
    i++;
  }
  gwip_len = i;

  i = 0;
  DL_FOREACH(vid, vid_el) {
    gw_arr[i].vid = vid_el->val;
    i++;
  }
  vid_len = i;

  i = 0;
  DL_FOREACH(ret_key, ret_key_el){
    ret_key_arr[i] = &ret_key_el->val;
    i++;
  }
  ret_key_len = i;

  int group_id = ret_group_add(gwip_len, gw_arr, ip, alen);

  fib_entry_set_group_id (fib, group_id);
}

static void
route_ipv6_request_mac_addr (GT_IPV6ADDR gwip, vid_t vid, GT_IPV6ADDR ip, int alen, struct gw_v6 *ret_key)
{
  struct gw_v6 gw;
  GT_IPV6ADDR gwaddr;
  int ix;

  gwaddr = gwip;
  route_ipv6_fill_gw (&gw, &gwaddr, vid);
  ix = ret_ipv6_add (&gw, alen == 0, ret_key);
  DEBUG ("route entry index %d\r\n", ix);
  if ((ix >= 0) && (alen != 0)) {
    CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
    GT_IPV6ADDR addr;

    memset (&re, 0, sizeof (re));
    re.ipLttEntry.routeEntryBaseIndex = ix;
    addr = ip;
    CRP (cpssDxChIpLpmIpv6UcPrefixAdd (0, 0, addr, alen, &re, GT_TRUE, GT_FALSE));
  }
}

#define MIN_IPv4_PKT_LEN (12 + 2 + 20)

void
route_handle_udaddr (uint32_t daddr) {
  // DEBUG(">>>>route_handle_udaddr ("IPv4_FMT")\n", IPv4_ARG(((uint8_t *)&daddr)));
  // uint32_t rt;
  struct list_uint32 *rt = NULL;
  struct list_vid *vid = NULL;
  struct list_gw *ret_key = NULL;
  int alen = 32;
  struct fib_entry *e;
  e = fib_route (daddr);
  if (!e) {
    DEBUG ("can't route for %d.%d.%d.%d\r\n",
           (daddr >> 24) & 0xFF, (daddr >> 16) & 0xFF,
           (daddr >> 8) & 0xFF, daddr & 0xFF);
    return;
  }

  if (fib_entry_get_group_id(e)) return;

  rt = fib_entry_get_gw (e);

  vid = fib_entry_get_vid (e);

  ret_key = fib_entry_get_retkey_ptr(e);
  if (rt){
    alen = fib_entry_get_len (e);
  }
  else {
    // rt = daddr;
    // alen = 32;
  }

  route_request_mac_addr (rt, vid, fib_entry_get_pfx (e), alen, ret_key, e);

}

void
route_handle_ipv6_udaddr (GT_IPV6ADDR daddr) {
DEBUG(">>>>route_handle_ipv6_udaddr ("IPv6_FMT")\n", IPv6_ARG(daddr.arIP));
  GT_IPV6ADDR rt;
  int alen;
  struct fib_entry_ipv6 *e;

  e = fib_ipv6_route (daddr);
  if (!e) {
        DEBUG ("can't route ipv6 for "IPv6_FMT"\r\n",
           IPv6_ARG(daddr.arIP));
    return;
  }
  GT_IPV6ADDR ip0;
  memset(&ip0, 0, sizeof(ip0));

  rt = fib_entry_ipv6_get_gw (e);
  if (memcmp(&rt, &ip0, 16))
    alen = fib_entry_ipv6_get_len (e);
  else {
    rt = daddr;
    alen = 128;
  }
//  route_prefix_set_drop (fib_entry_get_pfx (e), alen);
  route_ipv6_register (fib_entry_ipv6_get_pfx (e), alen, rt, fib_entry_ipv6_get_vid (e), daddr);
  route_ipv6_request_mac_addr (rt, fib_entry_ipv6_get_vid (e), fib_entry_ipv6_get_pfx (e), alen, fib_entry_ipv6_get_retkey_ptr(e));

  DEBUG ("got packet to "IPv6_FMT", gw "IPv6_FMT"\r\n",
         IPv6_ARG(daddr.arIP),
         IPv6_ARG(rt.arIP));
}

void
route_handle_udt (const uint8_t *data, int len)
{
  uint32_t daddr;

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

  if (master_id == stack_id) {
    mac_op_udt(daddr);
  }

  route_mutex_lock();
  route_handle_udaddr (daddr);
  route_mutex_unlock();
}

void
route_handle_ipv6_udt (const uint8_t *data, int len)
{
  GT_IPV6ADDR daddr;

  if (len < MIN_IPv4_PKT_LEN) {
    DEBUG ("frame length %d too small\r\n", len);
    return;
  }

  if ((data[12] != 0x86) ||
      (data[13] != 0xDD)) {
    DEBUG ("invalid ethertype 0x%02X%02X\r\n", data[12], data[13]);
    return;
  }

  memcpy(&daddr, data + 38, sizeof(daddr));

  if (master_id == stack_id) {
    mac_op_udt_ipv6(daddr);
  }

  route_handle_ipv6_udaddr (daddr);
}


enum status
route_mc_add (vid_t vid, const uint8_t *dst, const uint8_t *src, mcg_t via,
              vid_t src_vid)
{
  CPSS_DXCH_IP_LTT_ENTRY_STC le;
  GT_STATUS rc;
  GT_IPADDR s, d;
  int idx, splen, res;

  memcpy (&d.arIP, dst, sizeof (d.arIP));
  memcpy (&s.arIP, src, sizeof (s.arIP));
  if (s.u32Ip == 0)
    splen = 0;
  else
    splen = 32;

  DEBUG ("Adding route for vlan %d, vidx %d from "
         "group %d.%d.%d.%d of vlan %d\n",
         vid, via,
         d.arIP [0], d.arIP [1], d.arIP [2], d.arIP [3],
         src_vid);

  if (via == 0xFFFF)
    idx = DROP_MC_RE_IDX;
  else if (mcg_valid (via)) {
    DEBUG ("Looking for idx...\n");
    idx = mcre_find (dst, src, src_vid);
    if (idx == -1) { // idx does not exist
      DEBUG ("Idx does not exist. Creating mcre.\n");

      idx = mcre_create (dst, src, via, vid, src_vid);
      if (idx == -1)
        return ST_BAD_STATE;

      le.ipv6MCGroupScopeLevel    = CPSS_IPV6_PREFIX_SCOPE_GLOBAL_E;
      le.numOfPaths               = 0;
      le.routeEntryBaseIndex      = idx;
      le.routeType                = CPSS_DXCH_IP_ECMP_ROUTE_ENTRY_GROUP_E;
      le.sipSaCheckMismatchEnable = GT_FALSE;
      le.ucRPFCheckEnable         = GT_FALSE;

      rc = CRP (cpssDxChIpLpmIpv4McEntryAdd
                (0, 0, d, 32, s, splen, &le, GT_TRUE, GT_TRUE));
      if (rc == GT_OK) {
        DEBUG ("Add route from group %d.%d.%d.%d to idx %d.\n",
               d.arIP [0], d.arIP [1], d.arIP [2], d.arIP [3], idx);
        return ST_OK;
      }

      if (via != 0xFFFF)
        mcre_put (dst, src, src_vid);
      return ST_HEX;
    } else { // idx already exists
      DEBUG ("Idx = %d. Adding node.\n", idx);
      res = mcre_add_node (idx, via, vid);
      if (!res)
        return ST_OK;
    }

  } else
    return ST_BAD_VALUE;
  return ST_BAD_VALUE;
}

enum status
route_mc_del (vid_t vid, const uint8_t *dst, const uint8_t *src, mcg_t via,
              vid_t src_vid)
{
  CPSS_DXCH_IP_LTT_ENTRY_STC le;
  GT_STATUS rc;
  GT_IPADDR s, d;
  int splen, res;
  GT_U32 gri, gci, sri, sci;

  memcpy (&d.arIP, dst, sizeof (d.arIP));
  memcpy (&s.arIP, src, sizeof (s.arIP));

  DEBUG ("Deleting route of vlan %d, vidx %d from "
         "group %d.%d.%d.%d of vlan %d\n",
         vid, via,
         d.arIP [0], d.arIP [1], d.arIP [2], d.arIP [3],
         src_vid);

  if (s.u32Ip == 0)
    splen = 0;
  else
    splen = 32;

  DEBUG ("Looking such prefix...\n");

  rc = CRP (cpssDxChIpLpmIpv4McEntrySearch
            (0, 0, d, 32, s, splen, &le, &gri, &gci, &sri, &sci));
  ON_GT_ERROR (rc)
    goto out_err;

  if (le.routeEntryBaseIndex != DROP_MC_RE_IDX)
  {
    DEBUG ("Prefix found.\n");
    res = mcre_del_node (le.routeEntryBaseIndex, via, vid, src_vid);

    if (!res) {
      rc = CRP (cpssDxChIpLpmIpv4McEntryDel (0, 0, d, 32, s, splen));
      ON_GT_ERROR (rc)
        goto out_err;
    }

    return ST_OK;
  }

 out_err:
  DEBUG ("Prefix was not found/\n");
  return ST_HEX;
}

void *
route_get_udaddrs(void) {
  void  *r = fib_get_routes_addr(true);
  if (!r)
    return NULL;

  return r;
}

void
route_reset_prefixes4gw(uint32_t addr, int alen) {
  struct fib_entry *fib;
  struct route rt;
  fib = fib_get(ntohl(addr), alen);
  if (fib)
    fib_to_route(fib, &rt);
    route_mutex_lock();
    route_del(&rt);
    route_add(&rt);
    route_mutex_unlock();
}

// void
// route_dump(void) {
//   struct pfxs_by_gw *s, *t;
//   struct pfxs_ipv6_by_gw *s_v6, *t_v6;
//   DEBUG("!!!! ROUTE DUMP  !!!!\n");
//   HASH_ITER (hh, pfxs_by_gw, s, t) {
//     DEBUG(IPv4_FMT ":%3d, %p\n",  IPv4_ARG(s->gw.addr.arIP), s->gw.vid, s->pfxs);
//     if (s->pfxs) {
//       struct pfx_by_pfx *s1,*t1;
//       HASH_ITER (hh, s->pfxs, s1, t1) {
//         DEBUG("\t"IPv4_FMT "/%2d, %x, %p\n",  IPv4_ARG(s1->pfx.addr.arIP), s1->pfx.alen, s1->udaddr, s1);
//       }
//     }
//   }
//   DEBUG("!!!! end GW DUMP!!!!\n\n");

//   DEBUG("!!!! ROUTE DUMP IPV6  !!!!\n");
//   HASH_ITER (hh, pfxs_ipv6_by_gw, s_v6, t_v6) {
//     DEBUG(IPv6_FMT ":%3d, %p\n",  IPv6_ARG(s_v6->gw.addr.arIP), s_v6->gw.vid, s_v6->pfxs);
//     if (s_v6->pfxs) {
//       struct pfx_ipv6_by_pfx *s1_v6,*t1_v6;
//       HASH_ITER (hh, s_v6->pfxs, s1_v6, t1_v6) {
//         DEBUG("\t"IPv6_FMT "/%2d, "IPv6_FMT", %p\n",  IPv6_ARG(s1_v6->pfx.addrv6.arIP), s1_v6->pfx.alen, IPv6_ARG(s1_v6->udaddr.arIP), s1_v6);
//       }
//     }
//   }
//   DEBUG("!!!! end GW DUMP IPV6!!!!\n\n");
/*struct pfxs_by_gw {
  struct gw gw;
  struct pfx_by_pfx *pfxs;
  UT_hash_handle hh;
};*/

// }

pthread_mutex_t route_mutex;
pthread_mutex_t queue_mutex;

struct list_queue {
  pthread_t tid;
  struct list_queue *next;
};

struct list_queue *head = NULL;

// int list_queue_cmp(struct list_queue *a, struct list_queue *b) {
//   return !pthread_equal(a->tid, b->tid);
// }

void route_mutex_init() {
  pthread_mutex_init(&route_mutex, NULL);
  pthread_mutex_init(&queue_mutex, NULL);
}

void route_mutex_lock() {
  struct list_queue *add = NULL, *del = NULL;
  pthread_mutex_lock(&queue_mutex);
  add = calloc(1, sizeof(struct list_queue));
  add->tid = pthread_self();
  LL_APPEND(head, add);
  pthread_mutex_unlock(&queue_mutex);

  back:
    pthread_mutex_lock(&route_mutex);

    pthread_mutex_lock(&queue_mutex);
    if (!pthread_equal(head->tid, pthread_self())) {
      pthread_mutex_unlock(&queue_mutex);
      pthread_mutex_unlock(&route_mutex);
      goto back;
    }
    del = head;
    LL_DELETE(head, del);
    free(del);

    pthread_mutex_unlock(&queue_mutex);
}

void route_mutex_unlock(){
  pthread_mutex_unlock(&route_mutex);
}
