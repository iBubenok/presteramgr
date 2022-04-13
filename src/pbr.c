#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIpTypes.h>

#include <pbr.h>
#include <pcl.h>
#include <log.h>
#include <debug.h>
#include <lttindex.h>
#include <route.h>
#include <ret.h>
#include <sysdeps.h>

#include <uthash.h>
#include <port.h>

#define FR(term)                   \
  if ((term) != ST_OK) {  \
    DEBUG("\nError: %s\n", #term); \
    return ST_BAD_VALUE;           \
  }

struct pbr_entry {
  struct pcl_interface interface;
  uint32_t gw;
  vid_t vid;
  struct row_colum ltt_index;
  struct gw ret_key;
  UT_hash_handle hh;  
};

static struct pbr_entry *pbr_entry = NULL;



static enum status
get_port_ptr (uint16_t pid, struct port** port)
{
  (*port) = port_ptr(pid);
  if (!(*port)) {
    DEBUG("%s: port: %d - invalid port_ptr (NULL)\n", __FUNCTION__, pid);
    return ST_BAD_VALUE;
  }
  if (is_stack_port(*port)) {
    DEBUG("%s: port: %d - is stack port\n", __FUNCTION__, pid);
    return ST_BAD_VALUE;
  }
  return ST_OK;
}




enum status pbr_ltt_tcam_set(struct row_colum *ltt_index, ip_addr_t nextHop, struct pcl_interface interface, int ix) {
  int devs[NDEVS];
  int dev_count = 0;
  int d;
  struct port *port = NULL;
  switch (interface.type) {
    case PCL_INTERFACE_TYPE_VLAN:
      dev_count = NDEVS;
      for_each_dev(d) { devs[d] = d; };
      break;
    case PCL_INTERFACE_TYPE_PORT:
      FR(get_port_ptr(interface.num, &port));
      dev_count = 1;
      devs[0]   = port->ldev;
      break;
    default:
      return ST_BAD_VALUE;
      
  };

  for (d = 0; d < dev_count; d++) {   
    CPSS_DXCH_IP_LTT_ENTRY_STC ipLttEntry;
    memset(&ipLttEntry, 0, sizeof(CPSS_DXCH_IP_LTT_ENTRY_STC));

    ipLttEntry.routeEntryBaseIndex = ix;    
    CRP (cpssDxChIpLttWrite(devs[d], ltt_index->row, ltt_index->colum, &ipLttEntry));

    // CPSS_DXCH_IPV4_PREFIX_STC   prefixPtr;
    // CPSS_DXCH_IPV4_PREFIX_STC   maskPtr;

    // prefixPtr.vrId = 0;
    // prefixPtr.isMcSource = GT_FALSE;
    // // prefixPtr.ipAddr.u32Ip = nextHop;
    // memcpy(prefixPtr.ipAddr.arIP, nextHop,4);

    // maskPtr.vrId = 0;
    // maskPtr.isMcSource = GT_FALSE;
    // maskPtr.ipAddr.u32Ip = 0xFFFFFFFF;
    // // rc = cpssDxChIpv4PrefixSet(devs[d], ltt_index->row, ltt_index->colum, &prefixPtr, &maskPtr);
    // // DEBUG("SUFIK %u\n", rc);
    // CRP (cpssDxChIpv4PrefixSet(devs[d], ltt_index->row, ltt_index->colum, &prefixPtr, &maskPtr));
  }
  return ST_OK;
}




enum status pbr_route_set(struct row_colum *ltt_index, ip_addr_t nextHop, vid_t vid, struct pcl_interface interface) {
  struct pbr_entry *pe = NULL;
  struct gw gw;
  GT_IPADDR gwaddr;
  int ix;

  
  HASH_FIND(hh, pbr_entry, &interface, sizeof(interface), pe);
  if (!pe) {
    pe = calloc(1, sizeof(struct pbr_entry));
    pe->interface = interface;
    memcpy(&pe->gw, nextHop, 4);
    pe->vid = vid;
    pe->ltt_index = *ltt_index;
    HASH_ADD(hh, pbr_entry, interface, sizeof(interface), pe);
  }

  memcpy(gwaddr.arIP, nextHop, 4);
  route_fill_gw (&gw, &gwaddr, vid);
  ix = ret_add (&gw, true, &pe->ret_key);
  if (ix == -1) { 
    DEBUG("SUFIK\n");
    pbr_ltt_tcam_set(ltt_index, nextHop, interface, DEFAULT_UC_RE_IDX);
  }
  else {
    DEBUG("SUFIK\n");
    pbr_ltt_tcam_set(ltt_index, nextHop, interface, ix);
  }

  return ST_OK;
};

enum status pbr_route_unset(struct pcl_interface interface) {
  struct pbr_entry *pe = NULL;
  struct gw gw;
  GT_IPADDR gwaddr;

  HASH_FIND(hh, pbr_entry, &interface, sizeof(interface), pe);
  if (!pe) return ST_OK;
  if (pe){
    memcpy(gwaddr.arIP, &pe->gw, 4);
    route_fill_gw (&gw, &gwaddr, pe->vid);
    ret_unref(&gw, false);
    ltt_index_del(&pe->ltt_index);
    HASH_DEL(pbr_entry, pe);
    free(pe);
  }
  return ST_OK;
};
