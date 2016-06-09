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
#include <arpc.h>
#include <sysdeps.h>
#include <mcg.h>
#include <mac.h>
#include <stack.h>
#include <debug.h>
#include <utils.h>

#include <uthash.h>


#define HASH_FIND_PFX(head, findpfx, out)                       \
  HASH_FIND (hh, head, findpfx, sizeof (struct route_pfx), out)
#define HASH_ADD_PFX(head, pfxfield, add)                       \
  HASH_ADD (hh, head, pfxfield, sizeof (struct route_pfx), add)

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
    mcRouteEntry.cmd = CPSS_PACKET_CMD_DROP_HARD_E;
    mcRouteEntry.RPFFailCommand = CPSS_PACKET_CMD_DROP_HARD_E;
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
  }

  rc = CRP (cpssDxChIpLpmVirtualRouterAdd (lpmDbId, 0, &vrConfigInfo));

  return rc;
}

enum status
route_set_router_mac_addr (mac_addr_t addr)
{
  GT_ETHERADDR ra;
  int d;

  memcpy (ra.arEther, addr, sizeof (addr));
  for_each_dev (d)
    CRP (cpssDxChIpRouterMacSaBaseSet (d, &ra));

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
route_register (uint32_t addr, int alen, uint32_t gwaddr, vid_t vid)
{
  GT_IPADDR ip;
  struct gw gw;
  struct route_pfx pfx;
  struct pfxs_by_gw *pbg;
  struct pfx_by_pfx *pbp;

  DEBUG ("register pfx %d.%d.%d.%d/%d gw %d.%d.%d.%d\r\n",
         (addr >> 24) & 0xFF,
         (addr >> 16) & 0xFF,
         (addr >> 8) & 0xFF,
         addr & 0xFF,
         alen,
         (gwaddr >> 24) & 0xFF,
         (gwaddr >> 16) & 0xFF,
         (gwaddr >> 8) & 0xFF,
         gwaddr & 0xFF);

  ip.u32Ip = htonl (gwaddr);
  route_fill_gw (&gw, &ip, vid);
  HASH_FIND_GW (pfxs_by_gw, &gw, pbg);
  if (!pbg) {
    pbg = calloc (1, sizeof (*pbg));
    pbg->gw = gw;
    HASH_ADD_GW (pfxs_by_gw, gw, pbg);
  }

  pfx.addr.u32Ip = htonl (addr);
  pfx.alen = alen;
  HASH_FIND_PFX (pbg->pfxs, &pfx, pbp);
  if (!pbp) {
    pbp = calloc (1, sizeof (*pbp));
    pbp->pfx = pfx;
    HASH_ADD_PFX (pbg->pfxs, pfx, pbp);
  }
}

static void
route_unregister (uint32_t addr, int alen, uint32_t gwaddr, vid_t vid)
{
  GT_IPADDR ip;
  struct gw gw;
  struct route_pfx pfx;
  struct pfxs_by_gw *pbg;
  struct pfx_by_pfx *pbp;

  DEBUG ("unregister pfx %d.%d.%d.%d/%d gw %d.%d.%d.%d\r\n",
         (addr >> 24) & 0xFF,
         (addr >> 16) & 0xFF,
         (addr >> 8) & 0xFF,
         addr & 0xFF,
         alen,
         (gwaddr >> 24) & 0xFF,
         (gwaddr >> 16) & 0xFF,
         (gwaddr >> 8) & 0xFF,
         gwaddr & 0xFF);

  ip.u32Ip = htonl (gwaddr);
  route_fill_gw (&gw, &ip, vid);
  HASH_FIND_GW (pfxs_by_gw, &gw, pbg);
  if (!pbg)
    return;

  pfx.addr.u32Ip = htonl (addr);
  pfx.alen = alen;
  HASH_FIND_PFX (pbg->pfxs, &pfx, pbp);
  if (!pbp)
    return;

  HASH_DEL (pbg->pfxs, pbp);
  free (pbp);

  ret_unref (&gw, alen == 0);

  if (!pbg->pfxs) {
    HASH_DEL (pfxs_by_gw, pbg);
    free (pbg);
  }
}

enum status
route_add (const struct route *rt)
{
DEBUG(">>>>route_add(pfx.addr== %x, pfx.alen== %d, vid== %d, gw== %x)\n",
    ntohl (rt->pfx.addr.u32Ip), rt->pfx.alen, rt->vid, ntohl (rt->gw.u32Ip));

  fib_add (ntohl (rt->pfx.addr.u32Ip),
           rt->pfx.alen,
           rt->vid,
           ntohl (rt->gw.u32Ip));

  DEBUG ("add route to %d.%d.%d.%d/%d via %d.%d.%d.%d\r\n",
         rt->pfx.addr.arIP[0], rt->pfx.addr.arIP[1],
         rt->pfx.addr.arIP[2], rt->pfx.addr.arIP[3],
         rt->pfx.alen,
         rt->gw.arIP[0], rt->gw.arIP[1],
         rt->gw.arIP[2], rt->gw.arIP[3]);

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
      DEBUG ("install prefix %d.%d.%d.%d/%d via %d\r\n",
             pbp->pfx.addr.arIP[0], pbp->pfx.addr.arIP[1],
             pbp->pfx.addr.arIP[2], pbp->pfx.addr.arIP[3],
             pbp->pfx.alen, idx);
      CRP (cpssDxChIpLpmIpv4UcPrefixAdd
           (0, 0, pbp->pfx.addr, pbp->pfx.alen, &re, GT_TRUE));
    }
  }
}

void
route_del_fib_entry (struct fib_entry *e)
{
  GT_IPADDR pfx, gwip;

  pfx.u32Ip = htonl (fib_entry_get_pfx (e));
  gwip.u32Ip = htonl (fib_entry_get_gw (e));

  DEBUG ("delete prefix %d.%d.%d.%d/%d via %d.%d.%d.%d\r\n",
         pfx.arIP[0], pfx.arIP[1],
         pfx.arIP[2], pfx.arIP[3],
         fib_entry_get_len (e),
         gwip.arIP[0], gwip.arIP[1],
         gwip.arIP[2], gwip.arIP[3]);

  if (fib_entry_get_len (e) != 0)
    CRP (cpssDxChIpLpmIpv4UcPrefixDel (0, 0, pfx, fib_entry_get_len (e)));

  route_unregister (fib_entry_get_pfx (e), fib_entry_get_len (e),
                    fib_entry_get_gw (e), fib_entry_get_vid (e));

  DEBUG ("done\r\n");
}

enum status
route_del (const struct route *rt)
{
DEBUG(">>>>route_del(pfx.addr== %x, pfx.alen== %d, vid== %d, gw== %x)\n",
    ntohl (rt->pfx.addr.u32Ip), rt->pfx.alen, rt->vid, ntohl (rt->gw.u32Ip));

  if (!fib_del (ntohl (rt->pfx.addr.u32Ip), rt->pfx.alen))
    DEBUG ("prefix %d.%d.%d.%d/%d not found\r\n",
         rt->pfx.addr.arIP[0], rt->pfx.addr.arIP[1],
         rt->pfx.addr.arIP[2], rt->pfx.addr.arIP[3],
         rt->pfx.alen);

  return ST_OK;
}

enum status
route_add_mgmt_ip (ip_addr_t addr)
{
DEBUG(">>>>route_add_mgmt_ip (" IPv4_FMT ")", IPv4_ARG(addr));

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
DEBUG(">>>>route_del_mgmt_ip (" IPv4_FMT ")", IPv4_ARG(addr));
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
route_request_mac_addr (uint32_t gwip, vid_t vid, uint32_t ip, int alen)
{
  struct gw gw;
  GT_IPADDR gwaddr;
  int ix;

  gwaddr.u32Ip = htonl (gwip);
  route_fill_gw (&gw, &gwaddr, vid);
  ix = ret_add (&gw, alen == 0);
  DEBUG ("route entry index %d\r\n", ix);
  if ((ix >= 0) && (alen != 0)) {
    CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
    GT_IPADDR addr;

    memset (&re, 0, sizeof (re));
    re.ipLttEntry.routeEntryBaseIndex = ix;
    addr.u32Ip = htonl (ip);
    CRP (cpssDxChIpLpmIpv4UcPrefixAdd (0, 0, addr, alen, &re, GT_TRUE));
  }
}

#define MIN_IPv4_PKT_LEN (12 + 2 + 20)

void
route_handle_udaddr (uint32_t daddr) {
DEBUG(">>>>route_handle_udaddr (%x)\n", daddr);
  uint32_t rt;
  int alen;
  const struct fib_entry *e;

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
  route_prefix_set_drop (fib_entry_get_pfx (e), alen);
  route_register (fib_entry_get_pfx (e), alen, rt, fib_entry_get_vid (e));
  route_request_mac_addr (rt, fib_entry_get_vid (e), fib_entry_get_pfx (e), alen);

  DEBUG ("got packet to %d.%d.%d.%d, gw %d.%d.%d.%d\r\n",
         (daddr >> 24) & 0xFF, (daddr >> 16) & 0xFF,
         (daddr >> 8) & 0xFF, daddr & 0xFF,
         (rt >> 24) & 0xFF, (rt >> 16) & 0xFF,
         (rt >> 8) & 0xFF, rt & 0xFF);
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

  route_handle_udaddr (daddr);
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
