#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/pcl/cpssDxChPcl.h>
#include <cpss/dxCh/dxChxGen/cnc/cpssDxChCnc.h>

#include <pcl.h>
#include <port.h>
#include <dev.h>
#include <log.h>
#include <vlan.h>
#include <utils.h>
#include <sysdeps.h>
#include <uthash.h>
#include <debug.h>
#include <zmq.h>
#include <czmq.h>

static int max_port_id[NDEVS];

static int port_ix_on_dev[NPORTS + 1];

static int port_ipcl_id[NPORTS + 1];
static int port_epcl_id[NPORTS + 1];
static int vlan_ipcl_id[4095];

static int port_stack_secondary_ipv4_mc_drop_e_ix;
static int port_stack_secondary_ipv6_mc_drop_e_ix;
static int port_stack_secondary_ipv4_mc_drop_i_ix;
static int port_stack_secondary_ipv6_mc_drop_i_ix;

static int port_lbd_rule_ix[NPORTS + 1];

static int per_port_dhcptrap_rules_count = 2;
static int port_dhcptrap_rule_ix[NPORTS + 1];

static int stack_entries = 300;
static int stack_first_entry[NDEVS];
static int stack_max[NDEVS];

static int port_ipcl_def_ix[NPORTS + 1];
static int port_epcl_def_ix[NPORTS + 1];

static int port_arp_inspector_trap_ix[NPORTS + 1];

static int per_port_ip_source_guard_rules_count = 10;
static int port_ip_sourceguard_rule_start_ix[NPORTS + 1];
static int port_ip_sourceguard_drop_rule_ix[NPORTS + 1];

static int per_port_ip_ospf_rules_count = 2;
static int port_ip_ospf_mirror_rule_ix[NPORTS + 1];

static int user_acl_start_ix[NDEVS];

#define for_each_port(p) for (p = 1; p <= nports; p++)

static inline int
find_port_ix_on_dev (int pid) {
  int dev = (port_ptr(pid))->ldev;
  DECLARE_PORT_MAP(pm);

  int ix = -1;
  int i;

  for (i = 0; i < NPORTS; i++) {
    if ( pm[i].dev == dev ) {
      ix++;
      if ( i == (pid - 1) ) {
        return ix;
      }
    }
  }
  return -1;
}

static inline void
initialize_vars (void) {
  int dev, pid, vid;

  for_each_dev(dev) {
    max_port_id[dev] = 0;
    for_each_port(pid) {
      if ( (port_ptr(pid))->ldev == dev && (pid > max_port_id[dev]) ) {
        max_port_id[dev] = pid;
      }
    }
  }

  for_each_port(pid) {
    port_ix_on_dev[pid] = find_port_ix_on_dev(pid);
  }

  if ( stack_active() ) {
    port_stack_secondary_ipv4_mc_drop_e_ix = 1;
    port_stack_secondary_ipv6_mc_drop_e_ix = 2;
    port_stack_secondary_ipv4_mc_drop_i_ix = 3;
    port_stack_secondary_ipv6_mc_drop_i_ix = 4;
  } else {
    port_stack_secondary_ipv4_mc_drop_e_ix = 0;
    port_stack_secondary_ipv4_mc_drop_e_ix = 0;
    port_stack_secondary_ipv4_mc_drop_e_ix = 0;
    port_stack_secondary_ipv4_mc_drop_e_ix = 0;
  }

  for_each_port(pid) {
    port_ipcl_id[pid] = port_ix_on_dev[pid] * 2;
    port_epcl_id[pid] = port_ipcl_id[pid] + 1;
  }

  for (vid = 1; vid <= 4094; vid++) {
    vlan_ipcl_id[vid] = port_ipcl_id[NPORTS] + 2*vid;
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    if ( stack_active() && (port->ldev == stack_sec_port->ldev) ) {
      port_lbd_rule_ix[pid] =
        port_stack_secondary_ipv6_mc_drop_i_ix + port_ix_on_dev[pid] + 1;
    } else {
      port_lbd_rule_ix[pid] = port_ix_on_dev[pid] + 1;
    }
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    port_dhcptrap_rule_ix[pid] =
      port_lbd_rule_ix[max_port_id[port->ldev]] +
      port_ix_on_dev[pid] * per_port_dhcptrap_rules_count + 1;
  }

  for_each_dev(dev) {
    stack_first_entry[dev] =
      port_dhcptrap_rule_ix[max_port_id[dev]] + per_port_dhcptrap_rules_count;
    stack_max[dev] = stack_first_entry[dev] + stack_entries;
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    port_ipcl_def_ix[pid] = stack_max[port->ldev] + port_ix_on_dev[pid] * 2;
    port_epcl_def_ix[pid] = port_ipcl_def_ix[pid] + 1;
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    port_arp_inspector_trap_ix[pid] =
      port_epcl_def_ix[max_port_id[port->ldev]] + port_ix_on_dev[pid] + 1;
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    port_arp_inspector_trap_ix[pid] =
      port_epcl_def_ix[max_port_id[port->ldev]] + port_ix_on_dev[pid] + 1;
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    port_ip_sourceguard_rule_start_ix[pid] =
      port_arp_inspector_trap_ix[max_port_id[port->ldev]] +
      port_ix_on_dev[pid] * per_port_ip_source_guard_rules_count + 1;
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    port_ip_sourceguard_drop_rule_ix[pid] =
      port_ip_sourceguard_rule_start_ix[max_port_id[port->ldev]] +
      port_ix_on_dev[pid] + per_port_ip_source_guard_rules_count;
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    port_ip_ospf_mirror_rule_ix[pid] =
      port_ip_sourceguard_drop_rule_ix[max_port_id[port->ldev]] +
      port_ix_on_dev[pid] * per_port_ip_ospf_rules_count + 1;
  }

  for_each_dev(dev) {
    user_acl_start_ix[dev] =
      port_ip_ospf_mirror_rule_ix[max_port_id[dev]] +
      per_port_ip_ospf_rules_count;
  }
}

#define PORT_IPCL_ID(n) \
  port_ipcl_id[n]
#define PORT_EPCL_ID(n) \
  port_epcl_id[n]
#define VLAN_IPCL_ID(n) \
  vlan_ipcl_id[n]

#define PORT_STACK_SECONDARY_IPv4_MC_DROP_E_IX \
  port_stack_secondary_ipv4_mc_drop_e_ix
#define PORT_STACK_SECONDARY_IPv6_MC_DROP_E_IX \
  port_stack_secondary_ipv6_mc_drop_e_ix
#define PORT_STACK_SECONDARY_IPv4_MC_DROP_I_IX \
  port_stack_secondary_ipv4_mc_drop_i_ix
#define PORT_STACK_SECONDARY_IPv6_MC_DROP_I_IX \
  port_stack_secondary_ipv6_mc_drop_i_ix

#define PORT_LBD_RULE_IX(n) \
  port_lbd_rule_ix[n]

#define PER_PORT_DHCPTRAP_RULES_COUNT \
  per_port_dhcptrap_rules_count
#define PORT_DHCPTRAP_RULE_IX(n) \
  port_dhcptrap_rule_ix[n]

#define STACK_ENTRIES \
  stack_entries
#define STACK_FIRST_ENTRY(d) \
  stack_first_entry[d]
#define STACK_MAX(d) \
  stack_max[d]

#define PORT_IPCL_DEF_IX(n) \
  port_ipcl_def_ix[n]
#define PORT_EPCL_DEF_IX(n) \
  port_epcl_def_ix[n]

#define PORT_ARP_INSPECTOR_TRAP_IX(n) \
  port_arp_inspector_trap_ix[n]

#define PER_PORT_IP_SOURCE_GUARD_RULES_COUNT \
  per_port_ip_source_guard_rules_count
#define PORT_IP_SOURCEGUARD_RULE_START_IX(n) \
  port_ip_sourceguard_rule_start_ix[n]
#define PORT_IP_SOURCEGUARD_DROP_RULE_IX(n) \
  port_ip_sourceguard_drop_rule_ix[n]

#define PER_PORT_IP_OSPF_RULES_COUNT \
  per_port_ip_ospf_rules_count
#define PORT_IP_OSPF_MIRROR_RULE_IX(n) \
  port_ip_ospf_mirror_rule_ix[n]

#define USER_ACL_START_IX(d) \
  user_acl_start_ix[d]

#define PCL_TCAM_MAX_RULE_IX 1500

#define USER_ACL_STACK_ENTRIES(d) \
  PCL_TCAM_MAX_RULE_IX - USER_ACL_START_IX(d) + 1

static void __attribute__ ((unused))
print_pcl_indexes (void) {
  int pi, ix, d;

  DEBUG("Port Indexes on device:\r\n");
  for (pi = 1; pi <= nports; pi++) {
    struct port *port = port_ptr(pi);
    DEBUG("PortId: %d ldev: %d lport: %d IxOnDev: %d\r\n",
      pi, port->ldev, port->lport, port_ix_on_dev[pi]);
  }

  DEBUG("\r\nPCL indexes:\r\n");

  for_each_dev(d) {
    DEBUG("Device: %d\r\n", d);

    if ( stack_active() && (d == stack_sec_port->ldev) ) {
      DEBUG("STACK_MC_DROP\r\n");
      DEBUG("%d\r\n", PORT_STACK_SECONDARY_IPv4_MC_DROP_E_IX);
      DEBUG("%d\r\n", PORT_STACK_SECONDARY_IPv6_MC_DROP_E_IX);
      DEBUG("%d\r\n", PORT_STACK_SECONDARY_IPv4_MC_DROP_I_IX);
      DEBUG("%d\r\n", PORT_STACK_SECONDARY_IPv6_MC_DROP_I_IX);
    }

    DEBUG("LBD TRAP\r\n");
    for (pi = 1; pi <= nports; pi++) {
      if ( (port_ptr(pi))->ldev == d ) {
        DEBUG("%d\r\n", PORT_LBD_RULE_IX(pi));
      }
    }

    DEBUG("DHCP TRAP\r\n");
    for (pi = 1; pi <= nports; pi++) {
      if ( (port_ptr(pi))->ldev == d ) {
        DEBUG("%d\r\n", PORT_DHCPTRAP_RULE_IX(pi));
        DEBUG("%d\r\n", PORT_DHCPTRAP_RULE_IX(pi) + 1);
      }
    }

    DEBUG("VT Stack\r\n");
    for (ix = 0; ix < STACK_ENTRIES; ix++) {
      DEBUG("%d\r\n", STACK_FIRST_ENTRY(d) + ix);
    }

    DEBUG("Port Def IX\r\n");
    for (pi = 1; pi <= nports; pi++) {
      if ( (port_ptr(pi))->ldev == d ) {
        DEBUG("%d\r\n", PORT_IPCL_DEF_IX(pi));
        DEBUG("%d\r\n", PORT_EPCL_DEF_IX(pi));
      }
    }

    DEBUG("ARP INSPECTOR\r\n");
    for (pi = 1; pi <= nports; pi++) {
      if ( (port_ptr(pi))->ldev == d ) {
        DEBUG("%d\r\n", PORT_ARP_INSPECTOR_TRAP_IX(pi));
      }
    }

    DEBUG("IP SOURCE GUARD RULES\r\n");
    for (pi = 1; pi <= nports; pi++) {
      if ( (port_ptr(pi))->ldev == d ) {
        for (ix = 0; ix < PER_PORT_IP_SOURCE_GUARD_RULES_COUNT; ix++) {
          DEBUG("%d\r\n", PORT_IP_SOURCEGUARD_RULE_START_IX(pi) + ix);
        }
      }
    }

    DEBUG("IP SOURCE GUARD TRAP\r\n");
    for (pi = 1; pi <= nports; pi++) {
      if ( (port_ptr(pi))->ldev == d ) {
        DEBUG("%d\r\n", PORT_IP_SOURCEGUARD_DROP_RULE_IX(pi));
      }
    }

    DEBUG("IP OSPF MIRROR\r\n");
    for (pi = 1; pi <= nports; pi++) {
      if ( (port_ptr(pi))->ldev == d ) {
        DEBUG("%d\r\n", PORT_IP_OSPF_MIRROR_RULE_IX(pi));
        DEBUG("%d\r\n", PORT_IP_OSPF_MIRROR_RULE_IX(pi) + 1);
      }
    }

    DEBUG("USER_ACL_START_IX\r\n");
    DEBUG("%d\r\n", USER_ACL_START_IX(d));
  }

  DEBUG("End.");
}

static struct stack {
  int sp;
  int n_free;
  uint16_t data[1000];
  //uint16_t *data;
} rules[NDEVS], acl[NDEVS];

static void
pcl_init_rules (void)
{
  int i;
  int d;
  for_each_dev(d) {
    // rules[d].data = NULL;
    // rules[d].data = malloc(STACK_ENTRIES * sizeof(uint16_t));
    // DEBUG("malloc ok\r\n");
    // assert(rules[d].data);
    for (i = 0; i < STACK_ENTRIES; i++) {
      rules[d].data[i] = i + STACK_FIRST_ENTRY(d);
    }

    rules[d].sp = 0;
    rules[d].n_free = STACK_ENTRIES;
  }
}

static void
user_acl_init_rules (void)
{
  int i;
  int d;
  for_each_dev(d) {
    // acl[d].data = NULL;
    // DEBUG("USER_ACL_STACK_ENTRIES(%d) == %d\r\n", d, USER_ACL_STACK_ENTRIES(d));
    // acl[d].data = malloc(USER_ACL_STACK_ENTRIES(d) * sizeof(uint16_t));
    // DEBUG("malloc ok: %p\r\n", acl[d].data);
    // assert(acl[d].data);
    for (i = 0; i < USER_ACL_STACK_ENTRIES(d); i++) {
      acl[d].data[i] = i + USER_ACL_START_IX(d);
    }

    acl[d].sp = 0;
    acl[d].n_free = USER_ACL_STACK_ENTRIES(d);
  }
}

static int
pcl_alloc_rules (int dev, uint16_t *nums, int n)
{
  int i;

  if (rules[dev].n_free < n)
    return 0;

  rules[dev].n_free -= n;
  for (i = 0; i < n; i++)
    nums[i] = rules[dev].data[(rules[dev].sp)++];

  return 1;
}

static void __attribute__ ((unused))
pcl_free_rules (int dev, const uint16_t *nums, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    if (nums[i] < STACK_MAX(dev)) {
      rules[dev].n_free++;
      rules[dev].data[--(rules[dev].sp)] = nums[i];
    }
  }
}

struct vt_ix {
  int key;
  int tunnel;
  vid_t to;
  uint16_t ix[2];
  UT_hash_handle hh;
};

static inline int
vt_key (int pid, int from)
{
  return (pid << 12) | from;
}

static inline int
vt_key_vid (int key)
{
  return key & 0xFFF;
}

static inline int
vt_key_pid (int key)
{
  return (key >> 12) & 0xFFFF;
}

static struct vt_ix *vt_ix = NULL;

static struct vt_ix *
get_vt_ix (port_id_t pid, vid_t from, vid_t to, int tunnel, int alloc)
{
  int key = vt_key (pid, from);
  struct vt_ix *ix;
  struct port *port = port_ptr(pid);

  HASH_FIND_INT (vt_ix, &key, ix);
  if (alloc && !ix) {
    ix = calloc (1, sizeof (struct vt_ix));
    ix->key = key;
    if (from) {
      if (!pcl_alloc_rules (port->ldev, ix->ix, 2)) {
        free (ix);
        return NULL;
      }
    } else {
      /* Default rule for port. */
      ix->ix[0] = PORT_IPCL_DEF_IX (pid);
    }
    HASH_ADD_INT (vt_ix, key, ix);
  }

  if (ix) {
    ix->tunnel = tunnel;
    ix->to = to;
  }

  return ix;
}

static void
invalidate_vt_ix (struct vt_ix *ix)
{
  int d;

  for_each_dev (d)
    CRP (cpssDxChPclRuleInvalidate (d, CPSS_PCL_RULE_SIZE_EXT_E, ix->ix[0]));
  if (vt_key_vid (ix->key))
    for_each_dev (d)
      CRP (cpssDxChPclRuleInvalidate (d, CPSS_PCL_RULE_SIZE_EXT_E, ix->ix[1]));
}

static void
free_vt_ix (int dev, struct vt_ix *ix)
{
  invalidate_vt_ix (ix);
  pcl_free_rules (dev, ix->ix, 2);
  HASH_DEL (vt_ix, ix);
  free (ix);
}

enum status
pcl_remove_vt (port_id_t pid, vid_t from, int tunnel)
{
  struct vt_ix *ix = get_vt_ix (pid, from, 0, tunnel, 0);
  struct port *port = port_ptr (pid);

  if (!ix)
    return ST_DOES_NOT_EXIST;

  free_vt_ix (port->ldev, ix);

  return ST_OK;
}

static void
pcl_enable_vt (struct vt_ix *ix, int enable)
{
  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;
  struct port *port;
  vid_t from, to;
  port_id_t pid;
  int tunnel;

  invalidate_vt_ix (ix);
  if (!enable)
    return;

  from   = vt_key_vid (ix->key);
  to     = ix->to;
  pid    = vt_key_pid (ix->key);
  port   = port_ptr (pid);
  tunnel = ix->tunnel;

  memset (&act, 0, sizeof (act));
  act.actionStop = GT_TRUE;
  act.egressPolicy = GT_FALSE;
  if (to) {
    act.pktCmd = CPSS_PACKET_CMD_FORWARD_E;
    act.vlan.modifyVlan = CPSS_PACKET_ATTRIBUTE_ASSIGN_FOR_ALL_E;
    act.vlan.nestedVlan = gt_bool (tunnel);
    act.vlan.vlanId = to;
    act.vlan.precedence = CPSS_PACKET_ATTRIBUTE_ASSIGN_PRECEDENCE_HARD_E;
  } else
    act.pktCmd = CPSS_PACKET_CMD_DROP_HARD_E;

  memset (&mask, 0, sizeof (mask));
  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.common.vid = from ? 0xFFFF : 0;
  mask.ruleExtNotIpv6.common.sourcePort = 0xFF;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pid);
  rule.ruleExtNotIpv6.common.vid = from;
  rule.ruleExtNotIpv6.common.sourcePort = port->lport;

  DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pid, ix->ix[0], PORT_IPCL_ID (pid));
  CRP (cpssDxChPclRuleSet
       (port->ldev,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
        ix->ix[0],
        0,
        &mask,
        &rule,
        &act));

  if (from && to && !tunnel) {
    memset (&mask, 0, sizeof (mask));
    mask.ruleEgrExtNotIpv6.common.pclId = 0xFFFF;
    mask.ruleEgrExtNotIpv6.common.vid = 0xFFFF;

    memset (&rule, 0, sizeof (rule));
    rule.ruleEgrExtNotIpv6.common.pclId = PORT_EPCL_ID (pid);
    rule.ruleEgrExtNotIpv6.common.vid = to;

    memset (&act, 0, sizeof (act));
    act.pktCmd = CPSS_PACKET_CMD_FORWARD_E;
    act.actionStop = GT_FALSE;
    act.egressPolicy = GT_TRUE;
    act.vlan.modifyVlan = CPSS_PACKET_ATTRIBUTE_ASSIGN_FOR_TAGGED_E;
    act.vlan.nestedVlan = GT_FALSE;
    act.vlan.vlanId = from;
    act.vlan.precedence = CPSS_PACKET_ATTRIBUTE_ASSIGN_PRECEDENCE_HARD_E;

    DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pid, ix->ix[1], PORT_EPCL_ID (pid));
    CRP (cpssDxChPclRuleSet
         (port->ldev,
          CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
          ix->ix[1],
          0,
          &mask,
          &rule,
          &act));
  }
}

void
pcl_port_enable_vt (port_id_t pid, int enable)
{
  struct vt_ix *ix, *tmp;

  HASH_ITER (hh, vt_ix, ix, tmp) {
    if (vt_key_pid (ix->key) == pid)
      pcl_enable_vt (ix, enable);
  }
}

void
pcl_port_clear_vt (port_id_t pid)
{
  struct vt_ix *ix, *tmp;
  struct port *port = port_ptr (pid);

  HASH_ITER (hh, vt_ix, ix, tmp) {
    if (pid == ALL_PORTS || vt_key_pid (ix->key) == pid)
      free_vt_ix (port->ldev, ix);
  }
}

enum status
pcl_setup_vt (port_id_t pid, vid_t from, vid_t to, int tunnel, int enable)
{
  struct vt_ix *ix;

  ix = get_vt_ix (pid, from, to, tunnel, enable);
  if (!ix)
    return ST_BAD_STATE;

  if (enable)
    pcl_enable_vt (ix, 1);

  return ST_OK;
}

/******************************************************************************/
/* OSPF MULTICAST MIRROR                                                      */
/******************************************************************************/

static uint8_t ospf_dest_ip1[4]      = {224, 0, 0, 5};
static uint8_t ospf_dest_ip2[4]      = {224, 0, 0, 6};
static uint8_t ospf_dest_ip_mask[4]  = {255, 255, 255, 255};

void
pcl_setup_ospf(int d) {
  port_id_t pi;

  for(pi = 1; pi <= nports; pi++) {

    struct port *port = port_ptr (pi);
    if (port->ldev != d)
      continue;

    if (is_stack_port(port))
      return;

    CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
    CPSS_DXCH_PCL_ACTION_STC act;

    memset (&mask, 0, sizeof (mask));
    memset (&rule, 0, sizeof (rule));
    memset (&act, 0, sizeof (act));

    mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
    mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
    mask.ruleExtNotIpv6.common.isIp = 0xFF;

    rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pi);
    rule.ruleExtNotIpv6.common.isL2Valid = 1;
    rule.ruleExtNotIpv6.common.isIp = 1;

    memcpy (&rule.ruleExtNotIpv6.dip, ospf_dest_ip1, 4);
    memcpy (&mask.ruleExtNotIpv6.dip, ospf_dest_ip_mask, 4);

    act.pktCmd = CPSS_PACKET_CMD_MIRROR_TO_CPU_E;

    act.actionStop = GT_TRUE;
    act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E + 5;

    DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pi, PORT_IP_OSPF_MIRROR_RULE_IX (pi), PORT_IPCL_ID (pi));
    CRP (cpssDxChPclRuleSet
         (port->ldev,                                       /* devNum         */
          CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E, /* ruleFormat     */
          PORT_IP_OSPF_MIRROR_RULE_IX (pi),                 /* ruleIndex      */
          0,                                                /* ruleOptionsBmp */
          &mask,                                            /* maskPtr        */
          &rule,                                            /* patternPtr     */
          &act));                                           /* actionPtr      */

    memset (&mask, 0, sizeof (mask));
    memset (&rule, 0, sizeof (rule));
    memset (&act, 0, sizeof (act));

    mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
    mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
    mask.ruleExtNotIpv6.common.isIp = 0xFF;

    rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pi);
    rule.ruleExtNotIpv6.common.isL2Valid = 1;
    rule.ruleExtNotIpv6.common.isIp = 1;

    memcpy (&rule.ruleExtNotIpv6.dip, ospf_dest_ip2, 4);
    memcpy (&mask.ruleExtNotIpv6.dip, ospf_dest_ip_mask, 4);

    act.pktCmd = CPSS_PACKET_CMD_MIRROR_TO_CPU_E;

    act.actionStop = GT_TRUE;
    act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E + 5;

    DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pi, PORT_IP_OSPF_MIRROR_RULE_IX (pi) + 1, PORT_IPCL_ID (pi));
    CRP (cpssDxChPclRuleSet
         (port->ldev,                                       /* devNum         */
          CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E, /* ruleFormat     */
          PORT_IP_OSPF_MIRROR_RULE_IX (pi)  + 1,            /* ruleIndex      */
          0,                                                /* ruleOptionsBmp */
          &mask,                                            /* maskPtr        */
          &rule,                                            /* patternPtr     */
          &act));                                           /* actionPtr      */
  }
}

/******************************************************************************/
/* IP SOURCE GUARD                                                            */
/******************************************************************************/

static int sg_trap_enabled[65 /* MAXPORTS */];
static void __attribute__((constructor))
sg_init () {
  memset (sg_trap_enabled, 0, 65);
}

uint16_t
get_port_ip_sourceguard_rule_start_ix (port_id_t pi) {
  return PORT_IP_SOURCEGUARD_RULE_START_IX (pi);
}

uint16_t
get_per_port_ip_sourceguard_rules_count (void) {
  return PER_PORT_IP_SOURCE_GUARD_RULES_COUNT;
}

void
pcl_source_guard_trap_enable (port_id_t pi) {
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return;

  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;

  memset (&mask, 0, sizeof (mask));
  memset (&rule, 0, sizeof (rule));
  memset (&act, 0, sizeof (act));

  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
  mask.ruleExtNotIpv6.common.isIp = 0xFF;

  rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pi);
  rule.ruleExtNotIpv6.common.isL2Valid = 1;
  rule.ruleExtNotIpv6.common.isIp = 1;

  act.pktCmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;

  act.actionStop = GT_TRUE;
  act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E + 2;

  DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pi, PORT_IP_SOURCEGUARD_DROP_RULE_IX (pi), PORT_IPCL_ID (pi));
  CRP (cpssDxChPclRuleSet
       (port->ldev,                                       /* devNum         */
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E, /* ruleFormat     */
        PORT_IP_SOURCEGUARD_DROP_RULE_IX (pi),            /* ruleIndex      */
        0,                                                /* ruleOptionsBmp */
        &mask,                                            /* maskPtr        */
        &rule,                                            /* patternPtr     */
        &act));                                           /* actionPtr      */

  sg_trap_enabled[pi] = 1;
}

void
pcl_source_guard_trap_disable (port_id_t pi) {
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return;

  CRP (cpssDxChPclRuleInvalidate
       (port->ldev,
        CPSS_PCL_RULE_SIZE_EXT_E,
        PORT_IP_SOURCEGUARD_DROP_RULE_IX (pi)));

  sg_trap_enabled[pi] = 0;
}

void
pcl_source_guard_drop_enable (port_id_t pi) {
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return;

  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;

  memset (&mask, 0, sizeof (mask));
  memset (&rule, 0, sizeof (rule));
  memset (&act, 0, sizeof (act));

  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
  mask.ruleExtNotIpv6.common.isIp = 0xFF;

  rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pi);
  rule.ruleExtNotIpv6.common.isL2Valid = 1;
  rule.ruleExtNotIpv6.common.isIp = 1;

  act.pktCmd = CPSS_PACKET_CMD_DROP_HARD_E;

  act.actionStop = GT_TRUE;

  DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pi, PORT_IP_SOURCEGUARD_DROP_RULE_IX (pi), PORT_IPCL_ID (pi));
  CRP (cpssDxChPclRuleSet
       (port->ldev,                                       /* devNum         */
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E, /* ruleFormat     */
        PORT_IP_SOURCEGUARD_DROP_RULE_IX (pi),            /* ruleIndex      */
        0,                                                /* ruleOptionsBmp */
        &mask,                                            /* maskPtr        */
        &rule,                                            /* patternPtr     */
        &act));                                           /* actionPtr      */

  sg_trap_enabled[pi] = 0;
}

void
pcl_source_guard_drop_disable (port_id_t pi) {
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return;

  CRP (cpssDxChPclRuleInvalidate
       (port->ldev,
        CPSS_PCL_RULE_SIZE_EXT_E,
        PORT_IP_SOURCEGUARD_DROP_RULE_IX (pi)));

  sg_trap_enabled[pi] = 0;
}

void
pcl_source_guard_rule_set (port_id_t pi,
                           mac_addr_t mac,
                           vid_t vid,
                           ip_addr_t ip,
                           uint16_t rule_ix,
                           uint8_t  verify_mac) {
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return;

  GT_ETHERADDR source_mac;
  memcpy (&source_mac.arEther, mac, sizeof(mac_addr_t));

  GT_U16 vlan_id;
  memcpy (&vlan_id, &vid, sizeof(vid_t));

  GT_IPADDR source_ip;
  memcpy (&source_ip.arIP, ip, sizeof(ip_addr_t));

  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;

  memset (&mask, 0, sizeof (mask));
  memset (&rule, 0, sizeof (rule));
  memset (&act, 0, sizeof (act));

  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
  mask.ruleExtNotIpv6.common.isIp = 0xFF;

  if (verify_mac) {
    /* Source MAC */
    mask.ruleExtNotIpv6.macSa.arEther[5] = 0xFF;
    mask.ruleExtNotIpv6.macSa.arEther[4] = 0xFF;
    mask.ruleExtNotIpv6.macSa.arEther[3] = 0xFF;
    mask.ruleExtNotIpv6.macSa.arEther[2] = 0xFF;
    mask.ruleExtNotIpv6.macSa.arEther[1] = 0xFF;
    mask.ruleExtNotIpv6.macSa.arEther[0] = 0xFF;
  };

  /* VID */
  mask.ruleExtNotIpv6.common.vid  = 0xFFFF;
  /* Source IP */
  mask.ruleExtNotIpv6.sip.arIP[0] = 0xFF;
  mask.ruleExtNotIpv6.sip.arIP[1] = 0xFF;
  mask.ruleExtNotIpv6.sip.arIP[2] = 0xFF;
  mask.ruleExtNotIpv6.sip.arIP[3] = 0xFF;


  rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pi);
  rule.ruleExtNotIpv6.common.isL2Valid = 1;
  rule.ruleExtNotIpv6.common.isIp = 1;

  if (verify_mac) {
    /* Source MAC */
    rule.ruleExtNotIpv6.macSa = source_mac;
  }

  /* VID */
  rule.ruleExtNotIpv6.common.vid  = vlan_id;
  /* Source IP */
  rule.ruleExtNotIpv6.sip = source_ip;


  act.pktCmd = CPSS_PACKET_CMD_FORWARD_E;
  act.actionStop = GT_TRUE;

  DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pi, rule_ix, PORT_IPCL_ID (pi));
  CRP (cpssDxChPclRuleSet
       (port->ldev,                                       /* devNum         */
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E, /* ruleFormat     */
        rule_ix,                                          /* ruleIndex      */
        0,                                                /* ruleOptionsBmp */
        &mask,                                            /* maskPtr        */
        &rule,                                            /* patternPtr     */
        &act));                                           /* actionPtr      */
}

void
pcl_source_guard_rule_unset (port_id_t pi, uint16_t rule_ix) {
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return;

  CRP (cpssDxChPclRuleInvalidate
       (port->ldev,
        CPSS_PCL_RULE_SIZE_EXT_E,
        rule_ix));
}

int
pcl_source_guard_trap_enabled (port_id_t pi) {
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return 0;

  if ((pi < 1) || (pi > 65) || !port)
    return 0;

  return sg_trap_enabled[pi];
}

/******************************************************************************/
/* USER ACLs                                                                  */
/******************************************************************************/
uint32_t
allocate_user_rule_ix (uint16_t pid_or_vid) {
  /* Magic */
  if ( pid_or_vid < 10000 ) {
    struct port *port = port_ptr(pid_or_vid);
    int dev = port->ldev;
    acl[dev].n_free--;
    return acl[dev].data[(acl[dev].sp)++];
  } else {
    uint32_t aggregated_ix = 0;
    int d;
    for_each_dev(d) {
      acl[d].n_free--;
      aggregated_ix += ((acl[d].data[(acl[d].sp)++]) << d*16);
    }
    return aggregated_ix;
  }
}

void
free_user_rule_ix (uint16_t pid_or_vid, uint32_t rule_ix) {
  /* Magic */
  if ( pid_or_vid < 10000 ) {
    struct port *port = port_ptr(pid_or_vid);
    int dev = port->ldev;
    if (rule_ix < PCL_TCAM_MAX_RULE_IX) {
      acl[dev].n_free++;
      acl[dev].data[--(acl[dev].sp)] = rule_ix;
    }
  } else {
    int d;
    for_each_dev(d) {
      uint16_t ix = (rule_ix & (0xffff << d*16)) >> (d*16);
      if (ix < PCL_TCAM_MAX_RULE_IX) {
        acl[d].n_free++;
        acl[d].data[--(acl[d].sp)] = ix;
      }
    }
  }
}

uint8_t
check_user_rule_ix_count (uint16_t pid_or_vid, uint16_t count) {
  /* Magic */
  if ( pid_or_vid < 10000 ) {
    struct port *port = port_ptr(pid_or_vid);
    int dev = port->ldev;
    return acl[dev].n_free >= count;
  } else {
    int d;
    uint8_t result = 1;
    for_each_dev(d) {
      result = (result && (acl[d].n_free >= count));
    }
    return result;
  }
}

#define PRINT_SEPARATOR(c, size) { \
  char __SEPARATOR__[size];        \
  memset (__SEPARATOR__, c, size); \
  DEBUG("%s\r\n", __SEPARATOR__);  \
}

#define get_port_ptr(port, pid) {                                           \
  if ( pid < 10000 ) {                                                      \
    port = port_ptr(pid);                                                   \
    if (!port) {                                                            \
      DEBUG("%s: port: %d - invalid port_ptr (NULL), function returns\r\n", \
            __FUNCTION__, pid);                                             \
      goto out;                                                             \
    }                                                                       \
    if (is_stack_port(port)) {                                              \
      DEBUG("%s: port: %d - is stack port, function returns\r\n",           \
            __FUNCTION__, pid);                                             \
      goto out;                                                             \
    }                                                                       \
  } else {                                                                  \
    port = NULL;                                                              \
  }                                                                         \
}

#define inactivate_rule(dev, type, rule_ix) {                      \
  int status = CRP(cpssDxChPclRuleInvalidate(dev, type, rule_ix)); \
  if (status != GT_OK) {                                           \
    DEBUG("%s: %s: failed, function returns\r\n", __FUNCTION__,    \
          "cpssDxChPclRuleInvalidate");                            \
  } else {                                                         \
    DEBUG("%s: ok\r\n", __FUNCTION__);                             \
  }                                                                \
  goto out;                                                        \
}

static int
set_pcl_action (uint16_t pid_or_vid, uint16_t rule_ix, uint8_t action,
                uint8_t trap_action, CPSS_DXCH_PCL_ACTION_STC *act) {
  memset (act,  0, sizeof(*act));
  switch (action) {
    case PCL_ACTION_PERMIT:
      DEBUG("%s: %s\r\n", __FUNCTION__, "CPSS_PACKET_CMD_FORWARD_E");
      act->pktCmd = CPSS_PACKET_CMD_FORWARD_E;
      break;
    case PCL_ACTION_DENY:
      DEBUG("%s: %s\r\n", __FUNCTION__, "CPSS_PACKET_CMD_DROP_HARD_E");
      act->pktCmd = CPSS_PACKET_CMD_DROP_HARD_E;
      break;
    default:
      DEBUG("%s: action: invalid action (%d), function returns\r\n",
            __FUNCTION__, action);
      return 0;
  };

  switch (trap_action) {
    case PCL_TRAP_ACTION_LOG_INPUT:
      DEBUG("%s: %s\r\n", __FUNCTION__, "PCL_TRAP_ACTION_LOG_INPUT");
      act->matchCounter.enableMatchCount  = GT_TRUE;
      act->matchCounter.matchCounterIndex = rule_ix;
      DEBUG("%s: matchCounterIndex: %d\r\n",
            __FUNCTION__, act->matchCounter.matchCounterIndex);
      break;
    case PCL_TRAP_ACTION_DISABLE_PORT:
      DEBUG("%s: %s\r\n", __FUNCTION__, "PCL_TRAP_ACTION_DISABLE_PORT");
      break;
    case PCL_TRAP_ACTION_NONE:
      DEBUG("%s: %s\r\n", __FUNCTION__, "PCL_TRAP_ACTION_NONE");
      break;
    default:
      DEBUG("%s: action: invalid action (%d), function returns\r\n",
            __FUNCTION__, trap_action);
      return 0;
  };

  act->actionStop = GT_TRUE;

  return 1;
}

#define set_pcl_id(rule, mask, format, pcl_id) {    \
  rule.format.common.pclId = pcl_id;                \
  mask.format.common.pclId = 0xFFFF;                \
  DEBUG("%s: pclId: %d\r\n", __FUNCTION__, pcl_id); \
}

#define set_packet_type_ip(rule, mask, format) { \
  rule.format.common.isL2Valid = 1;              \
  mask.format.common.isL2Valid = 0xFF;           \
  rule.format.common.isIp = 1;                   \
  mask.format.common.isIp = 0xFF;                \
}

#define set_packet_type_mac(rule, mask, format) { \
  rule.format.common.isL2Valid = 1;               \
  mask.format.common.isL2Valid = 0xFF;            \
  rule.format.l2Encap =1;                         \
  mask.format.l2Encap = 0xFF;                     \
}

#define set_packet_type_ipv6(rule, mask, format) { \
  rule.format.common.isL2Valid = 1;                \
  mask.format.common.isL2Valid = 0xFF;             \
  rule.format.common.isIp = 1;                     \
  mask.format.common.isIp = 0xFF;                  \
  rule.format.commonExt.isIpv6 = 1;                \
  mask.format.commonExt.isIpv6 = 0xFF;             \
}

#define set_ip_protocol(rule, mask, format, proto) {                         \
  /* 0xFF: reserved for any IP protocol */                                   \
  if (proto != 0xFF) {                                                       \
    rule.format.commonExt.ipProtocol = proto;                                \
    mask.format.commonExt.ipProtocol = 0xFF;                                 \
  }                                                                          \
  DEBUG("%s: ipProtocol: value: 0x%X, mask:0x%X\r\n", __FUNCTION__,          \
        rule.format.commonExt.ipProtocol, mask.format.commonExt.ipProtocol); \
}

#define set_src_ip(rule, mask, format, src_ip, src_ip_mask) {        \
  memcpy (&rule.format.sip, &src_ip, sizeof(GT_IPADDR));             \
  memcpy (&mask.format.sip, &src_ip_mask, sizeof(GT_IPADDR));        \
  DEBUG("%s: src_ip: %d.%d.%d.%d, sip: 0x%X\r\n", __FUNCTION__,      \
        ip_addr_to_printf_arg(src_ip), rule.format.sip.u32Ip);       \
  DEBUG("%s: src_ip_mask: %d.%d.%d.%d, sip: 0x%X\r\n", __FUNCTION__, \
        ip_addr_to_printf_arg(src_ip_mask), mask.format.sip.u32Ip);  \
}

#define set_dst_ip(rule, mask, format, dst_ip, dst_ip_mask) {        \
  memcpy (&rule.format.dip, &dst_ip, sizeof(GT_IPADDR));             \
  memcpy (&mask.format.dip, &dst_ip_mask, sizeof(GT_IPADDR));        \
  DEBUG("%s: dst_ip: %d.%d.%d.%d, dip: 0x%X\r\n", __FUNCTION__,      \
        ip_addr_to_printf_arg(dst_ip), rule.format.dip.u32Ip);       \
  DEBUG("%s: dst_ip_mask: %d.%d.%d.%d, dip: 0x%X\r\n", __FUNCTION__, \
        ip_addr_to_printf_arg(dst_ip_mask), mask.format.dip.u32Ip);  \
}

#define set_src_ip_port(rule, mask, format, src_ip_port, src_ip_port_mask) { \
  rule.format.commonExt.l4Byte0 = nth_byte(1, src_ip_port);                  \
  rule.format.commonExt.l4Byte1 = nth_byte(0, src_ip_port);                  \
  mask.format.commonExt.l4Byte0 = nth_byte(1, src_ip_port_mask);             \
  mask.format.commonExt.l4Byte1 = nth_byte(0, src_ip_port_mask);             \
  DEBUG("%s: src_ip_port: 0x%X, l4Byte0: 0x%X, l4Byte1: 0x%X\r\n",           \
        __FUNCTION__, src_ip_port, rule.format.commonExt.l4Byte0,            \
        rule.format.commonExt.l4Byte1);                                      \
  DEBUG("%s: src_ip_port_mask: 0x%X, l4Byte0: 0x%X, l4Byte1: 0x%X\r\n",      \
        __FUNCTION__, src_ip_port_mask, mask.format.commonExt.l4Byte0,       \
        mask.format.commonExt.l4Byte1);                                      \
}

#define set_dst_ip_port(rule, mask, format, dst_ip_port, dst_ip_port_mask) { \
  rule.format.commonExt.l4Byte2 = nth_byte(1, dst_ip_port);                  \
  rule.format.commonExt.l4Byte3 = nth_byte(0, dst_ip_port);                  \
  mask.format.commonExt.l4Byte2 = nth_byte(1, dst_ip_port_mask);             \
  mask.format.commonExt.l4Byte3 = nth_byte(0, dst_ip_port_mask);             \
  DEBUG("%s: dst_ip_port: 0x%X, l4Byte2: 0x%X, l4Byte3: 0x%X\r\n",           \
        __FUNCTION__, dst_ip_port, rule.format.commonExt.l4Byte2,            \
        rule.format.commonExt.l4Byte3);                                      \
  DEBUG("%s: dst_ip_port_mask: 0x%X, l4Byte2: 0x%X, l4Byte3: 0x%X\r\n",      \
        __FUNCTION__, dst_ip_port_mask, mask.format.commonExt.l4Byte2,       \
        mask.format.commonExt.l4Byte3);                                      \
}

#define set_dscp(rule, mask, format, dscp_val, dscp_mask) {             \
  rule.format.commonExt.dscp = dscp_val;                                \
  mask.format.commonExt.dscp = dscp_mask;                               \
  DEBUG("%s: dscp: 0x%X, dscp: 0x%X\r\n", __FUNCTION__, dscp_val,       \
        rule.format.commonExt.dscp);                                    \
  DEBUG("%s: dscp_mask: 0x%X, dscp: 0x%X\r\n", __FUNCTION__, dscp_mask, \
        mask.format.commonExt.dscp);                                    \
}

#define set_icmp_type(rule, mask, format, icmp_type, icmp_type_mask) {     \
  rule.format.commonExt.l4Byte0 = icmp_type;                               \
  mask.format.commonExt.l4Byte0 = icmp_type_mask;                          \
  DEBUG("%s: icmp_type: 0x%X, l4Byte0: 0x%X\r\n", __FUNCTION__, icmp_type, \
        rule.format.commonExt.l4Byte0);                                    \
  DEBUG("%s: icmp_type_mask: 0x%X, l4Byte0: 0x%X\r\n", __FUNCTION__,       \
        icmp_type_mask, mask.format.commonExt.l4Byte0);                    \
}

#define set_icmp_code(rule, mask, format, icmp_code, icmp_code_mask) {     \
  rule.format.commonExt.l4Byte1 = icmp_code;                               \
  mask.format.commonExt.l4Byte1 = icmp_code_mask;                          \
  DEBUG("%s: icmp_code: 0x%X, l4Byte1: 0x%X\r\n", __FUNCTION__, icmp_code, \
        rule.format.commonExt.l4Byte1);                                    \
  DEBUG("%s: icmp_code_mask: 0x%X, l4Byte1: 0x%X\r\n", __FUNCTION__,       \
        icmp_code_mask, mask.format.commonExt.l4Byte1);                    \
}

#define set_igmp_type(rule, mask, format, igmp_type, igmp_type_mask) {     \
  rule.format.commonExt.l4Byte0 = igmp_type;                               \
  mask.format.commonExt.l4Byte0 = igmp_type_mask;                          \
  DEBUG("%s: igmp_type: 0x%X, l4Byte0: 0x%X\r\n", __FUNCTION__, igmp_type, \
        rule.format.commonExt.l4Byte0);                                    \
  DEBUG("%s: igmp_type_mask: 0x%X, l4Byte0: 0x%X\r\n", __FUNCTION__,       \
        igmp_type_mask, mask.format.commonExt.l4Byte0);                    \
}

#define set_tcp_flags(rule, mask, format, tcp_flags, tcp_flags_mask) {      \
  rule.format.commonExt.l4Byte13 = tcp_flags;                               \
  mask.format.commonExt.l4Byte13 = tcp_flags_mask;                          \
  DEBUG("%s: tcp_flags: 0x%X, l4Byte13: 0x%X\r\n", __FUNCTION__, tcp_flags, \
        rule.format.commonExt.l4Byte13);                                    \
  DEBUG("%s: tcp_flags_mask: 0x%X, l4Byte13: 0x%X\r\n", __FUNCTION__,       \
        tcp_flags_mask, mask.format.commonExt.l4Byte13);                    \
}

#define activate_rule(dev, type, rule_ix, rule_opt_bmp, mask, rule, act) {     \
  DEBUG("%s: activate_rule (%d, %s, %d, ...)\r\n",                             \
    __FUNCTION__, dev, #type, rule_ix);                                        \
  int status =                                                                 \
   CRP(cpssDxChPclRuleSet(dev, type, rule_ix, rule_opt_bmp, mask, rule, act)); \
  if (status != GT_OK) {                                                       \
    DEBUG("%s: %s: failed, function returns\r\n", __FUNCTION__,                \
          "cpssDxChPclRuleSet");                                               \
  } else {                                                                     \
    DEBUG("%s: ok\r\n", __FUNCTION__);                                         \
  }                                                                            \
}

#define set_ip_rule(dev, rule, mask, format, type, pcl_id, act, ip_rule) {   \
  set_pcl_id (rule, mask, format, pcl_id);                                   \
  set_packet_type_ip (rule, mask, format);                                   \
  set_ip_protocol (rule, mask, format, ip_rule->proto);                      \
  set_src_ip (rule, mask, format, ip_rule->src_ip, ip_rule->src_ip_mask);    \
  set_dst_ip (rule, mask, format, ip_rule->dst_ip, ip_rule->dst_ip_mask);    \
  if (is_tcp_or_udp(ip_rule->proto)) {                                       \
    set_src_ip_port (rule, mask, format, ip_rule->src_ip_port,               \
                     ip_rule->src_ip_port_mask);                             \
    set_dst_ip_port (rule, mask, format, ip_rule->dst_ip_port,               \
                     ip_rule->dst_ip_port_mask);                             \
  }                                                                          \
  set_dscp (rule, mask, format, ip_rule->dscp, ip_rule->dscp_mask);          \
  if (ip_rule->proto == 0x01 /* ICMP */) {                                   \
    set_icmp_type (rule, mask, format, ip_rule->icmp_type,                   \
                   ip_rule->icmp_type_mask);                                 \
    set_icmp_code (rule, mask, format, ip_rule->icmp_code,                   \
                   ip_rule->icmp_code_mask);                                 \
  } else if (ip_rule->proto == 0x02 /* IGMP */) {                            \
    set_igmp_type (rule, mask, format, ip_rule->igmp_type,                   \
                   ip_rule->igmp_type_mask);                                 \
  } else if (ip_rule->proto == 0x06 /* TCP */) {                             \
    set_tcp_flags (rule, mask, format, ip_rule->tcp_flags,                   \
                   ip_rule->tcp_flags_mask);                                 \
  }                                                                          \
  activate_rule (dev, type, ip_rule->rule_ix, 0, &mask, &rule, &act);        \
}

#define set_src_mac(rule, mask, format, src_mac, src_mac_mask) {       \
  memcpy (&rule.format.macSa, &src_mac, sizeof(GT_ETHERADDR));         \
  memcpy (&mask.format.macSa, &src_mac_mask, sizeof(GT_ETHERADDR));    \
  DEBUG("%s: src_mac: "mac_addr_fmt", macSa: "mac_addr_fmt"\r\n",      \
        __FUNCTION__, mac_addr_to_printf_arg(src_mac),                 \
        mac_addr_to_printf_arg(rule.format.macSa));                    \
  DEBUG("%s: src_mac_mask: "mac_addr_fmt", macSa: "mac_addr_fmt"\r\n", \
        __FUNCTION__, mac_addr_to_printf_arg(src_mac_mask),            \
        mac_addr_to_printf_arg(mask.format.macSa));                    \
}

#define set_dst_mac(rule, mask, format, dst_mac, dst_mac_mask) {       \
  memcpy (&rule.format.macDa, &dst_mac, sizeof(GT_ETHERADDR));         \
  memcpy (&mask.format.macDa, &dst_mac_mask, sizeof(GT_ETHERADDR));    \
  DEBUG("%s: dst_mac: "mac_addr_fmt", macDa: "mac_addr_fmt"\r\n",      \
        __FUNCTION__, mac_addr_to_printf_arg(dst_mac),                 \
        mac_addr_to_printf_arg(rule.format.macDa));                    \
  DEBUG("%s: dst_mac_mask: "mac_addr_fmt", macDa: "mac_addr_fmt"\r\n", \
        __FUNCTION__, mac_addr_to_printf_arg(dst_mac_mask),            \
        mac_addr_to_printf_arg(mask.format.macDa));                    \
}

#define set_eth_type(rule, mask, format, eth_type, eth_type_mask) {   \
  rule.format.etherType = eth_type;                                   \
  mask.format.etherType = eth_type_mask;                              \
  DEBUG("%s: eth_type: 0x%X, etherType: 0x%X\r\n", __FUNCTION__,      \
        eth_type, rule.format.etherType);                             \
  DEBUG("%s: eth_type_mask: 0x%X, etherType: 0x%X\r\n", __FUNCTION__, \
        eth_type_mask, mask.format.etherType);                        \
}

#define set_vid(rule, mask, format, vlan, vlan_mask) {             \
  rule.format.common.vid = vlan;                                   \
  mask.format.common.vid = vlan_mask;                              \
  DEBUG("%s: vlan: %d, common.vid: 0x%X\r\n", __FUNCTION__,        \
        vlan, rule.format.common.vid);                             \
  DEBUG("%s: vlan_mask: 0x%X, common.vid: 0x%X\r\n", __FUNCTION__, \
        vlan_mask, mask.format.common.vid);                        \
}

#define set_cos(rule, mask, format, cos, cos_mask) {             \
  rule.format.common.up = cos;                                   \
  mask.format.common.up = cos_mask;                              \
  DEBUG("%s: cos: %d, common.up: 0x%X\r\n", __FUNCTION__,        \
        cos, rule.format.common.up);                             \
  DEBUG("%s: cos_mask: 0x%X, common.up: 0x%X\r\n", __FUNCTION__, \
        cos_mask, mask.format.common.up);                        \
}

#define set_mac_rule(dev, rule, mask, format, type, pcl_id, act, mac_rule) {   \
  set_pcl_id (rule, mask, format, pcl_id);                                     \
  set_packet_type_mac (rule, mask, format);                                    \
  set_src_mac (rule, mask, format, mac_rule->src_mac, mac_rule->src_mac_mask); \
  set_dst_mac (rule, mask, format, mac_rule->dst_mac, mac_rule->dst_mac_mask); \
  set_eth_type (rule,mask,format,mac_rule->eth_type,mac_rule->eth_type_mask);  \
  set_vid (rule, mask, format, mac_rule->vid, mac_rule->vid_mask);             \
  set_cos (rule, mask, format, mac_rule->cos, mac_rule->cos_mask);             \
  activate_rule (dev, type, mac_rule->rule_ix, 0, &mask, &rule, &act);         \
}

#define set_src_ipv6(rule, mask, format, src_ip, src_ip_mask) {                \
  memcpy (&rule.format.sip, &src_ip, sizeof(GT_IPV6ADDR));                     \
  memcpy (&mask.format.sip, &src_ip_mask, sizeof(GT_IPV6ADDR));                \
  DEBUG("%s: src_ip: "ipv6_addr_fmt", sip: "ipv6_addr_fmt"\r\n",               \
        __FUNCTION__, ipv6_addr_to_printf_arg(src_ip),                         \
        ipv6_addr_to_printf_arg(rule.format.sip));                             \
  DEBUG("%s: src_ip_mask: "ipv6_addr_fmt", sip: "ipv6_addr_fmt"\r\n",          \
        __FUNCTION__, ipv6_addr_to_printf_arg(src_ip_mask),                    \
        ipv6_addr_to_printf_arg(mask.format.sip));                       \
}

#define set_dst_ipv6(rule, mask, format, dst_ip, dst_ip_mask) {                \
  memcpy (&rule.format.dip, &dst_ip, sizeof(GT_IPV6ADDR));                     \
  memcpy (&mask.format.dip, &dst_ip_mask, sizeof(GT_IPV6ADDR));                \
  DEBUG("%s: dst_ip: "ipv6_addr_fmt", dip: "ipv6_addr_fmt"\r\n",               \
        __FUNCTION__, ipv6_addr_to_printf_arg(dst_ip),                         \
        ipv6_addr_to_printf_arg(rule.format.dip));                             \
  DEBUG("%s: dst_ip_mask: "ipv6_addr_fmt", dip: "ipv6_addr_fmt"\r\n",          \
        __FUNCTION__, ipv6_addr_to_printf_arg(dst_ip_mask),                    \
        ipv6_addr_to_printf_arg(mask.format.dip));                       \
}

#define set_ipv6_rule(dev, rule, mask, format, type, pcl_id, act, ipv6_rule) { \
  set_pcl_id (rule, mask, format, pcl_id);                                     \
  set_packet_type_ipv6 (rule, mask, format);                                   \
  set_src_ipv6 (rule, mask, format, ipv6_rule->src, ipv6_rule->src_mask);      \
  set_dst_ipv6 (rule, mask, format, ipv6_rule->dst, ipv6_rule->dst_mask);      \
  if (is_tcp_or_udp(ipv6_rule->proto)) {                                       \
    set_src_ip_port (rule, mask, format, ipv6_rule->src_ip_port,               \
                     ipv6_rule->src_ip_port_mask);                             \
    set_dst_ip_port (rule, mask, format, ipv6_rule->dst_ip_port,               \
                     ipv6_rule->dst_ip_port_mask);                             \
  }                                                                            \
  set_dscp (rule, mask, format, ipv6_rule->dscp, ipv6_rule->dscp_mask);        \
  if (ipv6_rule->proto == 0x01 /* ICMP */) {                                   \
    set_icmp_type (rule, mask, format, ipv6_rule->icmp_type,                   \
                   ipv6_rule->icmp_type_mask);                                 \
    set_icmp_code (rule, mask, format, ipv6_rule->icmp_code,                   \
                   ipv6_rule->icmp_code_mask);                                 \
  } else if (ipv6_rule->proto == 0x06 /* TCP */) {                             \
    set_tcp_flags (rule, mask, format, ipv6_rule->tcp_flags,                   \
                   ipv6_rule->tcp_flags_mask);                                 \
  }                                                                            \
  activate_rule (dev, type, ipv6_rule->rule_ix, 0, &mask, &rule, &act);        \
}

const char* pcl_trap_action_to_str (enum PCL_TRAP_ACTION action) {
  switch (action) {
    case PCL_TRAP_ACTION_LOG_INPUT:
      return "PCL_TRAP_ACTION_LOG_INPUT";
    case PCL_TRAP_ACTION_DISABLE_PORT:
      return "PCL_TRAP_ACTION_DISABLE_PORT";
    case PCL_TRAP_ACTION_NONE:
      return "PCL_TRAP_ACTION_NONE";
    default:
      return "Unknown";
  }
};

void
pcl_ip_rule_set (uint16_t pid_or_vid, struct ip_pcl_rule *ip_rule,
                 enum PCL_DESTINATION destination, int enable) {
  PRINT_SEPARATOR('=', 100);
  DEBUG(
    "%s: %s: %d, enable: %s, destination: %s\r\n",
    __FUNCTION__,
    pid_or_vid > 10000 ? "vlan" : "port",
    pid_or_vid > 10000 ? pid_or_vid - 10000 : pid_or_vid,
    bool_to_str(enable), pcl_dest_to_str(destination)
  );

  struct port *port;
  get_port_ptr (port, pid_or_vid);

  if (!ip_rule) {
    DEBUG("%s: ip_rule: invalid pointer (NULL), function returns\r\n",
          __FUNCTION__);
    goto out;
  } else {
    DEBUG("%s: rule_ix: %d\r\n", __FUNCTION__, ip_rule->rule_ix);
  }

  if (!enable) {
    /* Magic */
    free_user_rule_ix(pid_or_vid, ip_rule->rule_ix);
    if ( pid_or_vid < 10000 ) {
      inactivate_rule (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, ip_rule->rule_ix);
    } else {
      int d;
      for_each_dev(d) {
        inactivate_rule (
          d,
          CPSS_PCL_RULE_SIZE_EXT_E,
          (ip_rule->rule_ix & (0xffff << d*16)) >> (d*16)
        );
      }
    }
  }

  CPSS_DXCH_PCL_ACTION_STC act;
  if (!set_pcl_action (pid_or_vid, ip_rule->rule_ix, ip_rule->action, ip_rule->trap_action, &act)) {
    goto out;
  }

  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  memset (&rule, 0, sizeof(rule));
  memset (&mask, 0, sizeof(mask));

  switch (destination) {
    case PCL_DESTINATION_INGRESS:
      if ( pid_or_vid < 10000 ) {
        set_ip_rule (port->ldev, rule, mask, ruleExtNotIpv6,
                     CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
                     PORT_IPCL_ID(pid_or_vid), act, ip_rule);
      } else {
        int d;
        uint32_t rule_ix = ip_rule->rule_ix;
        for_each_dev(d) {
          ip_rule->rule_ix = ((rule_ix & (0xffff << d*16)) >> (d*16));
          set_ip_rule (d, rule, mask, ruleExtNotIpv6,
                       CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
                       VLAN_IPCL_ID(pid_or_vid - 10000), act, ip_rule);
        }
      }
      break;

    case PCL_DESTINATION_EGRESS:
      if ( pid_or_vid < 10000 ) {
        set_ip_rule (port->ldev, rule, mask, ruleEgrExtNotIpv6,
                     CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
                     PORT_EPCL_ID(pid_or_vid), act, ip_rule);
      } else {
        DEBUG("%s: EGRESS destination on VLAN ACL, function returns\r\n",
          __FUNCTION__);
      }
      break;

    default:
      DEBUG("%s: destination: %d - unknown destination, function returns\r\n",
            __FUNCTION__, destination);
  };

  DEBUG("out\r\n");
out:
  PRINT_SEPARATOR('=', 100);
}

void
pcl_mac_rule_set (uint16_t pid_or_vid, struct mac_pcl_rule *mac_rule,
                  enum PCL_DESTINATION destination, int enable) {
  PRINT_SEPARATOR('=', 100);
  DEBUG(
    "%s: %s: %d, enable: %s, destination: %s\r\n",
    __FUNCTION__,
    pid_or_vid > 10000 ? "vlan" : "port",
    pid_or_vid > 10000 ? pid_or_vid - 10000 : pid_or_vid,
    bool_to_str(enable), pcl_dest_to_str(destination)
  );

  struct port *port;
  get_port_ptr (port, pid_or_vid);

  if (!mac_rule) {
    DEBUG("%s: mac_rule: invalid pointer (NULL), function returns\r\n",
          __FUNCTION__);
    goto out;
  } else {
    DEBUG("%s: rule_ix: %d\r\n", __FUNCTION__, mac_rule->rule_ix);
  }

  if (!enable) {
    /* Magic */
    free_user_rule_ix(pid_or_vid, mac_rule->rule_ix);
    if ( pid_or_vid < 10000 ) {
      inactivate_rule (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, mac_rule->rule_ix);
    } else {
      int d;
      for_each_dev(d) {
        inactivate_rule (
          d,
          CPSS_PCL_RULE_SIZE_EXT_E,
          (mac_rule->rule_ix & (0xffff << d*16)) >> (d*16)
        );
      }
    }
  }

  CPSS_DXCH_PCL_ACTION_STC act;
  if (!set_pcl_action (pid_or_vid, mac_rule->rule_ix, mac_rule->action, mac_rule->trap_action, &act)) {
    goto out;
  }

  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  memset (&rule, 0, sizeof(rule));
  memset (&mask, 0, sizeof(mask));

  switch (destination) {
    case PCL_DESTINATION_INGRESS:
      if ( pid_or_vid < 10000 ) {
        set_mac_rule (port->ldev, rule, mask, ruleExtNotIpv6,
                     CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
                     PORT_IPCL_ID(pid_or_vid), act, mac_rule);
      } else {
        int d;
        uint32_t rule_ix = mac_rule->rule_ix;
        for_each_dev(d) {
          mac_rule->rule_ix = ((rule_ix & (0xffff << d*16)) >> (d*16));
          set_mac_rule (d, rule, mask, ruleExtNotIpv6,
                       CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
                       VLAN_IPCL_ID(pid_or_vid - 10000), act, mac_rule);
        }
      }
      break;

    case PCL_DESTINATION_EGRESS:
      if ( pid_or_vid < 10000 ) {
        set_mac_rule (port->ldev, rule, mask, ruleEgrExtNotIpv6,
                     CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
                     PORT_EPCL_ID(pid_or_vid), act, mac_rule);
      } else {
        DEBUG("%s: EGRESS destination on VLAN ACL, function returns\r\n",
          __FUNCTION__);
      }
      break;

    default:
      DEBUG("%s: destination: %d - unknown destination, function returns\r\n",
            __FUNCTION__, destination);
  };

  DEBUG("out\r\n");
out:
  PRINT_SEPARATOR('=', 100);
}

void
pcl_ipv6_rule_set (uint16_t pid_or_vid, struct ipv6_pcl_rule *ipv6_rule,
                   enum PCL_DESTINATION destination, int enable) {
  PRINT_SEPARATOR('=', 100);
  DEBUG(
    "%s: %s: %d, enable: %s, destination: %s\r\n",
    __FUNCTION__,
    pid_or_vid > 10000 ? "vlan" : "port",
    pid_or_vid > 10000 ? pid_or_vid - 10000 : pid_or_vid,
    bool_to_str(enable), pcl_dest_to_str(destination)
  );

  struct port *port;
  get_port_ptr (port, pid_or_vid);

  if (!ipv6_rule) {
    DEBUG("%s: ipv6_rule: invalid pointer (NULL), function returns\r\n",
          __FUNCTION__);
    goto out;
  } else {
    DEBUG("%s: rule_ix: %d\r\n", __FUNCTION__, ipv6_rule->rule_ix);
  }

  if (!enable) {
    /* Magic */
    free_user_rule_ix(pid_or_vid, ipv6_rule->rule_ix);
    if ( pid_or_vid < 10000 ) {
      inactivate_rule (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, ipv6_rule->rule_ix);
    } else {
      int d;
      for_each_dev(d) {
        inactivate_rule (
          d,
          CPSS_PCL_RULE_SIZE_EXT_E,
          (ipv6_rule->rule_ix & (0xffff << d*16)) >> (d*16)
        );
      }
    }
  }

  CPSS_DXCH_PCL_ACTION_STC act;
  if (!set_pcl_action (pid_or_vid, ipv6_rule->rule_ix, ipv6_rule->action, ipv6_rule->trap_action, &act)) {
    goto out;
  }

  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  memset (&rule, 0, sizeof(rule));
  memset (&mask, 0, sizeof(mask));

  switch (destination) {
    case PCL_DESTINATION_INGRESS:
      if ( pid_or_vid < 10000 ) {
        set_ipv6_rule (port->ldev, rule, mask, ruleExtNotIpv6,
                     CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L4_E,
                     PORT_IPCL_ID(pid_or_vid), act, ipv6_rule);
      } else {
        int d;
        uint32_t rule_ix = ipv6_rule->rule_ix;
        for_each_dev(d) {
          ipv6_rule->rule_ix = ((rule_ix & (0xffff << d*16)) >> (d*16));
          set_ipv6_rule (d, rule, mask, ruleExtNotIpv6,
                       CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L4_E,
                       VLAN_IPCL_ID(pid_or_vid - 10000), act, ipv6_rule);
        }
      }
      break;

    case PCL_DESTINATION_EGRESS:
      if ( pid_or_vid < 10000 ) {
        set_ipv6_rule (port->ldev, rule, mask, ruleEgrExtNotIpv6,
                     CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_IPV6_L4_E,
                     PORT_EPCL_ID(pid_or_vid), act, ipv6_rule);
      } else {
        DEBUG("%s: EGRESS destination on VLAN ACL, function returns\r\n",
          __FUNCTION__);
      }
      break;

    default:
      DEBUG("%s: destination: %d - unknown destination, function returns\r\n",
            __FUNCTION__, destination);
  };

  DEBUG("out\r\n");
out:
  PRINT_SEPARATOR('=', 100);
}

void
pcl_default_rule_set (uint16_t pid_or_vid, struct default_pcl_rule *default_rule,
                      enum PCL_DESTINATION destination, int enable) {
  PRINT_SEPARATOR('=', 100);
  DEBUG(
    "%s: %s: %d, enable: %s, destination: %s\r\n",
    __FUNCTION__,
    pid_or_vid > 10000 ? "vlan" : "port",
    pid_or_vid > 10000 ? pid_or_vid - 10000 : pid_or_vid,
    bool_to_str(enable), pcl_dest_to_str(destination)
  );

  struct port *port;
  get_port_ptr (port, pid_or_vid);

  if (!default_rule) {
    DEBUG("%s: default_rule: invalid pointer (NULL), function returns\r\n",
          __FUNCTION__);
    goto out;
  } else {
    DEBUG("%s: rule_ix: %d\r\n", __FUNCTION__, default_rule->rule_ix);
  }

  if (!enable) {
    /* Magic */
    free_user_rule_ix(pid_or_vid, default_rule->rule_ix);
    if ( pid_or_vid < 10000 ) {
      inactivate_rule (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, default_rule->rule_ix);
    } else {
      int d;
      for_each_dev(d) {
        inactivate_rule (
          d,
          CPSS_PCL_RULE_SIZE_EXT_E,
          (default_rule->rule_ix & (0xffff << d*16)) >> (d*16)
        );
      }
    }
  }

  CPSS_DXCH_PCL_ACTION_STC act;
  if (!set_pcl_action (pid_or_vid, default_rule->rule_ix, default_rule->action, PCL_TRAP_ACTION_NONE, &act)) {
    goto out;
  }

  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  memset (&rule, 0, sizeof(rule));
  memset (&mask, 0, sizeof(mask));

  switch (destination) {
    case PCL_DESTINATION_INGRESS:
      if ( pid_or_vid < 10000 ) {
        set_pcl_id (rule, mask, ruleExtNotIpv6, PORT_IPCL_ID(pid_or_vid));
        activate_rule (
          port->ldev, CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
          default_rule->rule_ix, 0, &mask, &rule, &act
        );
      } else {

        int d;
        uint32_t rule_ix = default_rule->rule_ix;
        set_pcl_id (rule, mask, ruleExtNotIpv6, VLAN_IPCL_ID(pid_or_vid - 10000));
        for_each_dev(d) {
          rule_ix = ((rule_ix & (0xffff << d*16)) >> (d*16));
          activate_rule (
            d, CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
            rule_ix, 0, &mask, &rule, &act
          );
        }
      }
      break;

    case PCL_DESTINATION_EGRESS:
      if ( pid_or_vid < 10000 ) {
        set_pcl_id (rule, mask, ruleExtNotIpv6, PORT_EPCL_ID(pid_or_vid));
        activate_rule (
          port->ldev, CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
          default_rule->rule_ix, 0, &mask, &rule, &act
        );
      } else {
        DEBUG("%s: EGRESS destination on VLAN ACL, function returns\r\n",
          __FUNCTION__);
      }
      break;

    default:
      DEBUG("%s: destination: %d - unknown destination, function returns\r\n",
            __FUNCTION__, destination);
  };

  DEBUG("out\r\n");
out:
  PRINT_SEPARATOR('=', 100);
}

static void __attribute__ ((unused))
pcl_ip_rule_diff (struct ip_pcl_rule* a, struct ip_pcl_rule* b) {
  PRINT_SEPARATOR('=', 100);
  DEBUG("Diff start.");

  #ifndef diff
    #define diff(field)                                                 \
    {                                                                   \
      if ( memcmp ( &( a->field ), &( b->field ), sizeof( a->field )) ) \
        DEBUG("  %s diffs\r\n", #field);                                \
    }

    diff (rule_ix);
    diff (action);
    diff (proto);
    diff (src_ip);
    diff (src_ip_mask);
    diff (src_ip_port);
    diff (src_ip_port_mask);
    diff (dst_ip);
    diff (dst_ip_mask);
    diff (dst_ip_port);
    diff (dst_ip_port_mask);
    diff (dscp);
    diff (dscp_mask);
    diff (icmp_type);
    diff (icmp_type_mask);
    diff (icmp_code);
    diff (icmp_code_mask);
    diff (igmp_type);
    diff (igmp_type_mask);
    diff (tcp_flags);
    diff (tcp_flags_mask);

    #undef diff
  #else

    DEBUG("  diff() already defined: cannot diff by fields\r\n");
    if (memcmp(a, b, sizeof(struct ip_pcl_rule)))
      DEBUG("  Diff result: not equal.\r\n");
    else
      DEBUG("  Diff result: equal.\r\n");

  #endif

  DEBUG("Diff end.");
  PRINT_SEPARATOR('=', 100);
}

/******************************************************************************/
/* MULTICAST DROP                                                             */
/******************************************************************************/

static void
pcl_setup_mc_drop (int d)
{
  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;

  memset (&act, 0, sizeof (act));
  act.pktCmd = CPSS_PACKET_CMD_DROP_HARD_E;
  act.actionStop = GT_TRUE;

  memset (&mask, 0, sizeof (mask));
  mask.ruleEgrExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleEgrExtNotIpv6.common.isL2Valid = 0xFF;
  mask.ruleEgrExtNotIpv6.macDa.arEther[0] = 0x01;

  memset (&rule, 0, sizeof (rule));
  rule.ruleEgrExtNotIpv6.common.pclId = PORT_EPCL_ID (stack_sec_port->id);
  rule.ruleEgrExtNotIpv6.common.isL2Valid = 1;
  rule.ruleEgrExtNotIpv6.macDa.arEther[0] = 0x01;

  DEBUG("Pid: %d ix: %d PCLid: %d\r\n", stack_sec_port->id, 1, PORT_EPCL_ID (stack_sec_port->id));
  CRP (cpssDxChPclRuleSet
       (d,
        CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
        PORT_STACK_SECONDARY_IPv4_MC_DROP_E_IX,
        0,
        &mask,
        &rule,
        &act));

  memset (&mask, 0, sizeof (mask));
  mask.ruleEgrExtIpv6L2.common.pclId = 0xFFFF;
  mask.ruleEgrExtIpv6L2.common.isL2Valid = 0xFF;
  mask.ruleEgrExtIpv6L2.macDa.arEther[0] = 0x01;

  memset (&rule, 0, sizeof (rule));
  rule.ruleEgrExtIpv6L2.common.pclId = PORT_EPCL_ID (stack_sec_port->id);
  rule.ruleEgrExtIpv6L2.common.isL2Valid = 1;
  rule.ruleEgrExtIpv6L2.macDa.arEther[0] = 0x01;

  DEBUG("Pid: %d ix: %d PCLid: %d\r\n", stack_sec_port->id, 2, PORT_EPCL_ID (stack_sec_port->id));
  CRP (cpssDxChPclRuleSet
       (d,
        CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_IPV6_L2_E,
        PORT_STACK_SECONDARY_IPv6_MC_DROP_E_IX,
        0,
        &mask,
        &rule,
        &act));

  memset (&mask, 0, sizeof (mask));
  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
  mask.ruleExtNotIpv6.macDa.arEther[0] = 0x01;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (stack_sec_port->id);
  rule.ruleExtNotIpv6.common.isL2Valid = 1;
  rule.ruleExtNotIpv6.macDa.arEther[0] = 0x01;

  DEBUG("Pid: %d ix: %d PCLid: %d\r\n", stack_sec_port->id, 3, PORT_IPCL_ID (stack_sec_port->id));
  CRP (cpssDxChPclRuleSet
       (d,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
        PORT_STACK_SECONDARY_IPv4_MC_DROP_I_IX,
        0,
        &mask,
        &rule,
        &act));

  memset (&mask, 0, sizeof (mask));
  mask.ruleExtIpv6L2.common.pclId = 0xFFFF;
  mask.ruleExtIpv6L2.common.isL2Valid = 0xFF;
  mask.ruleExtIpv6L2.macDa.arEther[0] = 0x01;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtIpv6L2.common.pclId = PORT_IPCL_ID (stack_sec_port->id);
  rule.ruleExtIpv6L2.common.isL2Valid = 1;
  rule.ruleExtIpv6L2.macDa.arEther[0] = 0x01;

  DEBUG("Pid: %d ix: %d PCLid: %d\r\n", stack_sec_port->id, 4, PORT_IPCL_ID (stack_sec_port->id));
  CRP (cpssDxChPclRuleSet
       (d,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L2_E,
        PORT_STACK_SECONDARY_IPv4_MC_DROP_I_IX,
        0,
        &mask,
        &rule,
        &act));
}

enum status
pcl_enable_mc_drop (port_id_t pid, int enable)
{
  struct port *port = port_ptr (pid);
  assert(port == stack_sec_port);
  CPSS_INTERFACE_INFO_STC iface = {
    .type    = CPSS_INTERFACE_PORT_E,
    .devPort = {
      .devNum  = phys_dev (port->ldev),
      .portNum = port->lport
    }
  };
  CPSS_DXCH_PCL_LOOKUP_CFG_STC elc = {
    .enableLookup  = gt_bool (enable),
    .pclId         = PORT_EPCL_ID (port->id),
    .dualLookup    = GT_FALSE,
    .pclIdL01      = 0,
    .groupKeyTypes = {
      .nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
      .ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
      .ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_IPV6_L2_E
    }
  };
  CPSS_DXCH_PCL_LOOKUP_CFG_STC ilc = {
    .enableLookup  = gt_bool (enable),
    .pclId         = PORT_IPCL_ID (port->id),
    .dualLookup    = GT_FALSE,
    .pclIdL01      = 0,
    .groupKeyTypes = {
      .nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L2_E
    }
  };

  DEBUG ("%s mc drop\r\n", enable ? "enabling" : "disabling");

  CRP (cpssDxChPclCfgTblSet
       (port->ldev, &iface,
        CPSS_PCL_DIRECTION_EGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        &elc));
  CRP (cpssDxChPclCfgTblSet
       (port->ldev, &iface,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        &ilc));

  return ST_OK;
}

/******************************************************************************/
/* ARP non BC non REPLY_TO_ME TRAP                                            */
/******************************************************************************/

enum status
pcl_enable_arp_trap (int enable) {

  port_id_t pi;
  for (pi = 1; pi <= nports ; pi++) {
      struct port *port = port_ptr (pi);

      if (is_stack_port(port))
        continue;

    if (enable) {
      CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
      CPSS_DXCH_PCL_ACTION_STC act;

      memset (&mask, 0, sizeof (mask));
      mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
      mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
      mask.ruleExtNotIpv6.common.isIp = 0xFF;
      mask.ruleExtNotIpv6.l2Encap = 0xFF;
      mask.ruleExtNotIpv6.etherType  = 0xFFFF;

      memset (&rule, 0, sizeof (rule));
      rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pi);
      rule.ruleExtNotIpv6.common.isL2Valid = 1;
      rule.ruleExtNotIpv6.common.isIp = 0;
      rule.ruleExtNotIpv6.l2Encap = 1;
      rule.ruleExtNotIpv6.etherType  = 0x0806;

      memset (&act, 0, sizeof (act));
      act.pktCmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
      act.actionStop = GT_TRUE;
      act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E + 3;

      CRP (cpssDxChPclRuleSet
           (port->ldev,
            CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
            PORT_ARP_INSPECTOR_TRAP_IX (pi),
            0,
            &mask,
            &rule,
            &act));

    } else
        CRP (cpssDxChPclRuleInvalidate
             (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, PORT_ARP_INSPECTOR_TRAP_IX (pi)));

  }
  return ST_OK;
}


enum status
pcl_enable_lbd_trap (port_id_t pid, int enable)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (enable) {
    CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
    CPSS_DXCH_PCL_ACTION_STC act;

    memset (&mask, 0, sizeof (mask));
    mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
    mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
    mask.ruleExtNotIpv6.etherType = 0xFFFF;
    mask.ruleExtNotIpv6.l2Encap = 0xFF;

    memset (&rule, 0, sizeof (rule));
    rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pid);
    rule.ruleExtNotIpv6.common.isL2Valid = 1;
    rule.ruleExtNotIpv6.etherType = 0x88B7;
    rule.ruleExtNotIpv6.l2Encap = 1;

    memset (&act, 0, sizeof (act));
    act.pktCmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    act.actionStop = GT_TRUE;
    act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E;

    DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pid, PORT_LBD_RULE_IX (pid), PORT_IPCL_ID (pid));
    CRP (cpssDxChPclRuleSet
         (port->ldev,
          CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
          PORT_LBD_RULE_IX (pid),
          0,
          &mask,
          &rule,
          &act));
  } else
      CRP (cpssDxChPclRuleInvalidate
           (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, PORT_LBD_RULE_IX (pid)));

  return ST_OK;
}

enum status
pcl_enable_dhcp_trap (int enable)
{
  port_id_t pi;

  if (enable) {
    for (pi = 1; pi <= nports ; pi++) {
      struct port *port = port_ptr (pi);

      if (is_stack_port(port))
        continue;

      CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
      CPSS_DXCH_PCL_ACTION_STC act;

      memset (&mask, 0, sizeof (mask));

      mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
      mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
      mask.ruleExtNotIpv6.common.isIp = 0xFF;
      mask.ruleExtNotIpv6.l2Encap = 0xFF;

      mask.ruleExtNotIpv6.commonExt.isIpv6  = 0xff;
      mask.ruleExtNotIpv6.commonExt.ipProtocol  = 0xff;
      mask.ruleExtNotIpv6.commonExt.l4Byte2  = 0xff;
      mask.ruleExtNotIpv6.commonExt.l4Byte3  = 0xff;

      memset (&rule, 0, sizeof (rule));
      rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pi);
      rule.ruleExtNotIpv6.common.isL2Valid = 1;
      rule.ruleExtNotIpv6.common.isIp = 1;
      rule.ruleExtNotIpv6.l2Encap = 1;

      rule.ruleExtNotIpv6.commonExt.isIpv6  = 0;
      rule.ruleExtNotIpv6.commonExt.ipProtocol  = 17; /* UDP */
      rule.ruleExtNotIpv6.commonExt.l4Byte2  = 0;
      rule.ruleExtNotIpv6.commonExt.l4Byte3  = 67;


      memset (&act, 0, sizeof (act));
      act.pktCmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
      act.actionStop = GT_TRUE;
      act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E + 1;

      DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pi, PORT_DHCPTRAP_RULE_IX (pi), PORT_IPCL_ID (pi));
      CRP (cpssDxChPclRuleSet
           (port->ldev,
            CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
            PORT_DHCPTRAP_RULE_IX (pi),
            0,
            &mask,
            &rule,
            &act));


      memset (&mask, 0, sizeof (mask));

      mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
      mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
      mask.ruleExtNotIpv6.common.isIp = 0xFF;
      mask.ruleExtNotIpv6.l2Encap = 0xFF;

      mask.ruleExtNotIpv6.commonExt.isIpv6  = 0xff;
      mask.ruleExtNotIpv6.commonExt.ipProtocol  = 0xff;
      mask.ruleExtNotIpv6.commonExt.l4Byte2  = 0xff;
      mask.ruleExtNotIpv6.commonExt.l4Byte3  = 0xff;

      memset (&rule, 0, sizeof (rule));
      rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pi);
      rule.ruleExtNotIpv6.common.isL2Valid = 1;
      rule.ruleExtNotIpv6.common.isIp = 1;
      rule.ruleExtNotIpv6.l2Encap = 1;

      rule.ruleExtNotIpv6.commonExt.isIpv6  = 0;
      rule.ruleExtNotIpv6.commonExt.ipProtocol  = 17; /* UDP */
      rule.ruleExtNotIpv6.commonExt.l4Byte2  = 0;
      rule.ruleExtNotIpv6.commonExt.l4Byte3  = 68;


      memset (&act, 0, sizeof (act));
      act.pktCmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
      act.actionStop = GT_TRUE;
      act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E + 1;

      DEBUG("Pid: %d ix: %d PCLid: %d\r\n", pi, PORT_DHCPTRAP_RULE_IX (pi) + 1, PORT_IPCL_ID (pi));
      CRP (cpssDxChPclRuleSet
           (port->ldev,
            CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
            PORT_DHCPTRAP_RULE_IX (pi) + 1,
            0,
            &mask,
            &rule,
            &act));

    }
  } else {
    for (pi = 1; pi <= nports; pi++) {
      struct port *port = port_ptr (pi);

      if (is_stack_port(port))
        continue;

      CRP (cpssDxChPclRuleInvalidate
           (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, PORT_DHCPTRAP_RULE_IX (pi)));
      CRP (cpssDxChPclRuleInvalidate
           (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, PORT_DHCPTRAP_RULE_IX (pi) + 1));
    }
  }

  return ST_OK;
}

enum status
pcl_port_setup (port_id_t pid)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  /* Enable ingress PCL. */
  CRP (cpssDxChPclPortIngressPolicyEnable
       (port->ldev, port->lport, GT_TRUE));
  CRP (cpssDxChPclPortLookupCfgTabAccessModeSet
       (port->ldev, port->lport,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        0,
        CPSS_DXCH_PCL_PORT_LOOKUP_CFG_TAB_ACC_MODE_BY_PORT_E));
  CRP (cpssDxChPclPortLookupCfgTabAccessModeSet
       (port->ldev, port->lport,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_1_E,
        0,
        CPSS_DXCH_PCL_PORT_LOOKUP_CFG_TAB_ACC_MODE_BY_VLAN_E));

  /* Enable egress PCL. */
  CRP (cpssDxCh2EgressPclPacketTypesSet
       (port->ldev, port->lport, CPSS_DXCH_PCL_EGRESS_PKT_NON_TS_E, GT_TRUE));
  CRP (cpssDxChPclPortLookupCfgTabAccessModeSet
       (port->ldev, port->lport,
        CPSS_PCL_DIRECTION_EGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        0,
        CPSS_DXCH_PCL_PORT_LOOKUP_CFG_TAB_ACC_MODE_BY_PORT_E));

  return ST_OK;
}

enum status
pcl_enable_port (port_id_t pid, int enable)
{
  struct port *port = port_ptr (pid);
  CPSS_INTERFACE_INFO_STC iface = {
    .type    = CPSS_INTERFACE_PORT_E,
    .devPort = {
      .devNum  = phys_dev (port->ldev),
      .portNum = port->lport
    }
  };
  CPSS_DXCH_PCL_LOOKUP_CFG_STC lc = {
    .enableLookup  = gt_bool (enable),
    .pclId         = PORT_IPCL_ID (pid),
    .dualLookup    = GT_FALSE,
    .pclIdL01      = 0,
    .groupKeyTypes = {
      .nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L4_E
    }
  };

  CRP (cpssDxChPclCfgTblSet
       (port->ldev, &iface,
        CPSS_PCL_DIRECTION_INGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        &lc));

  lc.pclId                  = PORT_EPCL_ID (pid);
  lc.groupKeyTypes.nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E;
  lc.groupKeyTypes.ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E;
  lc.groupKeyTypes.ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_IPV6_L4_E;

  CRP (cpssDxChPclCfgTblSet
       (port->ldev, &iface,
        CPSS_PCL_DIRECTION_EGRESS_E,
        CPSS_PCL_LOOKUP_0_E,
        &lc));

  return ST_OK;
}

static void
pcl_vid_enable (uint16_t vid) {
  CPSS_INTERFACE_INFO_STC iface = {
    .type    = CPSS_INTERFACE_VID_E,
    .vlanId  = vid
  };

  CPSS_DXCH_PCL_LOOKUP_CFG_STC lc = {
    .enableLookup  = GT_TRUE,
    .pclId         = VLAN_IPCL_ID(vid),
    .dualLookup    = GT_FALSE,
    .pclIdL01      = 0,
    .groupKeyTypes = {
      .nonIpKey = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv4Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
      .ipv6Key  = CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L4_E
    }
  };

  int d;
  for_each_dev(d) {
    CRP (cpssDxChPclCfgTblSet
         (d, &iface,
          CPSS_PCL_DIRECTION_INGRESS_E,
          CPSS_PCL_LOOKUP_1_E,
          &lc));
  }
}

uint64_t
pcl_get_counter (uint16_t pid_or_vid, uint16_t rule_ix) {
  CPSS_DXCH_CNC_COUNTER_STC counter;
  memset(&counter, 0, sizeof(counter));

  uint8_t  block = pid_or_vid > 10000  ? 1 : 0;
  uint16_t index = rule_ix;

  CRP (cpssDxChCncCounterGet
       (0,
        block,
        index,
        CPSS_DXCH_CNC_COUNTER_FORMAT_MODE_0_E,
        &counter));

  uint64_t ret_value = 0;

  memcpy(&ret_value, &counter.packetCount, sizeof(ret_value));
  DEBUG("%s: Packets: %llu\r\n", __FUNCTION__, ret_value);

  return ret_value;
}

void
pcl_clear_counter (uint16_t pid_or_vid, uint16_t rule_ix) {
  CPSS_DXCH_CNC_COUNTER_STC counter;
  memset(&counter, 0, sizeof(counter));

  uint8_t  block = pid_or_vid > 10000  ? 1 : 0;
  uint16_t index = rule_ix;

  CRP (cpssDxChCncCounterSet
       (0,
        block,
        index,
        CPSS_DXCH_CNC_COUNTER_FORMAT_MODE_0_E,
        &counter));
}

static void
pcl_init_counters (int d) {
  CRP (cpssDxChCncBlockClientEnableSet
       (d,
        0,
        CPSS_DXCH_CNC_CLIENT_INGRESS_PCL_LOOKUP_0_E,
        GT_TRUE
        ));

  CRP (cpssDxChCncBlockClientEnableSet
       (d,
        1,
        CPSS_DXCH_CNC_CLIENT_INGRESS_PCL_LOOKUP_1_E,
        GT_TRUE
        ));

  GT_U64 bmp;

  memset(bmp.l, 0, 8);
  bmp.l[0] |= 1;

  CRP (cpssDxChCncBlockClientRangesSet
       (d,
        0,
        CPSS_DXCH_CNC_CLIENT_INGRESS_PCL_LOOKUP_0_E,
        bmp));

  memset(bmp.l, 0, 8);
  bmp.l[0] |= 1;

  CRP (cpssDxChCncBlockClientRangesSet
       (d,
        1,
        CPSS_DXCH_CNC_CLIENT_INGRESS_PCL_LOOKUP_1_E,
        bmp));

  CRP (cpssDxChCncCounterClearByReadEnableSet
       (d,
        GT_FALSE));
}

enum status
pcl_cpss_lib_init (int d)
{
  CPSS_DXCH_PCL_CFG_TBL_ACCESS_MODE_STC am;

  CRP (cpssDxChPclInit (d));

  /* Enable ingress PCL. */
  CRP (cpssDxChPclIngressPolicyEnable (d, GT_TRUE));

  /* Enable egress PCL. */
  CRP (cpssDxCh2PclEgressPolicyEnable (d, GT_TRUE));

  /* Configure access modes. */
  memset (&am, 0, sizeof (am));
  am.ipclAccMode = CPSS_DXCH_PCL_CFG_TBL_ACCESS_LOCAL_PORT_E;
  am.epclAccMode = CPSS_DXCH_PCL_CFG_TBL_ACCESS_LOCAL_PORT_E;
  CRP (cpssDxChPclCfgTblAccessModeSet (d, &am));

  /* Initialize indexes and etc. */
  initialize_vars();

  int vid;
  for (vid = 1; vid <= 4094; vid++) {
    pcl_vid_enable(vid);
  }

  /* Initialize CNC */
  pcl_init_counters(d);

  /* Initialize stack of VT rules */
  pcl_init_rules();

  /* Initialize stack of User ACL rules */
  user_acl_init_rules();

  if (stack_active())
    pcl_setup_mc_drop (d);

  pcl_setup_ospf(d);

  print_pcl_indexes();

  return ST_OK;
}
