#ifndef __PCL_H__
#define __PCL_H__

#include <control-proto.h>

/* test */
extern void pcl_test_start (uint16_t pcl_id, uint16_t rule_ix);
extern void pcl_test_iter ();
extern void pcl_test_stop ();


/* PCL inits */
extern enum status pcl_cpss_lib_pre_init ();
extern enum status pcl_cpss_lib_init (int);
extern enum status pcl_port_setup (port_id_t);
extern enum status pcl_enable_port (port_id_t, int);

/* LBD */
extern enum status pcl_enable_lbd_trap (port_id_t, int);

/* LLDP */
extern enum status pcl_enable_lldp_trap (port_id_t, int);

/* SP */
extern enum status pcl_enable_lacp_trap (port_id_t, int);

extern enum status pcl_enable_cfm_trap (port_id_t, int);

/* DHCP */
extern enum status pcl_enable_dhcp_trap (int);

/* VT */
extern enum status pcl_setup_vt (port_id_t, vid_t, vid_t, int, int);
extern enum status pcl_remove_vt (port_id_t, vid_t, int);
extern void pcl_port_enable_vt (port_id_t, int);
extern void pcl_port_clear_vt (port_id_t);

/* IP Source Guard */
extern uint16_t get_port_ip_sourceguard_rule_start_ix (port_id_t);
extern uint16_t get_per_port_ip_sourceguard_rules_count (void);
extern void pcl_source_guard_trap_enable (port_id_t);
extern void pcl_source_guard_trap_disable (port_id_t);
extern void pcl_source_guard_drop_enable (port_id_t);
extern void pcl_source_guard_drop_disable (port_id_t);
extern void pcl_source_guard_rule_set (port_id_t, mac_addr_t, vid_t, ip_addr_t, bool_t, uint16_t*);
extern void pcl_source_guard_rule_unset (port_id_t, uint16_t);
extern int pcl_source_guard_trap_enabled (port_id_t);
extern bool_t is_free_rule_ix (port_id_t);

/* ARP */
extern enum status pcl_enable_arp_trap (int);

/* User ACL */
typedef struct {
  uint8_t value[16];
} ipv6_addr_t;

struct ip_pcl_rule {
  uint8_t   proto;
  ip_addr_t src_ip;
  ip_addr_t src_ip_mask;
  uint8_t   src_ip_port_single;
  uint16_t  src_ip_port;
  uint16_t  src_ip_port_max;
  uint16_t  src_ip_port_mask;
  ip_addr_t dst_ip;
  ip_addr_t dst_ip_mask;
  uint8_t   dst_ip_port_single;
  uint16_t  dst_ip_port;
  uint16_t  dst_ip_port_max;
  uint16_t  dst_ip_port_mask;
  uint8_t   dscp;
  uint8_t   dscp_mask;
  uint8_t   icmp_type;
  uint8_t   icmp_type_mask;
  uint8_t   icmp_code;
  uint8_t   icmp_code_mask;
  uint8_t   igmp_type;
  uint8_t   igmp_type_mask;
  uint8_t   tcp_flags;
  uint8_t   tcp_flags_mask;
  uint8_t   trap_action;
} __attribute__ ((packed));

struct mac_pcl_rule {
  mac_addr_t src_mac;
  mac_addr_t src_mac_mask;
  mac_addr_t dst_mac;
  mac_addr_t dst_mac_mask;
  uint16_t   eth_type;
  uint16_t   eth_type_mask;
  vid_t      vid;
  vid_t      vid_mask;
  uint8_t    cos;
  uint8_t    cos_mask;
  uint8_t    trap_action;
} __attribute__ ((packed));

struct ipv6_pcl_rule {
  uint8_t     proto;
  ipv6_addr_t src;
  ipv6_addr_t src_mask;
  uint8_t     src_ip_port_single;
  uint16_t    src_ip_port;
  uint16_t    src_ip_port_max;
  uint16_t    src_ip_port_mask;
  ipv6_addr_t dst;
  ipv6_addr_t dst_mask;
  uint8_t     dst_ip_port_single;
  uint16_t    dst_ip_port;
  uint16_t    dst_ip_port_max;
  uint16_t    dst_ip_port_mask;
  uint8_t     dscp;
  uint8_t     dscp_mask;
  uint8_t     icmp_type;
  uint8_t     icmp_type_mask;
  uint8_t     icmp_code;
  uint8_t     icmp_code_mask;
  uint8_t     tcp_flags;
  uint8_t     tcp_flags_mask;
  uint8_t     trap_action;
} __attribute__ ((packed));

#define IP_PROTO_TCP 0x6
#define IP_PROTO_UDP 0x11

#define is_tcp_or_udp(proto) \
  (((proto) == IP_PROTO_TCP) || ((proto) == IP_PROTO_UDP))

extern void
pcl_set_fake_mode_enabled (bool_t);

extern enum status
pcl_ip_rule_set (char                 *name,
                 uint8_t              name_len,
                 pcl_rule_num_t       rule_num,
                 struct pcl_interface interface,
                 pcl_dest_t           dest,
                 pcl_rule_action_t    rule_action,
                 struct ip_pcl_rule   *rule_params);

extern enum status
pcl_mac_rule_set (char                 *name,
                  uint8_t              name_len,
                  pcl_rule_num_t       rule_num,
                  struct pcl_interface interface,
                  pcl_dest_t           dest,
                  pcl_rule_action_t    rule_action,
                  struct mac_pcl_rule  *rule_params);

extern enum status
pcl_ipv6_rule_set (char                 *name,
                   uint8_t              name_len,
                   pcl_rule_num_t       rule_num,
                   struct pcl_interface interface,
                   pcl_dest_t           dest,
                   pcl_rule_action_t    rule_action,
                   struct ipv6_pcl_rule *rule_params);

extern enum status
pcl_default_rule_set (struct pcl_interface interface,
                      pcl_dest_t           dest,
                      pcl_default_action_t default_action);

extern enum status
pcl_get_counter (struct pcl_interface interface,
                 pcl_dest_t           dest,
                 char                 *name,
                 uint8_t              name_len,
                 pcl_rule_num_t       rule_num,
                 uint64_t             *counter);

extern enum status
pcl_clear_counter (struct pcl_interface interface,
                   pcl_dest_t           dest,
                   char                 *name,
                   uint8_t              name_len,
                   pcl_rule_num_t       rule_num);

extern void
pcl_reset_rules (struct pcl_interface interface, pcl_dest_t dest);

#endif /* __PCL_H__ */
