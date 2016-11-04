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
#include <stackd.h>
#include <utils.h>
#include <sysdeps.h>
#include <uthash.h>
#include <debug.h>
#include <zmq.h>
#include <czmq.h>
#include <utlist.h>
#include <dstack.h>

/******************************************************************************/
/* Static variables                                                           */
/******************************************************************************/
static uint16_t max_pcl_id = 1023;

static uint16_t port_ipcl_id[NPORTS + 1] = {};
static uint16_t port_epcl_id[NPORTS + 1] = {};

static uint16_t vlan_pcl_id_first;

static uint16_t __attribute__((unused)) rule_ix_max = 1535;
static uint16_t port_stackmail_trap_primary_index;
static uint16_t port_stackmail_trap_secondary_index;

static uint16_t user_acl_stack_entries = 600; // TODO: calc it
static uint16_t user_acl_start_ix[NDEVS] = {};
static uint16_t user_acl_max[NDEVS] = {};

static uint16_t port_lbd_rule_ix[NPORTS + 1] = {};

static uint16_t port_lldp_rule_ix[NPORTS + 1] = {};

static uint16_t port_dhcptrap67_rule_ix[NPORTS + 1] = {};
static uint16_t port_dhcptrap68_rule_ix[NPORTS + 1] = {};

static uint16_t port_arp_inspector_trap_ix[NPORTS + 1] = {};

static uint16_t per_port_ip_source_guard_rules_count = 10;
static uint16_t port_ip_sourceguard_rule_start_ix[NPORTS + 1] = {};
static uint16_t port_ip_sourceguard_drop_rule_ix[NPORTS + 1] = {};

static uint16_t port_ip_ospf_mirror_rule_ix[NPORTS + 1] = {};

static uint16_t port_ip_rip_mirror_rule_ix[NPORTS + 1] = {};

static uint16_t vt_stack_entries = 300;
static uint16_t vt_stack_first_entry[NDEVS] = {};
static uint16_t vt_stack_max[NDEVS] = {};
static uint16_t vt_port_ipcl_def_rule_ix[NPORTS + 1] = {};
static uint16_t __attribute__((unused)) vt_port_epcl_def_rule_ix[NPORTS + 1] = {};

#define for_each_port(p) for (p = 1; p <= nports; p++)

/******************************************************************************/
/* Initialization                                                             */
/******************************************************************************/
static inline void
initialize_vars (void)
{
  int dev, pid;

  int pcl_id = 0;

  for_each_port(pid) {
    port_ipcl_id[pid] = pcl_id++;
    port_epcl_id[pid] = pcl_id++;
  }

  vlan_pcl_id_first = pcl_id;

  int idx[NDEVS];

  for_each_dev(dev) {
    idx[dev] = 0;
  }

  if ( stack_active() ) {
    port_stackmail_trap_primary_index   = idx[stack_pri_port->ldev]++;
    port_stackmail_trap_secondary_index = idx[stack_sec_port->ldev]++;
  } else {
    port_stackmail_trap_primary_index   = 0;
    port_stackmail_trap_secondary_index = 0;
  }

  for_each_dev(dev) {
    user_acl_start_ix[dev] = idx[dev];
    idx[dev] += user_acl_stack_entries;
    user_acl_max[dev] = idx[dev];
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    port_lbd_rule_ix[pid]                  = idx[port->ldev]++;
    port_lldp_rule_ix[pid]                 = idx[port->ldev]++;
    port_dhcptrap67_rule_ix[pid]           = idx[port->ldev]++;
    port_dhcptrap68_rule_ix[pid]           = idx[port->ldev]++;
    port_arp_inspector_trap_ix[pid]        = idx[port->ldev]++;
    port_ip_sourceguard_rule_start_ix[pid] = idx[port->ldev];
    idx[port->ldev] += per_port_ip_source_guard_rules_count;
    port_ip_sourceguard_drop_rule_ix[pid]  = idx[port->ldev]++;
    port_ip_ospf_mirror_rule_ix[pid]       = idx[port->ldev]++;
    port_ip_rip_mirror_rule_ix[pid]        = idx[port->ldev]++;
  }

  for_each_dev(dev) {
    vt_stack_first_entry[dev] = idx[dev];
    idx[dev] += vt_stack_entries;
    vt_stack_max[dev] = idx[dev];
  }

  for_each_port(pid) {
    struct port *port = port_ptr(pid);
    vt_port_ipcl_def_rule_ix[pid] = idx[port->ldev]++;
    vt_port_epcl_def_rule_ix[pid] = idx[port->ldev]++;
  }
}

#define PORT_IPCL_ID(n) \
  port_ipcl_id[n]
#define PORT_EPCL_ID(n) \
  port_epcl_id[n]

/******************************************************************************/
/* VT                                                                         */
/******************************************************************************/

static struct dstack *rules[NDEVS];

static void
pcl_init_rules (void)
{
  int i, d;

  for_each_dev(d) {
    dstack_init(&rules[d]);
    for (i = 0; i < vt_stack_entries; i++) {
      uint16_t rule_num = i + vt_stack_first_entry[d];
      dstack_push_back(rules[d], &rule_num, sizeof(rule_num));
    }
  }
}

static int
pcl_alloc_rules (int dev, uint16_t *nums, int n)
{
  int i;

  if (rules[dev]->count < n)
    return 0;

  for (i = 0; i < n; i++) {
    dstack_pop(rules[dev], &nums[i], NULL);
  }

  return 1;
}

static void
pcl_free_rules (int dev, const uint16_t *nums, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    dstack_push(rules[dev], &nums[i], sizeof(uint16_t));
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
      ix->ix[0] = vt_port_ipcl_def_rule_ix[pid];
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

void
pcl_setup_ospf(int d)
{
  port_id_t pi;
  for_each_port(pi) {
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
    mask.ruleExtNotIpv6.commonExt.ipProtocol  = 0xFF;

    rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pi);
    rule.ruleExtNotIpv6.common.isL2Valid = 1;
    rule.ruleExtNotIpv6.common.isIp = 1;
    rule.ruleExtNotIpv6.commonExt.ipProtocol  = 0x59; /* OSPF */

    act.pktCmd = CPSS_PACKET_CMD_MIRROR_TO_CPU_E;

    act.actionStop = GT_TRUE;
    act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E + 5;

    CRP (cpssDxChPclRuleSet
         (port->ldev,                                       /* devNum         */
          CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E, /* ruleFormat     */
          port_ip_ospf_mirror_rule_ix[pi],                  /* ruleIndex      */
          0,                                                /* ruleOptionsBmp */
          &mask,                                            /* maskPtr        */
          &rule,                                            /* patternPtr     */
          &act));                                           /* actionPtr      */
  }
}

/******************************************************************************/
/* RIP MULTICAST MIRROR                                                      */
/******************************************************************************/

static uint8_t rip_dest_ip[4]      = {224, 0, 0, 9};
static uint8_t rip_dest_ip_mask[4] = {255, 255, 255, 255};

void
pcl_setup_rip(int d)
{
  port_id_t pi;
  for_each_port(pi) {
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

    memcpy (&rule.ruleExtNotIpv6.dip, rip_dest_ip, 4);
    memcpy (&mask.ruleExtNotIpv6.dip, rip_dest_ip_mask, 4);

    act.pktCmd = CPSS_PACKET_CMD_MIRROR_TO_CPU_E;

    act.actionStop = GT_TRUE;
    act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E + 6;

    CRP (cpssDxChPclRuleSet
         (port->ldev,                                       /* devNum         */
          CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E, /* ruleFormat     */
          port_ip_rip_mirror_rule_ix[pi],                   /* ruleIndex      */
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
sg_init ()
{
  memset (sg_trap_enabled, 0, 65);
}

uint16_t
get_port_ip_sourceguard_rule_start_ix (port_id_t pi)
{
  return port_ip_sourceguard_rule_start_ix[pi];
}

uint16_t
get_per_port_ip_sourceguard_rules_count (void)
{
  return per_port_ip_source_guard_rules_count;
}

void
pcl_source_guard_trap_enable (port_id_t pi)
{
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

  CRP (cpssDxChPclRuleSet
       (port->ldev,                                       /* devNum         */
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E, /* ruleFormat     */
        port_ip_sourceguard_drop_rule_ix[pi],             /* ruleIndex      */
        0,                                                /* ruleOptionsBmp */
        &mask,                                            /* maskPtr        */
        &rule,                                            /* patternPtr     */
        &act));                                           /* actionPtr      */

  sg_trap_enabled[pi] = 1;
}

void
pcl_source_guard_trap_disable (port_id_t pi)
{
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return;

  CRP (cpssDxChPclRuleInvalidate
       (port->ldev,
        CPSS_PCL_RULE_SIZE_EXT_E,
        port_ip_sourceguard_drop_rule_ix[pi]));

  sg_trap_enabled[pi] = 0;
}

void
pcl_source_guard_drop_enable (port_id_t pi)
{
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

  CRP (cpssDxChPclRuleSet
       (port->ldev,                                       /* devNum         */
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E, /* ruleFormat     */
        port_ip_sourceguard_drop_rule_ix[pi],             /* ruleIndex      */
        0,                                                /* ruleOptionsBmp */
        &mask,                                            /* maskPtr        */
        &rule,                                            /* patternPtr     */
        &act));                                           /* actionPtr      */

  sg_trap_enabled[pi] = 0;
}

void
pcl_source_guard_drop_disable (port_id_t pi)
{
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return;

  CRP (cpssDxChPclRuleInvalidate
       (port->ldev,
        CPSS_PCL_RULE_SIZE_EXT_E,
        port_ip_sourceguard_drop_rule_ix[pi]));

  sg_trap_enabled[pi] = 0;
}

void
pcl_source_guard_rule_set (port_id_t pi,
                           mac_addr_t mac,
                           vid_t vid,
                           ip_addr_t ip,
                           uint16_t rule_ix,
                           uint8_t  verify_mac)
{
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
pcl_source_guard_rule_unset (port_id_t pi, uint16_t rule_ix)
{
  struct port *port = port_ptr (pi);

  if (is_stack_port(port))
    return;

  CRP (cpssDxChPclRuleInvalidate
       (port->ldev,
        CPSS_PCL_RULE_SIZE_EXT_E,
        rule_ix));
}

int
pcl_source_guard_trap_enabled (port_id_t pi)
{
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
DEFINE_INT_CMP(uint16_t);
DEFINE_INT_CMP(uint8_t);

#define PRINT_SEPARATOR(c, size) { \
  char __SEPARATOR__[size + 1];    \
  memset (__SEPARATOR__, c, size); \
  __SEPARATOR__[size] = '\0';      \
  DEBUG("\n%s\n", __SEPARATOR__);  \
}

static bool_t user_acl_fake_mode;

#define NAME_SZ 32
#define PCL_CMP_IDX_COUNT 8

const char* __attribute__((unused))
pcl_rule_action_to_str (pcl_rule_action_t rule_action)
{
  switch (rule_action) {
    case PCL_RULE_ACTION_PERMIT: return "PCL_RULE_ACTION_PERMIT";
    case PCL_RULE_ACTION_DENY  : return "PCL_RULE_ACTION_DENY";
    default                    : return "UNDEFINED";
  };
}

const char* __attribute__((unused))
pcl_rule_trap_action_to_str (pcl_rule_trap_action_t trap_action)
{
  switch (trap_action) {
    case PCL_RULE_TRAP_ACTION_LOG_INPUT: return "PCL_RULE_TRAP_ACTION_LOG_INPUT";
    case PCL_RULE_TRAP_ACTION_NONE     : return "PCL_RULE_TRAP_ACTION_NONE";
    default                            : return "UNDEFINED";
  };
}

static const char*
bitmap_to_str (uint8_t bitmap)
{
  switch (bitmap) {
    case   0: return "00000000";
    case   1: return "00000001";
    case   2: return "00000010";
    case   3: return "00000011";
    case   4: return "00000100";
    case   5: return "00000101";
    case   6: return "00000110";
    case   7: return "00000111";
    case   8: return "00001000";
    case   9: return "00001001";
    case  10: return "00001010";
    case  11: return "00001011";
    case  12: return "00001100";
    case  13: return "00001101";
    case  14: return "00001110";
    case  15: return "00001111";
    case  16: return "00010000";
    case  17: return "00010001";
    case  18: return "00010010";
    case  19: return "00010011";
    case  20: return "00010100";
    case  21: return "00010101";
    case  22: return "00010110";
    case  23: return "00010111";
    case  24: return "00011000";
    case  25: return "00011001";
    case  26: return "00011010";
    case  27: return "00011011";
    case  28: return "00011100";
    case  29: return "00011101";
    case  30: return "00011110";
    case  31: return "00011111";
    case  32: return "00100000";
    case  33: return "00100001";
    case  34: return "00100010";
    case  35: return "00100011";
    case  36: return "00100100";
    case  37: return "00100101";
    case  38: return "00100110";
    case  39: return "00100111";
    case  40: return "00101000";
    case  41: return "00101001";
    case  42: return "00101010";
    case  43: return "00101011";
    case  44: return "00101100";
    case  45: return "00101101";
    case  46: return "00101110";
    case  47: return "00101111";
    case  48: return "00110000";
    case  49: return "00110001";
    case  50: return "00110010";
    case  51: return "00110011";
    case  52: return "00110100";
    case  53: return "00110101";
    case  54: return "00110110";
    case  55: return "00110111";
    case  56: return "00111000";
    case  57: return "00111001";
    case  58: return "00111010";
    case  59: return "00111011";
    case  60: return "00111100";
    case  61: return "00111101";
    case  62: return "00111110";
    case  63: return "00111111";
    case  64: return "01000000";
    case  65: return "01000001";
    case  66: return "01000010";
    case  67: return "01000011";
    case  68: return "01000100";
    case  69: return "01000101";
    case  70: return "01000110";
    case  71: return "01000111";
    case  72: return "01001000";
    case  73: return "01001001";
    case  74: return "01001010";
    case  75: return "01001011";
    case  76: return "01001100";
    case  77: return "01001101";
    case  78: return "01001110";
    case  79: return "01001111";
    case  80: return "01010000";
    case  81: return "01010001";
    case  82: return "01010010";
    case  83: return "01010011";
    case  84: return "01010100";
    case  85: return "01010101";
    case  86: return "01010110";
    case  87: return "01010111";
    case  88: return "01011000";
    case  89: return "01011001";
    case  90: return "01011010";
    case  91: return "01011011";
    case  92: return "01011100";
    case  93: return "01011101";
    case  94: return "01011110";
    case  95: return "01011111";
    case  96: return "01100000";
    case  97: return "01100001";
    case  98: return "01100010";
    case  99: return "01100011";
    case 100: return "01100100";
    case 101: return "01100101";
    case 102: return "01100110";
    case 103: return "01100111";
    case 104: return "01101000";
    case 105: return "01101001";
    case 106: return "01101010";
    case 107: return "01101011";
    case 108: return "01101100";
    case 109: return "01101101";
    case 110: return "01101110";
    case 111: return "01101111";
    case 112: return "01110000";
    case 113: return "01110001";
    case 114: return "01110010";
    case 115: return "01110011";
    case 116: return "01110100";
    case 117: return "01110101";
    case 118: return "01110110";
    case 119: return "01110111";
    case 120: return "01111000";
    case 121: return "01111001";
    case 122: return "01111010";
    case 123: return "01111011";
    case 124: return "01111100";
    case 125: return "01111101";
    case 126: return "01111110";
    case 127: return "01111111";
    case 128: return "10000000";
    case 129: return "10000001";
    case 130: return "10000010";
    case 131: return "10000011";
    case 132: return "10000100";
    case 133: return "10000101";
    case 134: return "10000110";
    case 135: return "10000111";
    case 136: return "10001000";
    case 137: return "10001001";
    case 138: return "10001010";
    case 139: return "10001011";
    case 140: return "10001100";
    case 141: return "10001101";
    case 142: return "10001110";
    case 143: return "10001111";
    case 144: return "10010000";
    case 145: return "10010001";
    case 146: return "10010010";
    case 147: return "10010011";
    case 148: return "10010100";
    case 149: return "10010101";
    case 150: return "10010110";
    case 151: return "10010111";
    case 152: return "10011000";
    case 153: return "10011001";
    case 154: return "10011010";
    case 155: return "10011011";
    case 156: return "10011100";
    case 157: return "10011101";
    case 158: return "10011110";
    case 159: return "10011111";
    case 160: return "10100000";
    case 161: return "10100001";
    case 162: return "10100010";
    case 163: return "10100011";
    case 164: return "10100100";
    case 165: return "10100101";
    case 166: return "10100110";
    case 167: return "10100111";
    case 168: return "10101000";
    case 169: return "10101001";
    case 170: return "10101010";
    case 171: return "10101011";
    case 172: return "10101100";
    case 173: return "10101101";
    case 174: return "10101110";
    case 175: return "10101111";
    case 176: return "10110000";
    case 177: return "10110001";
    case 178: return "10110010";
    case 179: return "10110011";
    case 180: return "10110100";
    case 181: return "10110101";
    case 182: return "10110110";
    case 183: return "10110111";
    case 184: return "10111000";
    case 185: return "10111001";
    case 186: return "10111010";
    case 187: return "10111011";
    case 188: return "10111100";
    case 189: return "10111101";
    case 190: return "10111110";
    case 191: return "10111111";
    case 192: return "11000000";
    case 193: return "11000001";
    case 194: return "11000010";
    case 195: return "11000011";
    case 196: return "11000100";
    case 197: return "11000101";
    case 198: return "11000110";
    case 199: return "11000111";
    case 200: return "11001000";
    case 201: return "11001001";
    case 202: return "11001010";
    case 203: return "11001011";
    case 204: return "11001100";
    case 205: return "11001101";
    case 206: return "11001110";
    case 207: return "11001111";
    case 208: return "11010000";
    case 209: return "11010001";
    case 210: return "11010010";
    case 211: return "11010011";
    case 212: return "11010100";
    case 213: return "11010101";
    case 214: return "11010110";
    case 215: return "11010111";
    case 216: return "11011000";
    case 217: return "11011001";
    case 218: return "11011010";
    case 219: return "11011011";
    case 220: return "11011100";
    case 221: return "11011101";
    case 222: return "11011110";
    case 223: return "11011111";
    case 224: return "11100000";
    case 225: return "11100001";
    case 226: return "11100010";
    case 227: return "11100011";
    case 228: return "11100100";
    case 229: return "11100101";
    case 230: return "11100110";
    case 231: return "11100111";
    case 232: return "11101000";
    case 233: return "11101001";
    case 234: return "11101010";
    case 235: return "11101011";
    case 236: return "11101100";
    case 237: return "11101101";
    case 238: return "11101110";
    case 239: return "11101111";
    case 240: return "11110000";
    case 241: return "11110001";
    case 242: return "11110010";
    case 243: return "11110011";
    case 244: return "11110100";
    case 245: return "11110101";
    case 246: return "11110110";
    case 247: return "11110111";
    case 248: return "11111000";
    case 249: return "11111001";
    case 250: return "11111010";
    case 251: return "11111011";
    case 252: return "11111100";
    case 253: return "11111101";
    case 254: return "11111110";
    case 255: return "11111111";
    default : return "--------";
  };
}

#define nth_byte(n, value) ((uint8_t)(((value) & (0xff << ((n)*8))) >> ((n)*8)))

#define ip_addr_fmt "%d.%d.%d.%d"

#define mac_addr_fmt "%02X:%02X:%02X:%02X:%02X:%02X"

#define ipv6_addr_fmt "%X:%X:%X:%X:%X:%X:%X:%X"

#define ip_addr_to_printf_arg(ip) (ip)[0],(ip)[1],(ip)[2],(ip)[3]

#define mac_addr_to_printf_arg(m)                          \
  ((uint8_t*)&m)[0], ((uint8_t*)&m)[1], ((uint8_t*)&m)[2], \
  ((uint8_t*)&m)[3], ((uint8_t*)&m)[4], ((uint8_t*)&m)[5]

#define ipv6_addr_to_printf_arg(ip)        \
  ((uint16_t*)&ip)[0],((uint16_t*)&ip)[1], \
  ((uint16_t*)&ip)[2],((uint16_t*)&ip)[3], \
  ((uint16_t*)&ip)[4],((uint16_t*)&ip)[5], \
  ((uint16_t*)&ip)[6],((uint16_t*)&ip)[7]

enum pcl_ip_port_type
{
  PCL_IP_PORT_TYPE_SRC,
  PCL_IP_PORT_TYPE_DST
};
typedef uint8_t pcl_ip_port_type_t;

enum pcl_port_cmp_operator
{
  PCL_PORT_CMP_OPERATOR_GTE,
  PCL_PORT_CMP_OPERATOR_LTE
};
typedef uint8_t pcl_port_cmp_operator_t;

struct rule_binding_key {
  char                 name[NAME_SZ];
  pcl_rule_num_t       num;
  struct pcl_interface interface;
  pcl_dest_t           destination;
} __attribute__((packed));

struct rule_binding {
  struct rule_binding_key key;
  uint16_t                rule_ix;
  UT_hash_handle          hh;
} __attribute__((packed));

struct port_cmp_key {
  pcl_dest_t              destination;
  uint8_t                 ip_proto;
  pcl_ip_port_type_t      port_type;
  pcl_port_cmp_operator_t op;
  uint16_t                port_num;
} __attribute__((packed));

struct rule_ix_list {
  struct rule_ix_list *prev;
  struct rule_ix_list *next;
  uint16_t            rule_ix;
} __attribute__((packed));

struct port_cmp {
  struct port_cmp_key key;
  uint8_t             cmp_ix;
  struct rule_ix_list *rule_idxs;
  UT_hash_handle      hh;
} __attribute__((packed));

static struct user_acl_t {
  struct dstack       *pcl_ids;
  uint16_t            vlan_ipcl_id[4095];

  struct dstack       *rules[NDEVS];
  struct rule_binding *bindings[NDEVS];

  struct dstack       *port_cmp_idxs[NDEVS];
  struct port_cmp     *port_cmps[NDEVS];
} *user_acl, *user_acl_fake, *curr_acl;


static void
pcl_enable_vlan (vid_t, uint16_t);

static enum status
pcl_tcp_udp_port_cmp_set (int dev, struct port_cmp *port_cmp);


static void
user_acl_init (struct user_acl_t **u)
{
  int i, d;

  (*u) = malloc(sizeof(struct user_acl_t));

  dstack_init(&(*u)->pcl_ids);
  int pcl_ids_count = (max_pcl_id - vlan_pcl_id_first) + 1;
  for (i = 0; i < pcl_ids_count; i++) {
    uint16_t pcl_id = i + vlan_pcl_id_first;
    dstack_push_back((*u)->pcl_ids, &pcl_id, sizeof(pcl_id));
  }

  memset((*u)->vlan_ipcl_id, 0, 4095);

  for_each_dev(d) {
    dstack_init(&(*u)->rules[d]);
    for (i = 0; i < user_acl_stack_entries; i++) {
      uint16_t rule_num = i + user_acl_start_ix[d];
      dstack_push_back((*u)->rules[d], &rule_num, sizeof(rule_num));
    }

    (*u)->bindings[d]  = NULL;

    dstack_init(&(*u)->port_cmp_idxs[d]);
    for (i = 0; i < PCL_CMP_IDX_COUNT; i++) {
      uint8_t ix = i;
      dstack_push_back((*u)->port_cmp_idxs[d], &ix, sizeof(ix));
    }

    (*u)->port_cmps[d] = NULL;
  }
}

static void
pcl_init_user_acl (void)
{
  user_acl_init(&user_acl);
  user_acl_init(&user_acl_fake);
  curr_acl = user_acl;
}

static enum status
pcl_add_vlan_pcl_id (vid_t vid)
{
  if (!curr_acl->vlan_ipcl_id[vid]) {
    if (!curr_acl->pcl_ids->count) {
      return ST_BAD_VALUE;
    }

    dstack_pop(curr_acl->pcl_ids, &curr_acl->vlan_ipcl_id[vid], NULL);

    if (!user_acl_fake_mode) {
      pcl_enable_vlan(vid, curr_acl->vlan_ipcl_id[vid]);
    }
  }

  return ST_OK;
}

static enum status
pcl_add_rule_ix (int dev, struct rule_binding_key *key, uint16_t *rule_ix)
{
  struct rule_binding *tmp = NULL;
  HASH_FIND(hh,
            curr_acl->bindings[dev],
            key,
            sizeof(struct rule_binding_key),
            tmp);

  if (tmp) {
    return ST_BAD_VALUE;
  }

  if (!curr_acl->rules[dev]->count) {
    return ST_BAD_VALUE;
  }

  dstack_pop(curr_acl->rules[dev], rule_ix, NULL);

  struct rule_binding *bind = malloc(sizeof(struct rule_binding));
  bind->key     = *key;
  bind->rule_ix = *rule_ix;

  HASH_ADD(hh,
           curr_acl->bindings[dev],
           key,
           sizeof(struct rule_binding_key),
           bind);

  return ST_OK;
}

static enum status
pcl_find_rule_ix (int dev, struct rule_binding_key *key, uint16_t *rule_ix)
{
  struct rule_binding *bind = NULL;
  HASH_FIND(hh,
            curr_acl->bindings[dev],
            key,
            sizeof(struct rule_binding_key),
            bind);

  if (!bind) {
    return ST_BAD_VALUE;
  }

  *rule_ix = bind->rule_ix;

  return ST_OK;
}

static enum status
pcl_add_port_cmp_ix (int                     dev,
                     pcl_dest_t              destination,
                     uint8_t                 ip_proto,
                     pcl_ip_port_type_t      port_type,
                     pcl_port_cmp_operator_t op,
                     uint16_t                port_num,
                     uint16_t                rule_ix,
                     uint8_t                 *cmp_ix)
{
  struct port_cmp_key key;
  key.destination = destination;
  key.ip_proto    = ip_proto;
  key.port_type   = port_type;
  key.op          = op;
  key.port_num    = port_num;

  struct port_cmp *port_cmp = NULL;
  HASH_FIND(hh,
            curr_acl->port_cmps[dev],
            &key,
            sizeof(struct port_cmp_key),
            port_cmp);

  if (port_cmp) {
    struct rule_ix_list *elem;
    elem          = malloc(sizeof(struct rule_ix_list));
    elem->prev    = NULL;
    elem->next    = NULL;
    elem->rule_ix = rule_ix;
    DL_PREPEND(port_cmp->rule_idxs, elem);
    return ST_OK;
  }

  if (!curr_acl->port_cmp_idxs[dev]->count) {
    return ST_BAD_VALUE;
  }

  dstack_pop(curr_acl->port_cmp_idxs[dev], cmp_ix, NULL);
  port_cmp            = malloc(sizeof(struct port_cmp));
  port_cmp->key       = key;
  port_cmp->cmp_ix    = *cmp_ix;
  port_cmp->rule_idxs = NULL;

  struct rule_ix_list *elem;
  elem          = malloc(sizeof(struct rule_ix_list));
  elem->prev    = NULL;
  elem->next    = NULL;
  elem->rule_ix = rule_ix;
  DL_PREPEND(port_cmp->rule_idxs, elem);

  HASH_ADD(hh,
           curr_acl->port_cmps[dev],
           key,
           sizeof(struct port_cmp_key),
           port_cmp);

  if (!user_acl_fake_mode) {
    pcl_tcp_udp_port_cmp_set(dev, port_cmp);
  }

  return ST_OK;
}

static enum status
pcl_tcp_udp_port_cmp_set (int dev, struct port_cmp *port_cmp)
{
  CPSS_PCL_DIRECTION_ENT         dir;
  CPSS_L4_PROTOCOL_ENT           proto;
  CPSS_L4_PROTOCOL_PORT_TYPE_ENT port_type;
  CPSS_COMPARE_OPERATOR_ENT      op;

  switch (port_cmp->key.destination) {
    case PCL_DEST_INGRESS:
      dir = CPSS_PCL_DIRECTION_INGRESS_E;
      break;
    case PCL_DEST_EGRESS:
      dir = CPSS_PCL_DIRECTION_EGRESS_E;
      break;
    default:
      return ST_BAD_VALUE;
  };

  switch (port_cmp->key.ip_proto) {
    case IP_PROTO_TCP:
      proto = CPSS_L4_PROTOCOL_TCP_E;
      break;
    case IP_PROTO_UDP:
      proto = CPSS_L4_PROTOCOL_UDP_E;
      break;
    default:
      return ST_BAD_VALUE;
  };

  switch (port_cmp->key.port_type) {
    case PCL_IP_PORT_TYPE_SRC:
      port_type = CPSS_L4_PROTOCOL_PORT_SRC_E;
      break;
    case PCL_IP_PORT_TYPE_DST:
      port_type = CPSS_L4_PROTOCOL_PORT_DST_E;
      break;
    default:
      return ST_BAD_VALUE;
  };

  switch (port_cmp->key.op) {
    case PCL_PORT_CMP_OPERATOR_GTE:
      op = CPSS_COMPARE_OPERATOR_GTE;
      break;
    case PCL_PORT_CMP_OPERATOR_LTE:
      op = CPSS_COMPARE_OPERATOR_LTE;
      break;
    default:
      return ST_BAD_VALUE;
  };

  CRP (cpssDxCh2PclTcpUdpPortComparatorSet
       (dev,
        dir,
        proto,
        port_cmp->cmp_ix,
        port_type,
        op,
        port_cmp->key.port_num));

  return ST_OK;
}

static enum status
set_pcl_action (uint16_t                 rule_ix,
                pcl_rule_action_t        action,
                pcl_rule_trap_action_t   trap_action,
                CPSS_DXCH_PCL_ACTION_STC *act)
{
  switch (action) {
    case PCL_RULE_ACTION_PERMIT:
      DEBUG("%s: %s\n", __FUNCTION__, "CPSS_PACKET_CMD_FORWARD_E");
      act->pktCmd = CPSS_PACKET_CMD_FORWARD_E;
      break;
    case PCL_RULE_ACTION_DENY:
      DEBUG("%s: %s\n", __FUNCTION__, "CPSS_PACKET_CMD_DROP_HARD_E");
      act->pktCmd = CPSS_PACKET_CMD_DROP_HARD_E;
      break;
    default:
      DEBUG("%s: action: invalid (%d)\n", __FUNCTION__, action);
      return ST_BAD_VALUE;
  };

  switch (trap_action) {
    case PCL_RULE_TRAP_ACTION_LOG_INPUT:
      DEBUG("%s: %s\n", __FUNCTION__, "PCL_RULE_TRAP_ACTION_LOG_INPUT");
      act->matchCounter.enableMatchCount  = GT_TRUE;
      act->matchCounter.matchCounterIndex = rule_ix;
      DEBUG("%s: matchCounterIndex: %d\n",
            __FUNCTION__,
            act->matchCounter.matchCounterIndex);
      break;
    case PCL_RULE_TRAP_ACTION_NONE:
      DEBUG("%s: %s\n", __FUNCTION__, "PCL_TRAP_ACTION_NONE");
      break;
    default:
      DEBUG("%s: trap action: invalid (%d)\n", __FUNCTION__, trap_action);
      return ST_BAD_VALUE;
  };

  act->actionStop = GT_TRUE;
  return ST_OK;
}

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

#define inactivate_rule(dev, type, rule_ix) {         \
  CRP(cpssDxChPclRuleInvalidate(dev, type, rule_ix)); \
}

#define set_pcl_id(rule, mask, format, pcl_id) {  \
  rule.format.common.pclId = pcl_id;              \
  mask.format.common.pclId = 0xFFFF;              \
  DEBUG("%s: pclId: %d\n", __FUNCTION__, pcl_id); \
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
  rule.format.l2Encap = 1;                        \
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

#define set_ip_protocol(rule, mask, format, proto) { \
  /* 0xFF: reserved for any IP protocol */           \
  if (proto != 0xFF) {                               \
    rule.format.commonExt.ipProtocol = proto;        \
    mask.format.commonExt.ipProtocol = 0xFF;         \
  }                                                  \
  DEBUG("%s: ipProtocol: value: 0x%X, mask:0x%X\n",  \
        __FUNCTION__,                                \
        rule.format.commonExt.ipProtocol,            \
        mask.format.commonExt.ipProtocol);           \
}

#define set_src_ip(rule, mask, format, src_ip, src_ip_mask) { \
  memcpy(&rule.format.sip, &src_ip, sizeof(GT_IPADDR));       \
  memcpy(&mask.format.sip, &src_ip_mask, sizeof(GT_IPADDR));  \
  DEBUG("%s: src_ip: %d.%d.%d.%d, sip: 0x%X\n",               \
        __FUNCTION__,                                         \
        ip_addr_to_printf_arg(src_ip),                        \
        rule.format.sip.u32Ip);                               \
  DEBUG("%s: src_ip_mask: %d.%d.%d.%d, sip: 0x%X\n",          \
        __FUNCTION__,                                         \
        ip_addr_to_printf_arg(src_ip_mask),                   \
        mask.format.sip.u32Ip);                               \
}

#define set_dst_ip(rule, mask, format, dst_ip, dst_ip_mask) { \
  memcpy(&rule.format.dip, &dst_ip, sizeof(GT_IPADDR));       \
  memcpy(&mask.format.dip, &dst_ip_mask, sizeof(GT_IPADDR));  \
  DEBUG("%s: dst_ip: %d.%d.%d.%d, dip: 0x%X\n",               \
        __FUNCTION__,                                         \
        ip_addr_to_printf_arg(dst_ip),                        \
        rule.format.dip.u32Ip);                               \
  DEBUG("%s: dst_ip_mask: %d.%d.%d.%d, dip: 0x%X\n",          \
        __FUNCTION__,                                         \
        ip_addr_to_printf_arg(dst_ip_mask),                   \
        mask.format.dip.u32Ip);                               \
}

#define set_src_ip_port(rule, mask, format, src_ip_port, src_ip_port_mask) { \
  rule.format.commonExt.l4Byte0 = nth_byte(1, src_ip_port);                  \
  rule.format.commonExt.l4Byte1 = nth_byte(0, src_ip_port);                  \
  mask.format.commonExt.l4Byte0 = nth_byte(1, src_ip_port_mask);             \
  mask.format.commonExt.l4Byte1 = nth_byte(0, src_ip_port_mask);             \
  DEBUG("%s: src_ip_port: 0x%X, l4Byte0: 0x%X, l4Byte1: 0x%X\n",             \
        __FUNCTION__,                                                        \
        src_ip_port,                                                         \
        rule.format.commonExt.l4Byte0,                                       \
        rule.format.commonExt.l4Byte1);                                      \
  DEBUG("%s: src_ip_port_mask: 0x%X, l4Byte0: 0x%X, l4Byte1: 0x%X\n",        \
        __FUNCTION__,                                                        \
        src_ip_port_mask,                                                    \
        mask.format.commonExt.l4Byte0,                                       \
        mask.format.commonExt.l4Byte1);                                      \
}

#define set_dst_ip_port(rule, mask, format, dst_ip_port, dst_ip_port_mask) { \
  rule.format.commonExt.l4Byte2 = nth_byte(1, dst_ip_port);                  \
  rule.format.commonExt.l4Byte3 = nth_byte(0, dst_ip_port);                  \
  mask.format.commonExt.l4Byte2 = nth_byte(1, dst_ip_port_mask);             \
  mask.format.commonExt.l4Byte3 = nth_byte(0, dst_ip_port_mask);             \
  DEBUG("%s: dst_ip_port: 0x%X, l4Byte2: 0x%X, l4Byte3: 0x%X\n",             \
        __FUNCTION__,                                                        \
        dst_ip_port,                                                         \
        rule.format.commonExt.l4Byte2,                                       \
        rule.format.commonExt.l4Byte3);                                      \
  DEBUG("%s: dst_ip_port_mask: 0x%X, l4Byte2: 0x%X, l4Byte3: 0x%X\n",        \
        __FUNCTION__,                                                        \
        dst_ip_port_mask,                                                    \
        mask.format.commonExt.l4Byte2,                                       \
        mask.format.commonExt.l4Byte3);                                      \
}

#define set_ip_port_range(rule, mask, format, cmp_ix, cmp_bitmap_field) { \
  uint8_t bitmap = 0;                                                     \
  bitmap |= (uint8_t)((1 << cmp_ix[0]) & 0xff);                           \
  bitmap |= (uint8_t)((1 << cmp_ix[1]) & 0xff);                           \
  rule.format.cmp_bitmap_field |= bitmap;                                 \
  mask.format.cmp_bitmap_field |= bitmap;                                 \
  DEBUG("%s: bitmap: %s\n",                                               \
        __FUNCTION__,                                                     \
        bitmap_to_str(bitmap));                                           \
}

#define set_dscp(rule, mask, format, dscp_val, dscp_mask) { \
  rule.format.commonExt.dscp = dscp_val;                    \
  mask.format.commonExt.dscp = dscp_mask;                   \
  DEBUG("%s: dscp: 0x%X, dscp: 0x%X\n",                     \
        __FUNCTION__,                                       \
        dscp_val,                                           \
        rule.format.commonExt.dscp);                        \
  DEBUG("%s: dscp_mask: 0x%X, dscp: 0x%X\n",                \
        __FUNCTION__,                                       \
        dscp_mask,                                          \
        mask.format.commonExt.dscp);                        \
}

#define set_icmp_type(rule, mask, format, icmp_type, icmp_type_mask) { \
  rule.format.commonExt.l4Byte0 = icmp_type;                           \
  mask.format.commonExt.l4Byte0 = icmp_type_mask;                      \
  DEBUG("%s: icmp_type: 0x%X, l4Byte0: 0x%X\n",                        \
        __FUNCTION__,                                                  \
        icmp_type,                                                     \
        rule.format.commonExt.l4Byte0);                                \
  DEBUG("%s: icmp_type_mask: 0x%X, l4Byte0: 0x%X\n",                   \
        __FUNCTION__,                                                  \
        icmp_type_mask,                                                \
        mask.format.commonExt.l4Byte0);                                \
}

#define set_icmp_code(rule, mask, format, icmp_code, icmp_code_mask) { \
  rule.format.commonExt.l4Byte1 = icmp_code;                           \
  mask.format.commonExt.l4Byte1 = icmp_code_mask;                      \
  DEBUG("%s: icmp_code: 0x%X, l4Byte1: 0x%X\n",                        \
        __FUNCTION__,                                                  \
        icmp_code,                                                     \
        rule.format.commonExt.l4Byte1);                                \
  DEBUG("%s: icmp_code_mask: 0x%X, l4Byte1: 0x%X\n",                   \
        __FUNCTION__,                                                  \
        icmp_code_mask,                                                \
        mask.format.commonExt.l4Byte1);                                \
}

#define set_igmp_type(rule, mask, format, igmp_type, igmp_type_mask) { \
  rule.format.commonExt.l4Byte0 = igmp_type;                           \
  mask.format.commonExt.l4Byte0 = igmp_type_mask;                      \
  DEBUG("%s: igmp_type: 0x%X, l4Byte0: 0x%X\n",                        \
        __FUNCTION__,                                                  \
        igmp_type,                                                     \
        rule.format.commonExt.l4Byte0);                                \
  DEBUG("%s: igmp_type_mask: 0x%X, l4Byte0: 0x%X\n",                   \
        __FUNCTION__,                                                  \
        igmp_type_mask,                                                \
        mask.format.commonExt.l4Byte0);                                \
}

#define set_tcp_flags(rule, mask, format, tcp_flags, tcp_flags_mask) { \
  rule.format.commonExt.l4Byte13 = tcp_flags;                          \
  mask.format.commonExt.l4Byte13 = tcp_flags_mask;                     \
  DEBUG("%s: tcp_flags: 0x%X, l4Byte13: 0x%X\n",                       \
        __FUNCTION__,                                                  \
        tcp_flags,                                                     \
        rule.format.commonExt.l4Byte13);                               \
  DEBUG("%s: tcp_flags_mask: 0x%X, l4Byte13: 0x%X\n",                  \
        __FUNCTION__,                                                  \
        tcp_flags_mask,                                                \
        mask.format.commonExt.l4Byte13);                               \
}

#define activate_rule(dev, type, rule_ix, rule_opt_bmp, mask, rule, act) {    \
  DEBUG("%s: activate_rule (dev = %d, rule_ix = %d, ...)\n",                  \
        __FUNCTION__,                                                         \
        dev,                                                                  \
        rule_ix);                                                             \
  DEBUG("\ncpssDxChPclRuleSet(\n"                                             \
        "  %d\n"                                                              \
        "  %s\n"                                                              \
        "  %d\n"                                                              \
        "  %d\n"                                                              \
        "  <mask>\n"                                                          \
        "  <rule>\n"                                                          \
        "  <act>)\n",                                                         \
        dev,                                                                  \
        #type,                                                                \
        rule_ix,                                                              \
        rule_opt_bmp);                                                        \
  CRP(cpssDxChPclRuleSet(dev, type, rule_ix, rule_opt_bmp, mask, rule, act)); \
}

/*DEBUG("\n<mask>:\n");                                                       \
  PRINTHexDump(mask, sizeof(*mask));                                          \
  DEBUG("\n<rule>:\n");                                                       \
  PRINTHexDump(rule, sizeof(*rule));                                          \
  DEBUG("\n<act>:\n");                                                        \
  PRINTHexDump(act, sizeof(*act));                                            \*/

#define set_ip_rule(dev, rule, mask, format, type, pcl_id, act, ip_rule, rule_ix, src_cmp_ix, dst_cmp_ix, cmp_bitmap_field) { \
  set_pcl_id(rule, mask, format, pcl_id);                                                                                     \
  set_packet_type_ip(rule, mask, format);                                                                                     \
  set_ip_protocol(rule, mask, format, ip_rule->proto);                                                                        \
  set_src_ip(rule, mask, format, ip_rule->src_ip, ip_rule->src_ip_mask);                                                      \
  set_dst_ip(rule, mask, format, ip_rule->dst_ip, ip_rule->dst_ip_mask);                                                      \
  set_dscp(rule, mask, format, ip_rule->dscp, ip_rule->dscp_mask);                                                            \
  if (is_tcp_or_udp(ip_rule->proto)) {                                                                                        \
    if (ip_rule->src_ip_port_single) {                                                                                        \
      set_src_ip_port(rule, mask, format, ip_rule->src_ip_port, ip_rule->src_ip_port_mask);                                   \
    } else {                                                                                                                  \
      set_ip_port_range(rule, mask, format, src_cmp_ix, cmp_bitmap_field);                                                    \
    }                                                                                                                         \
    if (ip_rule->dst_ip_port_single) {                                                                                        \
      set_dst_ip_port(rule, mask, format, ip_rule->dst_ip_port, ip_rule->dst_ip_port_mask);                                   \
    } else {                                                                                                                  \
      set_ip_port_range(rule, mask, format, dst_cmp_ix, cmp_bitmap_field);                                                    \
    }                                                                                                                         \
  }                                                                                                                           \
  if (ip_rule->proto == 0x01 /* ICMP */) {                                                                                    \
    set_icmp_type(rule, mask, format, ip_rule->icmp_type, ip_rule->icmp_type_mask);                                           \
    set_icmp_code(rule, mask, format, ip_rule->icmp_code, ip_rule->icmp_code_mask);                                           \
  } else if (ip_rule->proto == 0x02 /* IGMP */) {                                                                             \
    set_igmp_type(rule, mask, format, ip_rule->igmp_type, ip_rule->igmp_type_mask);                                           \
  } else if (ip_rule->proto == 0x06 /* TCP */) {                                                                              \
    set_tcp_flags(rule, mask, format, ip_rule->tcp_flags, ip_rule->tcp_flags_mask);                                           \
  }                                                                                                                           \
  activate_rule(dev, type, rule_ix, 0, &mask, &rule, &act);                                                                   \
}

#define set_src_mac(rule, mask, format, src_mac, src_mac_mask) {     \
  memcpy(&rule.format.macSa, &src_mac, sizeof(GT_ETHERADDR));        \
  memcpy(&mask.format.macSa, &src_mac_mask, sizeof(GT_ETHERADDR));   \
  DEBUG("%s: src_mac: "mac_addr_fmt", macSa: "mac_addr_fmt"\n",      \
        __FUNCTION__,                                                \
        mac_addr_to_printf_arg(src_mac),                             \
        mac_addr_to_printf_arg(rule.format.macSa));                  \
  DEBUG("%s: src_mac_mask: "mac_addr_fmt", macSa: "mac_addr_fmt"\n", \
        __FUNCTION__,                                                \
        mac_addr_to_printf_arg(src_mac_mask),                        \
        mac_addr_to_printf_arg(mask.format.macSa));                  \
}

#define set_dst_mac(rule, mask, format, dst_mac, dst_mac_mask) {     \
  memcpy (&rule.format.macDa, &dst_mac, sizeof(GT_ETHERADDR));       \
  memcpy (&mask.format.macDa, &dst_mac_mask, sizeof(GT_ETHERADDR));  \
  DEBUG("%s: dst_mac: "mac_addr_fmt", macDa: "mac_addr_fmt"\n",      \
        __FUNCTION__,                                                \
        mac_addr_to_printf_arg(dst_mac),                             \
        mac_addr_to_printf_arg(rule.format.macDa));                  \
  DEBUG("%s: dst_mac_mask: "mac_addr_fmt", macDa: "mac_addr_fmt"\n", \
        __FUNCTION__,                                                \
        mac_addr_to_printf_arg(dst_mac_mask),                        \
        mac_addr_to_printf_arg(mask.format.macDa));                  \
}

#define set_eth_type(rule, mask, format, eth_type, eth_type_mask) { \
  rule.format.etherType = eth_type;                                 \
  mask.format.etherType = eth_type_mask;                            \
  DEBUG("%s: eth_type: 0x%X, etherType: 0x%X\n",                    \
        __FUNCTION__,                                               \
        eth_type,                                                   \
        rule.format.etherType);                                     \
  DEBUG("%s: eth_type_mask: 0x%X, etherType: 0x%X\n",               \
        __FUNCTION__,                                               \
        eth_type_mask,                                              \
        mask.format.etherType);                                     \
}

#define set_vid(rule, mask, format, vlan, vlan_mask) { \
  rule.format.common.vid = vlan;                       \
  mask.format.common.vid = vlan_mask;                  \
  DEBUG("%s: vlan: %d, common.vid: 0x%X\n",            \
        __FUNCTION__,                                  \
        vlan,                                          \
        rule.format.common.vid);                       \
  DEBUG("%s: vlan_mask: 0x%X, common.vid: 0x%X\n",     \
        __FUNCTION__,                                  \
        vlan_mask,                                     \
        mask.format.common.vid);                       \
}

#define set_cos(rule, mask, format, cos, cos_mask) { \
  rule.format.common.up = cos;                       \
  mask.format.common.up = cos_mask;                  \
  DEBUG("%s: cos: %d, common.up: 0x%X\n",            \
        __FUNCTION__,                                \
        cos,                                         \
        rule.format.common.up);                      \
  DEBUG("%s: cos_mask: 0x%X, common.up: 0x%X\n",     \
        __FUNCTION__,                                \
        cos_mask,                                    \
        mask.format.common.up);                      \
}

#define set_mac_rule(dev, rule, mask, format, type, pcl_id, act, mac_rule, rule_ix) { \
  set_pcl_id(rule, mask, format, pcl_id);                                             \
  set_packet_type_mac(rule, mask, format);                                            \
  set_src_mac(rule, mask, format, mac_rule->src_mac, mac_rule->src_mac_mask);         \
  set_dst_mac(rule, mask, format, mac_rule->dst_mac, mac_rule->dst_mac_mask);         \
  set_eth_type(rule,mask,format,mac_rule->eth_type,mac_rule->eth_type_mask);          \
  set_vid(rule, mask, format, mac_rule->vid, mac_rule->vid_mask);                     \
  set_cos(rule, mask, format, mac_rule->cos, mac_rule->cos_mask);                     \
  activate_rule(dev, type, rule_ix, 0, &mask, &rule, &act);                           \
}

#define set_src_ipv6(rule, mask, format, src_ip, src_ip_mask) {     \
  memcpy(&rule.format.sip, &src_ip, sizeof(GT_IPV6ADDR));           \
  memcpy(&mask.format.sip, &src_ip_mask, sizeof(GT_IPV6ADDR));      \
  DEBUG("%s: src_ip: "ipv6_addr_fmt", sip: "ipv6_addr_fmt"\n",      \
        __FUNCTION__,                                               \
        ipv6_addr_to_printf_arg(src_ip),                            \
        ipv6_addr_to_printf_arg(rule.format.sip));                  \
  DEBUG("%s: src_ip_mask: "ipv6_addr_fmt", sip: "ipv6_addr_fmt"\n", \
        __FUNCTION__,                                               \
        ipv6_addr_to_printf_arg(src_ip_mask),                       \
        ipv6_addr_to_printf_arg(mask.format.sip));                  \
}

#define set_dst_ipv6(rule, mask, format, dst_ip, dst_ip_mask) {     \
  memcpy(&rule.format.dip, &dst_ip, sizeof(GT_IPV6ADDR));           \
  memcpy(&mask.format.dip, &dst_ip_mask, sizeof(GT_IPV6ADDR));      \
  DEBUG("%s: dst_ip: "ipv6_addr_fmt", dip: "ipv6_addr_fmt"\n",      \
        __FUNCTION__,                                               \
        ipv6_addr_to_printf_arg(dst_ip),                            \
        ipv6_addr_to_printf_arg(rule.format.dip));                  \
  DEBUG("%s: dst_ip_mask: "ipv6_addr_fmt", dip: "ipv6_addr_fmt"\n", \
        __FUNCTION__,                                               \
        ipv6_addr_to_printf_arg(dst_ip_mask),                       \
        ipv6_addr_to_printf_arg(mask.format.dip));                  \
}

#define set_ipv6_rule(dev, rule, mask, format, type, pcl_id, act, ipv6_rule, rule_ix, src_cmp_ix, dst_cmp_ix, cmp_bitmap_field) { \
  set_pcl_id(rule, mask, format, pcl_id);                                                                                         \
  set_packet_type_ipv6(rule, mask, format);                                                                                       \
  set_src_ipv6(rule, mask, format, ipv6_rule->src, ipv6_rule->src_mask);                                                          \
  set_dst_ipv6(rule, mask, format, ipv6_rule->dst, ipv6_rule->dst_mask);                                                          \
  set_dscp(rule, mask, format, ipv6_rule->dscp, ipv6_rule->dscp_mask);                                                            \
  if (is_tcp_or_udp(ipv6_rule->proto)) {                                                                                          \
    if (ipv6_rule->src_ip_port_single) {                                                                                          \
      set_src_ip_port(rule, mask, format, ipv6_rule->src_ip_port, ipv6_rule->src_ip_port_mask);                                   \
    } else {                                                                                                                      \
      set_ip_port_range(rule, mask, format, src_cmp_ix, cmp_bitmap_field);                                                        \
    }                                                                                                                             \
    if (ipv6_rule->dst_ip_port_single) {                                                                                          \
      set_dst_ip_port(rule, mask, format, ipv6_rule->dst_ip_port, ipv6_rule->dst_ip_port_mask);                                   \
    } else {                                                                                                                      \
      set_ip_port_range(rule, mask, format, dst_cmp_ix, cmp_bitmap_field);                                                        \
    }                                                                                                                             \
  }                                                                                                                               \
  if (ipv6_rule->proto == 0x01 /* ICMP */) {                                                                                      \
    set_icmp_type(rule, mask, format, ipv6_rule->icmp_type, ipv6_rule->icmp_type_mask);                                           \
    set_icmp_code(rule, mask, format, ipv6_rule->icmp_code, ipv6_rule->icmp_code_mask);                                           \
  } else if(ipv6_rule->proto == 0x06 /* TCP */) {                                                                                 \
    set_tcp_flags(rule, mask, format, ipv6_rule->tcp_flags, ipv6_rule->tcp_flags_mask);                                           \
  }                                                                                                                               \
  activate_rule(dev, type, rule_ix, 0, &mask, &rule, &act);                                                            \
}

#define FR(term)                   \
  if ((result = term) != ST_OK) {  \
    DEBUG("\nError: %s\n", #term); \
    goto out;                      \
  }

void
pcl_set_fake_mode_enabled (bool_t enable)
{
  DEBUG("\n%s FAKE MODE\n", enable ? "ENABLE" : "DISABLE");
  user_acl_fake_mode = enable;

  if (enable) {
    dstack_copy(&user_acl_fake->pcl_ids, user_acl->pcl_ids);
    memcpy(user_acl_fake->vlan_ipcl_id, user_acl->vlan_ipcl_id, 4095);

    int d;
    for_each_dev(d) {
      dstack_copy(&user_acl_fake->rules[d], user_acl->rules[d]);
      {
        struct rule_binding *iter, *tmp, *bind;
        HASH_ITER(hh, user_acl_fake->bindings[d], iter, tmp) {
          HASH_DEL(user_acl_fake->bindings[d], iter);
          free(iter);
        }

        HASH_ITER(hh, user_acl->bindings[d], iter, tmp) {
          bind = malloc(sizeof(struct rule_binding));
          memcpy(bind, iter, sizeof(struct rule_binding));
          HASH_ADD(hh,
                   user_acl_fake->bindings[d],
                   key,
                   sizeof(struct rule_binding_key),
                   bind);
        }
      }

      dstack_copy(&user_acl_fake->port_cmp_idxs[d], user_acl->port_cmp_idxs[d]);
      {
        struct port_cmp *iter, *tmp, *port_cmp;
        HASH_ITER(hh, user_acl_fake->port_cmps[d], iter, tmp) {
          HASH_DEL(user_acl_fake->port_cmps[d], iter);
          {
            struct rule_ix_list *elem, *tmp;
            DL_FOREACH_SAFE(iter->rule_idxs, elem, tmp) {
              DL_DELETE(iter->rule_idxs, elem);
              free(elem);
            }
          }
          free(iter);
        }

        HASH_ITER(hh, user_acl->port_cmps[d], iter, tmp) {
          port_cmp = malloc(sizeof(struct port_cmp));
          port_cmp->key       = iter->key;
          port_cmp->cmp_ix    = iter->cmp_ix;
          port_cmp->rule_idxs = NULL;

          struct rule_ix_list *elem, *rule_ix;
          DL_FOREACH(iter->rule_idxs, elem) {
            rule_ix = malloc(sizeof(struct rule_ix_list));
            rule_ix->prev    = NULL;
            rule_ix->next    = NULL;
            rule_ix->rule_ix = elem->rule_ix;
            DL_PREPEND(port_cmp->rule_idxs, rule_ix);
          }

          HASH_ADD(hh,
                   user_acl_fake->port_cmps[d],
                   key,
                   sizeof(struct port_cmp_key),
                   port_cmp);
        }
      }
    }
    curr_acl = user_acl_fake;
  } else {
    curr_acl = user_acl;
  }
}

enum status
pcl_ip_rule_set (char                 *name,
                 uint8_t              name_len,
                 pcl_rule_num_t       rule_num,
                 struct pcl_interface interface,
                 pcl_dest_t           dest,
                 pcl_rule_action_t    rule_action,
                 struct ip_pcl_rule   *rule_params)
{
  PRINT_SEPARATOR('=', 80);
  DEBUG("\n"
        "%s (\n"
        "  name           = %s,\n"
        "  rule_num       = %llu,\n"
        "  interface.type = %d,\n"
        "  interface.num  = %d,\n"
        "  dest           = %d,\n"
        "  rule_action    = %d)\n",
        __FUNCTION__,
        name,
        rule_num,
        interface.type,
        interface.num,
        dest,
        rule_action);

  enum status             result = ST_OK;
  struct rule_binding_key bind_key;
  struct port             *port = NULL;
  int                     d;

  if (name_len > NAME_SZ) {
    result = ST_BAD_VALUE;
    goto out;
  }

  memset(bind_key.name, 0, NAME_SZ);
  memcpy(bind_key.name, name, name_len);
  bind_key.num         = rule_num;
  bind_key.interface   = interface;
  bind_key.destination = dest;

  int devs[NDEVS];
  memset(devs, 0, sizeof(int)*NDEVS);
  int      dev_count = 0;
  uint16_t pcl_id    = 0;

  switch (interface.type) {
    case PCL_INTERFACE_TYPE_VLAN:
      FR(pcl_add_vlan_pcl_id(interface.num));
      pcl_id    = curr_acl->vlan_ipcl_id[interface.num];
      dev_count = NDEVS;
      for_each_dev(d) { devs[d] = d; };
      break;
    case PCL_INTERFACE_TYPE_PORT:
      FR(get_port_ptr(interface.num, &port));
      switch (dest) {
        case PCL_DEST_INGRESS:
          pcl_id = PORT_IPCL_ID(interface.num);
          break;
        case PCL_DEST_EGRESS:
          pcl_id = PORT_EPCL_ID(interface.num);
          break;
        default:
          result = ST_BAD_VALUE;
          goto out;
      };
      dev_count = 1;
      devs[0]   = port->ldev;
      break;
    default:
      result = ST_BAD_VALUE;
      goto out;
  };

  for (d = 0; d < dev_count; d++) {
    uint16_t rule_ix;
    uint8_t  src_cmp_ix[2];
    uint8_t  dst_cmp_ix[2];

    memset(src_cmp_ix, 0, 2);
    memset(dst_cmp_ix, 0, 2);

    FR(pcl_add_rule_ix(devs[d], &bind_key, &rule_ix));
    if (is_tcp_or_udp(rule_params->proto)) {
      if (!rule_params->src_ip_port_single) {
        FR(pcl_add_port_cmp_ix(devs[d],
                               dest,
                               rule_params->proto,
                               PCL_IP_PORT_TYPE_SRC,
                               PCL_PORT_CMP_OPERATOR_GTE,
                               rule_params->src_ip_port,
                               rule_ix,
                               &src_cmp_ix[0]));
        FR(pcl_add_port_cmp_ix(devs[d],
                               dest,
                               rule_params->proto,
                               PCL_IP_PORT_TYPE_SRC,
                               PCL_PORT_CMP_OPERATOR_LTE,
                               rule_params->src_ip_port_max,
                               rule_ix,
                               &src_cmp_ix[1]));
      }
      if (!rule_params->dst_ip_port_single) {
        FR(pcl_add_port_cmp_ix(devs[d],
                               dest,
                               rule_params->proto,
                               PCL_IP_PORT_TYPE_DST,
                               PCL_PORT_CMP_OPERATOR_GTE,
                               rule_params->dst_ip_port,
                               rule_ix,
                               &dst_cmp_ix[0]));
        FR(pcl_add_port_cmp_ix(devs[d],
                               dest,
                               rule_params->proto,
                               PCL_IP_PORT_TYPE_DST,
                               PCL_PORT_CMP_OPERATOR_LTE,
                               rule_params->dst_ip_port_max,
                               rule_ix,
                               &dst_cmp_ix[1]));
      }
    }

    if (!user_acl_fake_mode) {
      CPSS_DXCH_PCL_ACTION_STC act;
      memset(&act, 0, sizeof(act));
      FR(set_pcl_action(rule_ix, rule_action, rule_params->trap_action, &act));

      CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
      memset(&rule, 0, sizeof(rule));
      memset(&mask, 0, sizeof(mask));

      switch (dest) {
        case PCL_DEST_INGRESS:
          set_ip_rule(devs[d],
                      rule,
                      mask,
                      ruleExtNotIpv6,
                      CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
                      pcl_id,
                      act,
                      rule_params,
                      rule_ix,
                      src_cmp_ix,
                      dst_cmp_ix,
                      udb[2]);
          break;
        case PCL_DEST_EGRESS:
          set_ip_rule(devs[d],
                      rule,
                      mask,
                      ruleEgrExtNotIpv6,
                      CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
                      pcl_id,
                      act,
                      rule_params,
                      rule_ix,
                      src_cmp_ix,
                      dst_cmp_ix,
                      commonExt.egrTcpUdpPortComparator);
          break;
        default:
          result = ST_BAD_VALUE;
          goto out;
      };
    }
  }
out:
  PRINT_SEPARATOR('=', 80);
  return result;
}

enum status
pcl_mac_rule_set (char                 *name,
                  uint8_t              name_len,
                  pcl_rule_num_t       rule_num,
                  struct pcl_interface interface,
                  pcl_dest_t           dest,
                  pcl_rule_action_t    rule_action,
                  struct mac_pcl_rule  *rule_params)
{
  PRINT_SEPARATOR('=', 80);
  DEBUG("\n"
        "%s (\n"
        "  name           = %s,\n"
        "  rule_num       = %llu,\n"
        "  interface.type = %d,\n"
        "  interface.num  = %d,\n"
        "  dest           = %d,\n"
        "  rule_action    = %d)\n",
        __FUNCTION__,
        name,
        rule_num,
        interface.type,
        interface.num,
        dest,
        rule_action);

  enum status             result = ST_OK;
  struct rule_binding_key bind_key;
  struct port             *port = NULL;
  int                     d;

  if (name_len > NAME_SZ) {
    result = ST_BAD_VALUE;
    goto out;
  }

  memset(bind_key.name, 0, NAME_SZ);
  memcpy(bind_key.name, name, name_len);
  bind_key.num         = rule_num;
  bind_key.interface   = interface;
  bind_key.destination = dest;

  int devs[NDEVS];
  memset(devs, 0, sizeof(int)*NDEVS);
  int      dev_count = 0;
  uint16_t pcl_id    = 0;

  switch (interface.type) {
    case PCL_INTERFACE_TYPE_VLAN:
      FR(pcl_add_vlan_pcl_id(interface.num));
      pcl_id    = curr_acl->vlan_ipcl_id[interface.num];
      dev_count = NDEVS;
      for_each_dev(d) { devs[d] = d; };
      break;
    case PCL_INTERFACE_TYPE_PORT:
      FR(get_port_ptr(interface.num, &port));
      switch (dest) {
        case PCL_DEST_INGRESS:
          pcl_id = PORT_IPCL_ID(interface.num);
          break;
        case PCL_DEST_EGRESS:
          pcl_id = PORT_EPCL_ID(interface.num);
          break;
        default:
          result = ST_BAD_VALUE;
          goto out;
      };
      dev_count = 1;
      devs[0]   = port->ldev;
      break;
    default:
      result = ST_BAD_VALUE;
      goto out;
  };

  for (d = 0; d < dev_count; d++) {
    uint16_t rule_ix;
    uint8_t  src_cmp_ix[2];
    uint8_t  dst_cmp_ix[2];

    memset(src_cmp_ix, 0, 2);
    memset(dst_cmp_ix, 0, 2);

    FR(pcl_add_rule_ix(devs[d], &bind_key, &rule_ix));

    if (!user_acl_fake_mode) {
      CPSS_DXCH_PCL_ACTION_STC act;
      memset(&act, 0, sizeof(act));
      FR(set_pcl_action(rule_ix, rule_action, rule_params->trap_action, &act));

      CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
      memset(&rule, 0, sizeof(rule));
      memset(&mask, 0, sizeof(mask));

      switch (dest) {
        case PCL_DEST_INGRESS:
          set_mac_rule(devs[d],
                       rule,
                       mask,
                       ruleExtNotIpv6,
                       CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
                       pcl_id,
                       act,
                       rule_params,
                       rule_ix);
          break;
        case PCL_DEST_EGRESS:
          set_mac_rule(devs[d],
                       rule,
                       mask,
                       ruleEgrExtNotIpv6,
                       CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
                       pcl_id,
                       act,
                       rule_params,
                       rule_ix);
          break;
        default:
          result = ST_BAD_VALUE;
          goto out;
      };
    }
  }
out:
  PRINT_SEPARATOR('=', 80);
  return result;
}

enum status
pcl_ipv6_rule_set (char                 *name,
                   uint8_t              name_len,
                   pcl_rule_num_t       rule_num,
                   struct pcl_interface interface,
                   pcl_dest_t           dest,
                   pcl_rule_action_t    rule_action,
                   struct ipv6_pcl_rule *rule_params)
{
  PRINT_SEPARATOR('=', 80);
  DEBUG("\n"
        "%s (\n"
        "  name           = %s,\n"
        "  rule_num       = %llu,\n"
        "  interface.type = %d,\n"
        "  interface.num  = %d,\n"
        "  dest           = %d,\n"
        "  rule_action    = %d)\n",
        __FUNCTION__,
        name,
        rule_num,
        interface.type,
        interface.num,
        dest,
        rule_action);

  enum status             result = ST_OK;
  struct rule_binding_key bind_key;
  struct port             *port = NULL;
  int                     d;

  if (name_len > NAME_SZ) {
    result = ST_BAD_VALUE;
    goto out;
  }

  memset(bind_key.name, 0, NAME_SZ);
  memcpy(bind_key.name, name, name_len);
  bind_key.num         = rule_num;
  bind_key.interface   = interface;
  bind_key.destination = dest;

  int devs[NDEVS];
  memset(devs, 0, sizeof(int)*NDEVS);
  int      dev_count = 0;
  uint16_t pcl_id    = 0;

  switch (interface.type) {
    case PCL_INTERFACE_TYPE_VLAN:
      FR(pcl_add_vlan_pcl_id(interface.num));
      pcl_id    = curr_acl->vlan_ipcl_id[interface.num];
      dev_count = NDEVS;
      for_each_dev(d) { devs[d] = d; };
      break;
    case PCL_INTERFACE_TYPE_PORT:
      FR(get_port_ptr(interface.num, &port));
      switch (dest) {
        case PCL_DEST_INGRESS:
          pcl_id = PORT_IPCL_ID(interface.num);
          break;
        case PCL_DEST_EGRESS:
          pcl_id = PORT_EPCL_ID(interface.num);
          break;
        default:
          result = ST_BAD_VALUE;
          goto out;
      };
      dev_count = 1;
      devs[0]   = port->ldev;
      break;
    default:
      result = ST_BAD_VALUE;
      goto out;
  };

  for (d = 0; d < dev_count; d++) {
    uint16_t rule_ix;
    uint8_t  src_cmp_ix[2];
    uint8_t  dst_cmp_ix[2];

    memset(src_cmp_ix, 0, 2);
    memset(dst_cmp_ix, 0, 2);

    FR(pcl_add_rule_ix(devs[d], &bind_key, &rule_ix));
    if (is_tcp_or_udp(rule_params->proto)) {
      if (!rule_params->src_ip_port_single) {
        FR(pcl_add_port_cmp_ix(devs[d],
                               dest,
                               rule_params->proto,
                               PCL_IP_PORT_TYPE_SRC,
                               PCL_PORT_CMP_OPERATOR_GTE,
                               rule_params->src_ip_port,
                               rule_ix,
                               &src_cmp_ix[0]));
        FR(pcl_add_port_cmp_ix(devs[d],
                               dest,
                               rule_params->proto,
                               PCL_IP_PORT_TYPE_SRC,
                               PCL_PORT_CMP_OPERATOR_LTE,
                               rule_params->src_ip_port_max,
                               rule_ix,
                               &src_cmp_ix[1]));
      }
      if (!rule_params->dst_ip_port_single) {
        FR(pcl_add_port_cmp_ix(devs[d],
                               dest,
                               rule_params->proto,
                               PCL_IP_PORT_TYPE_DST,
                               PCL_PORT_CMP_OPERATOR_GTE,
                               rule_params->dst_ip_port,
                               rule_ix,
                               &dst_cmp_ix[0]));
        FR(pcl_add_port_cmp_ix(devs[d],
                               dest,
                               rule_params->proto,
                               PCL_IP_PORT_TYPE_DST,
                               PCL_PORT_CMP_OPERATOR_LTE,
                               rule_params->dst_ip_port_max,
                               rule_ix,
                               &dst_cmp_ix[1]));
      }
    }

    if (!user_acl_fake_mode) {
      CPSS_DXCH_PCL_ACTION_STC act;
      memset(&act, 0, sizeof(act));
      FR(set_pcl_action(rule_ix, rule_action, rule_params->trap_action, &act));

      CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
      memset(&rule, 0, sizeof(rule));
      memset(&mask, 0, sizeof(mask));

      switch (dest) {
        case PCL_DEST_INGRESS:
          set_ipv6_rule(devs[d],
                        rule,
                        mask,
                        ruleExtIpv6L4,
                        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_IPV6_L4_E,
                        pcl_id,
                        act,
                        rule_params,
                        rule_ix,
                        src_cmp_ix,
                        dst_cmp_ix,
                        udb[2]);
          break;
        case PCL_DEST_EGRESS:
          set_ipv6_rule(devs[d],
                        rule,
                        mask,
                        ruleEgrExtIpv6L4,
                        CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_IPV6_L4_E,
                        pcl_id,
                        act,
                        rule_params,
                        rule_ix,
                        src_cmp_ix,
                        dst_cmp_ix,
                        commonExt.egrTcpUdpPortComparator);
          break;
        default:
          result = ST_BAD_VALUE;
          goto out;
      };
    }
  }
out:
  PRINT_SEPARATOR('=', 80);
  return result;
}

enum status
pcl_default_rule_set (struct pcl_interface interface,
                      pcl_dest_t           dest,
                      pcl_default_action_t default_action)
{
  PRINT_SEPARATOR('=', 80);
  DEBUG("\n"
        "%s (\n"
        "  interface.type = %d,\n"
        "  interface.num  = %d,\n"
        "  dest           = %d,\n"
        "  default_action = %d)\n",
        __FUNCTION__,
        interface.type,
        interface.num,
        dest,
        default_action);

  enum status             result = ST_OK;
  struct rule_binding_key bind_key;
  struct port             *port = NULL;
  int                     d;

  memset(bind_key.name, 0, NAME_SZ);
  bind_key.num         = 0x00; /* default */
  bind_key.interface   = interface;
  bind_key.destination = dest;

  int devs[NDEVS];
  memset(devs, 0, sizeof(int)*NDEVS);
  int      dev_count = 0;
  uint16_t pcl_id    = 0;

  switch (interface.type) {
    case PCL_INTERFACE_TYPE_VLAN:
      FR(pcl_add_vlan_pcl_id(interface.num));
      pcl_id    = curr_acl->vlan_ipcl_id[interface.num];
      dev_count = NDEVS;
      for_each_dev(d) { devs[d] = d; };
      break;
    case PCL_INTERFACE_TYPE_PORT:
      FR(get_port_ptr(interface.num, &port));
      switch (dest) {
        case PCL_DEST_INGRESS:
          pcl_id = PORT_IPCL_ID(interface.num);
          break;
        case PCL_DEST_EGRESS:
          pcl_id = PORT_EPCL_ID(interface.num);
          break;
        default:
          result = ST_BAD_VALUE;
          goto out;
      };
      dev_count = 1;
      devs[0]   = port->ldev;
      break;
    default:
      result = ST_BAD_VALUE;
      goto out;
  };

  for (d = 0; d < dev_count; d++) {
    uint16_t rule_ix;

    FR(pcl_add_rule_ix(devs[d], &bind_key, &rule_ix));

    if (!user_acl_fake_mode) {
      CPSS_DXCH_PCL_ACTION_STC act;
      memset(&act, 0, sizeof(act));

      pcl_rule_action_t rule_action;
      switch (default_action) {
        case PCL_DEFAULT_ACTION_DENY:
          rule_action = PCL_RULE_ACTION_DENY;
          break;
        case PCL_DEFAULT_ACTION_PERMIT:
          rule_action = PCL_RULE_ACTION_PERMIT;
          break;
        default:
          result = ST_BAD_VALUE;
          goto out;
      }
      FR(set_pcl_action(rule_ix, rule_action, PCL_RULE_TRAP_ACTION_NONE, &act));

      CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
      memset(&rule, 0, sizeof(rule));
      memset(&mask, 0, sizeof(mask));


      switch (dest) {
        case PCL_DEST_INGRESS:
          CRP(cpssDxChPclRuleSet
              (devs[d],
               CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
               rule_ix,
               0,
               &mask,
               &rule,
               &act));
          break;
        case PCL_DEST_EGRESS:
          CRP(cpssDxChPclRuleSet
              (devs[d],
               CPSS_DXCH_PCL_RULE_FORMAT_EGRESS_EXT_NOT_IPV6_E,
               rule_ix,
               0,
               &mask,
               &rule,
               &act));
          break;
        default:
          result = ST_BAD_VALUE;
          goto out;
      };
    }
  }
out:
  PRINT_SEPARATOR('=', 80);
  return result;
}

void
pcl_reset_rules (struct pcl_interface interface, pcl_dest_t dest)
{
  PRINT_SEPARATOR('=', 80);
  DEBUG("\n"
        "%s (\n"
        "  interface.type = %d,\n"
        "  interface.num  = %d,\n"
        "  dest           = %d)\n",
        __FUNCTION__,
        interface.type,
        interface.num,
        dest);

  int block;
  switch (interface.type) {
    case PCL_INTERFACE_TYPE_VLAN:
      dstack_push(curr_acl->pcl_ids,
                  &curr_acl->vlan_ipcl_id[interface.num],
                  sizeof(uint16_t));
      curr_acl->vlan_ipcl_id[interface.num] = 0;
      block = 1;
      break;
    case PCL_INTERFACE_TYPE_PORT:
      block = 0;
      break;
    default:
      return;
  };

  int d;
  for_each_dev(d) {
    struct rule_binding *bind, *tmp;
    HASH_ITER(hh, curr_acl->bindings[d], bind, tmp) {
      if (!memcmp(&bind->key.interface, &interface, sizeof(interface)) &&
          bind->key.destination == dest) {
        dstack_push(curr_acl->rules[d], &bind->rule_ix, sizeof(bind->rule_ix));
        dstack_rev_sort2(curr_acl->rules[d], INT_CMP(uint16_t));

        struct port_cmp *port_cmp, *tmp;
        HASH_ITER(hh, curr_acl->port_cmps[d], port_cmp, tmp) {
          struct rule_ix_list *elem = NULL;
          DL_SEARCH_SCALAR(port_cmp->rule_idxs, elem, rule_ix, bind->rule_ix);
          if (elem) {
            DL_DELETE(port_cmp->rule_idxs, elem);
            if (!port_cmp->rule_idxs) {
              dstack_push(curr_acl->port_cmp_idxs[d],
                          &port_cmp->cmp_ix,
                          sizeof(uint8_t));
              dstack_rev_sort2(curr_acl->port_cmp_idxs[d], INT_CMP(uint8_t));

              HASH_DEL(curr_acl->port_cmps[d], port_cmp);
              free(port_cmp);
            }
          }
        }

        inactivate_rule(d, CPSS_PCL_RULE_SIZE_EXT_E, bind->rule_ix);

        CPSS_DXCH_CNC_COUNTER_STC pcl_counter;
        memset(&pcl_counter, 0, sizeof(pcl_counter));

        CRP (cpssDxChCncCounterSet
             (d,
              block,
              bind->rule_ix,
              CPSS_DXCH_CNC_COUNTER_FORMAT_MODE_0_E,
              &pcl_counter));

        HASH_DEL(curr_acl->bindings[d], bind);
        free(bind);
      }
    }
  }
  PRINT_SEPARATOR('=', 80);
}

enum status
pcl_get_counter (struct pcl_interface interface,
                 pcl_dest_t           dest,
                 char                 *name,
                 uint8_t              name_len,
                 pcl_rule_num_t       rule_num,
                 uint64_t             *counter)
{
  PRINT_SEPARATOR('=', 80);
  DEBUG("\n"
        "%s (\n"
        "  interface.type = %d,\n"
        "  interface.num  = %d,\n"
        "  dest           = %d,\n"
        "  name           = %s,\n"
        "  rule_num       = %llu)\n",
        __FUNCTION__,
        interface.type,
        interface.num,
        dest,
        name,
        rule_num);

  enum status             result = ST_OK;
  struct rule_binding_key bind_key;
  struct port             *port = NULL;
  int                     d;

  if (name_len > NAME_SZ) {
    result = ST_BAD_VALUE;
    goto out;
  }

  memset(bind_key.name, 0, NAME_SZ);
  memcpy(bind_key.name, name, name_len);
  bind_key.num         = rule_num;
  bind_key.interface   = interface;
  bind_key.destination = dest;

  int devs[NDEVS];
  memset(devs, 0, sizeof(int)*NDEVS);
  int     dev_count = 0;
  uint8_t block;

  switch (interface.type) {
    case PCL_INTERFACE_TYPE_VLAN:
      dev_count = NDEVS;
      for_each_dev(d) { devs[d] = d; };
      block = 1;
      break;
    case PCL_INTERFACE_TYPE_PORT:
      FR(get_port_ptr(interface.num, &port));
      dev_count = 1;
      devs[0]   = port->ldev;
      block = 0;
      break;
    default:
      result = ST_BAD_VALUE;
      goto out;
  };

  *counter = 0;
  for (d = 0; d < dev_count; d++) {
    uint16_t rule_ix;
    FR(pcl_find_rule_ix(devs[d], &bind_key, &rule_ix));

    CPSS_DXCH_CNC_COUNTER_STC pcl_counter;
    memset(&pcl_counter, 0, sizeof(pcl_counter));

    CRP (cpssDxChCncCounterGet
         (d,
          block,
          rule_ix,
          CPSS_DXCH_CNC_COUNTER_FORMAT_MODE_0_E,
          &pcl_counter));

    uint64_t packet_count = 0;
    memcpy(&packet_count, &pcl_counter.packetCount, sizeof(packet_count));
    (*counter) += packet_count;
  }
out:
  PRINT_SEPARATOR('=', 80);
  return result;
}

enum status
pcl_clear_counter (struct pcl_interface interface,
                   pcl_dest_t           dest,
                   char                 *name,
                   uint8_t              name_len,
                   pcl_rule_num_t       rule_num)
{
  PRINT_SEPARATOR('=', 80);
  DEBUG("\n"
        "%s (\n"
        "  interface.type = %d,\n"
        "  interface.num  = %d,\n"
        "  dest           = %d,\n"
        "  name           = %s,\n"
        "  rule_num       = %llu)\n",
        __FUNCTION__,
        interface.type,
        interface.num,
        dest,
        name,
        rule_num);

  enum status             result = ST_OK;
  struct rule_binding_key bind_key;
  struct port             *port = NULL;
  int                     d;

  if (name_len > NAME_SZ) {
    result = ST_BAD_VALUE;
    goto out;
  }

  memset(bind_key.name, 0, NAME_SZ);
  memcpy(bind_key.name, name, name_len);
  bind_key.num         = rule_num;
  bind_key.interface   = interface;
  bind_key.destination = dest;

  int devs[NDEVS];
  memset(devs, 0, sizeof(int)*NDEVS);
  int     dev_count = 0;
  uint8_t block;

  switch (interface.type) {
    case PCL_INTERFACE_TYPE_VLAN:
      dev_count = NDEVS;
      for_each_dev(d) { devs[d] = d; };
      block = 1;
      break;
    case PCL_INTERFACE_TYPE_PORT:
      FR(get_port_ptr(interface.num, &port));
      dev_count = 1;
      devs[0]   = port->ldev;
      block = 0;
      break;
    default:
      result = ST_BAD_VALUE;
      goto out;
  };

  for (d = 0; d < dev_count; d++) {
    uint16_t rule_ix;
    FR(pcl_find_rule_ix(devs[d], &bind_key, &rule_ix));

    CPSS_DXCH_CNC_COUNTER_STC pcl_counter;
    memset(&pcl_counter, 0, sizeof(pcl_counter));

    CRP (cpssDxChCncCounterSet
         (d,
          block,
          rule_ix,
          CPSS_DXCH_CNC_COUNTER_FORMAT_MODE_0_E,
          &pcl_counter));
  }
out:
  PRINT_SEPARATOR('=', 80);
  return result;
}

/******************************************************************************/
/* Stack Mail TRAP                                                            */
/******************************************************************************/

static void
pcl_setup_stackmail_trap (port_id_t pid) {

  assert(PORT_STACK_ROLE(pid - 1) != PSR_NONE);

  struct port *port = port_ptr (pid);

  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  CPSS_DXCH_PCL_ACTION_STC act;

  memset (&mask, 0, sizeof (mask));
  memset(mask.ruleExtNotIpv6.macDa.arEther, 0xff, 6);
  mask.ruleExtNotIpv6.macDa.arEther[5] = 0xFE;
  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;

  mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
  mask.ruleExtNotIpv6.common.isIp = 0xff;
  mask.ruleExtNotIpv6.l2Encap = 0xff;
  mask.ruleExtNotIpv6.etherType = 0xffff;

  memset (&rule, 0, sizeof (rule));
  rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pid);
  rule.ruleExtNotIpv6.common.isL2Valid = 1;
  rule.ruleExtNotIpv6.common.isIp = 0;
  rule.ruleExtNotIpv6.l2Encap = 1;
  rule.ruleExtNotIpv6.etherType = ETH_TYPE_STACK;

  memcpy(rule.ruleExtNotIpv6.macDa.arEther, mac_sec, 6);

  memset (&act, 0, sizeof (act));
  act.pktCmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
  act.actionStop = GT_TRUE;
  act.mirror.cpuCode = (PORT_STACK_ROLE(pid - 1) == PSR_PRIMARY)?
                          CPSS_NET_FIRST_USER_DEFINED_E + 7:
                          CPSS_NET_FIRST_USER_DEFINED_E + 8;

  uint16_t rule_ix =
    (PORT_STACK_ROLE(pid - 1) == PSR_PRIMARY)?
         port_stackmail_trap_primary_index :
         port_stackmail_trap_secondary_index;

  CRP (cpssDxChPclRuleSet
       (port->ldev,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
        rule_ix,
        0,
        &mask,
        &rule,
        &act));
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
            port_arp_inspector_trap_ix[pi],
            0,
            &mask,
            &rule,
            &act));

    } else
        CRP (cpssDxChPclRuleInvalidate
             (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, port_arp_inspector_trap_ix[pi]));

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

    CRP (cpssDxChPclRuleSet
         (port->ldev,
          CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
          port_lbd_rule_ix [pid],
          0,
          &mask,
          &rule,
          &act));
  } else
      CRP (cpssDxChPclRuleInvalidate
           (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, port_lbd_rule_ix[pid]));

  return ST_OK;
}

static char LLDP_MAC[6] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e };

enum status
pcl_enable_lldp_trap (port_id_t pid, int enable)
{
  DEBUG("%s", __FUNCTION__);
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
    memset(mask.ruleExtNotIpv6.macDa.arEther, 0xff, 6);

    memset (&rule, 0, sizeof (rule));
    rule.ruleExtNotIpv6.common.pclId = PORT_IPCL_ID (pid);
    rule.ruleExtNotIpv6.common.isL2Valid = 1;
    rule.ruleExtNotIpv6.etherType = 0x88CC;
    rule.ruleExtNotIpv6.l2Encap = 1;
    memcpy(rule.ruleExtNotIpv6.macDa.arEther, LLDP_MAC, 6);

    memset (&act, 0, sizeof (act));
    act.pktCmd = CPSS_PACKET_CMD_TRAP_TO_CPU_E;
    act.actionStop = GT_TRUE;
    act.mirror.cpuCode = CPSS_NET_FIRST_USER_DEFINED_E + 4;

    CRP (cpssDxChPclRuleSet
         (port->ldev,
          CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
          port_lldp_rule_ix [pid],
          0,
          &mask,
          &rule,
          &act));
  } else
      CRP (cpssDxChPclRuleInvalidate
           (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, port_lldp_rule_ix[pid]));

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

      CRP (cpssDxChPclRuleSet
           (port->ldev,
            CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
            port_dhcptrap67_rule_ix[pi],
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

      CRP (cpssDxChPclRuleSet
           (port->ldev,
            CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
            port_dhcptrap68_rule_ix[pi],
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
           (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, port_dhcptrap67_rule_ix[pi]));
      CRP (cpssDxChPclRuleInvalidate
           (port->ldev, CPSS_PCL_RULE_SIZE_EXT_E, port_dhcptrap68_rule_ix[pi]));
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
pcl_enable_vlan (vid_t vid, uint16_t pcl_id) {
  DEBUG("%s: vid: %d, pcl_id: %d\n", __FUNCTION__, vid, pcl_id);

  CPSS_INTERFACE_INFO_STC iface = {
    .type    = CPSS_INTERFACE_VID_E,
    .vlanId  = vid
  };

  CPSS_DXCH_PCL_LOOKUP_CFG_STC lc = {
    .enableLookup  = GT_TRUE,
    .pclId         = pcl_id,
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

static void
pcl_init_cnc_counters (int d) {
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

static void
pcl_init_port_comparators (int d) {
  CRP (cpssDxChPclUserDefinedByteSet
       (d,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
        CPSS_DXCH_PCL_PACKET_TYPE_IPV4_TCP_E,
        2, // UDB index
        CPSS_DXCH_PCL_OFFSET_TCP_UDP_COMPARATOR_E,
        0));
  CRP (cpssDxChPclUserDefinedByteSet
       (d,
        CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
        CPSS_DXCH_PCL_PACKET_TYPE_IPV4_UDP_E,
        2, // UDB index
        CPSS_DXCH_PCL_OFFSET_TCP_UDP_COMPARATOR_E,
        0));
}

static uint16_t test_pcl_id  = 0;
static uint16_t test_rule_ix = 0;

void
pcl_test_start (uint16_t pcl_id, uint16_t rule_ix)
{
  test_pcl_id  = pcl_id;
  test_rule_ix = rule_ix;

  pcl_enable_vlan(1, pcl_id);

  CPSS_DXCH_PCL_RULE_FORMAT_UNT mask, rule;
  memset(&mask, 0, sizeof(mask));
  memset(&rule, 0, sizeof(rule));

  rule.ruleExtNotIpv6.common.pclId = pcl_id;
  mask.ruleExtNotIpv6.common.pclId = 0xFFFF;
  rule.ruleExtNotIpv6.common.isL2Valid = 1;
  mask.ruleExtNotIpv6.common.isL2Valid = 0xFF;
  rule.ruleExtNotIpv6.l2Encap = 1;
  mask.ruleExtNotIpv6.l2Encap = 0xFF;

  CPSS_DXCH_PCL_ACTION_STC act;
  memset(&act, 0, sizeof(act));

  act.pktCmd = CPSS_PACKET_CMD_DROP_HARD_E;
  act.matchCounter.enableMatchCount  = GT_TRUE;
  act.matchCounter.matchCounterIndex = rule_ix;
  act.actionStop = GT_TRUE;

  DEBUG("\ncpssDxChPclRuleSet(\n"
        "  %d\n"
        "  %s\n"
        "  %d\n"
        "  %d\n"
        "  <mask>\n"
        "  <rule>\n"
        "  <act>)\n",
        0,
        "CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E",
        rule_ix,
        0);
  /*DEBUG("\n<mask>:\n");
  PRINTHexDump(&mask, sizeof(mask));
  DEBUG("\n<rule>:\n");
  PRINTHexDump(&rule, sizeof(rule));
  DEBUG("\n<act>:\n");
  PRINTHexDump(&act, sizeof(act));*/
  CRP(cpssDxChPclRuleSet
      (0,
       CPSS_DXCH_PCL_RULE_FORMAT_INGRESS_EXT_NOT_IPV6_E,
       rule_ix,
       0,
       &mask,
       &rule,
       &act));
}

void
pcl_test_iter ()
{
  CPSS_DXCH_CNC_COUNTER_STC pcl_counter;
  memset(&pcl_counter, 0, sizeof(pcl_counter));

  DEBUG("cpssDxChCncCounterGet(%d, %d, %d)\n", 0, 1, test_rule_ix);
  CRP (cpssDxChCncCounterGet
       (0,
        1,
        test_rule_ix,
        CPSS_DXCH_CNC_COUNTER_FORMAT_MODE_0_E,
        &pcl_counter));

  uint64_t packet_count = -1;
  memcpy(&packet_count, &pcl_counter.packetCount, sizeof(packet_count));
  DEBUG("\npacket_count: %llu\n", packet_count);
}

void
pcl_test_stop ()
{
  CRP(cpssDxChPclRuleInvalidate(0, CPSS_PCL_RULE_SIZE_EXT_E, test_rule_ix));
}

enum status
pcl_cpss_lib_pre_init ()
{
  /* Initialize indexes and etc. */
  initialize_vars();

  /* Initialize stack of VT rules */
  pcl_init_rules();

  /* Initialize User ACL */
  pcl_init_user_acl();

  return ST_OK;
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

  /* Initialize CNC */
  pcl_init_cnc_counters(d);

  /* Initialize TCP/UDP port comparators */
  pcl_init_port_comparators(d);

  if (stack_active() && d == stack_pri_port->ldev)
    pcl_setup_stackmail_trap (stack_pri_port->id);
  if (stack_active() && d == stack_sec_port->ldev)
    pcl_setup_stackmail_trap (stack_sec_port->id);

  pcl_setup_ospf(d);
  pcl_setup_rip(d);

  return ST_OK;
}
