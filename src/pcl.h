#ifndef __PCL_H__
#define __PCL_H__

#include <control-proto.h>

extern enum status pcl_cpss_lib_init (int);
extern enum status pcl_port_setup (port_id_t);
extern enum status pcl_enable_port (port_id_t, int);
extern enum status pcl_enable_vlan (uint16_t vid);

extern enum status free_vlan_pcl_id (vid_t);

extern enum status pcl_enable_lbd_trap (port_id_t, int);
extern enum status pcl_enable_dhcp_trap (int);
extern enum status pcl_setup_vt (port_id_t, vid_t, vid_t, int, int);
extern enum status pcl_remove_vt (port_id_t, vid_t, int);
extern void pcl_port_enable_vt (port_id_t, int);
extern void pcl_port_clear_vt (port_id_t);
extern enum status pcl_enable_mc_drop (port_id_t, int);

extern uint16_t get_port_ip_sourceguard_rule_start_ix (port_id_t);
extern uint16_t get_per_port_ip_sourceguard_rules_count (void);
extern void pcl_source_guard_trap_enable (port_id_t);
extern void pcl_source_guard_trap_disable (port_id_t);
extern void pcl_source_guard_drop_enable (port_id_t);
extern void pcl_source_guard_drop_disable (port_id_t);
extern void pcl_source_guard_rule_set (port_id_t, mac_addr_t, vid_t, ip_addr_t, uint16_t, uint8_t);
extern void pcl_source_guard_rule_unset (port_id_t, uint16_t);
extern int pcl_source_guard_trap_enabled (port_id_t);
extern enum status pcl_enable_arp_trap (int);

enum PCL_ACTION {
  PCL_ACTION_PERMIT = 0,
  PCL_ACTION_DENY   = 1
};

enum PCL_TRAP_ACTION {
  PCL_TRAP_ACTION_LOG_INPUT    = 0,
  PCL_TRAP_ACTION_DISABLE_PORT = 1,
  PCL_TRAP_ACTION_NONE         = 2
};
extern const char* pcl_trap_action_to_str (enum PCL_TRAP_ACTION action);

enum PCL_DESTINATION {
  PCL_DESTINATION_INGRESS = 0,
  PCL_DESTINATION_EGRESS  = 1
};

enum PCL_IP_PORT_TYPE {
  PCL_IP_PORT_TYPE_SRC = 0,
  PCL_IP_PORT_TYPE_DST
};

enum PCL_RULE_TYPE {
  PCL_RULE_TYPE_IP      = 0,
  PCL_RULE_TYPE_MAC     = 1,
  PCL_RULE_TYPE_IPV6    = 2,
  PCL_RULE_TYPE_DEFAULT = 3
};

typedef struct {
  uint8_t value[16];
} ipv6_addr_t;

struct ip_pcl_rule {
  uint32_t  rule_ix;
  uint8_t   action;
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
  uint32_t   rule_ix;          /* 2  */
  uint8_t    action;           /* 3  */
  mac_addr_t src_mac;          /* 9  */
  mac_addr_t src_mac_mask;     /* 15 */
  mac_addr_t dst_mac;          /* 21 */
  mac_addr_t dst_mac_mask;     /* 27 */
  uint16_t   eth_type;         /* 29 */
  uint16_t   eth_type_mask;    /* 31 */
  vid_t      vid;              /* 33 */
  vid_t      vid_mask;         /* 35 */
  uint8_t    cos;              /* 36 */
  uint8_t    cos_mask;         /* 37 */
  uint8_t    trap_action;      /* 38 */
} __attribute__ ((packed));

struct ipv6_pcl_rule {
  uint32_t    rule_ix;
  uint8_t     action;
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

struct default_pcl_rule {
  uint32_t    rule_ix;         /* 2  */
  uint8_t     action;          /* 3  */
} __attribute__ ((packed));

struct pcl_port_cmp_idxs_bind {
  struct pcl_port_cmp_idxs_bind *next;
  struct pcl_port_cmp_idxs_bind *prev;
  uint32_t rule_ix;
  uint8_t  proto;
  uint16_t nums[2];
};

extern uint32_t allocate_user_rule_ix (uint16_t);

extern void free_user_rule_ix (uint16_t, uint32_t);

extern uint8_t check_user_rule_ix_count (uint16_t, uint16_t);

extern enum status pcl_ip_rule_set (uint16_t, struct ip_pcl_rule*,
                                    enum PCL_DESTINATION, int);
extern enum status pcl_mac_rule_set (uint16_t, struct mac_pcl_rule*,
                                     enum PCL_DESTINATION, int);
extern enum status pcl_ipv6_rule_set (uint16_t, struct ipv6_pcl_rule*,
                                      enum PCL_DESTINATION, int);
extern enum status pcl_default_rule_set (uint16_t, struct default_pcl_rule*,
                                         enum PCL_DESTINATION, int);

extern uint64_t pcl_get_counter (uint16_t, uint16_t);
extern void pcl_clear_counter (uint16_t, uint16_t);

#ifndef bool_to_str
#define bool_to_str(value) ((value) ? "true" : "false")
#endif

#ifndef pcl_dest_to_str
#define pcl_dest_to_str(value)                                \
  ((value == PCL_DESTINATION_INGRESS) ? "ingress" :           \
   ((value == PCL_DESTINATION_EGRESS) ? "egress" : "unknown"))
#endif

#ifndef is_tcp_or_udp
#define is_tcp_or_udp(proto) (((proto) == 0x6) || ((proto) == 0x11))
#endif

#ifndef nth_byte
#define nth_byte(n, value) ((uint8_t)(((value) & (0xff << ((n)*8))) >> ((n)*8)))
#endif

#ifndef ip_addr_fmt
#define ip_addr_fmt "%d.%d.%d.%d"
#endif

#ifndef mac_addr_fmt
#define mac_addr_fmt "%02X:%02X:%02X:%02X:%02X:%02X"
#endif

#ifndef ipv6_addr_fmt
#define ipv6_addr_fmt "%X:%X:%X:%X:%X:%X:%X:%X"
#endif

#ifndef ip_addr_to_printf_arg
#define ip_addr_to_printf_arg(ip) (ip)[0],(ip)[1],(ip)[2],(ip)[3]
#endif

#ifndef mac_addr_to_printf_arg
#define mac_addr_to_printf_arg(m)                          \
  ((uint8_t*)&m)[0], ((uint8_t*)&m)[1], ((uint8_t*)&m)[2], \
  ((uint8_t*)&m)[3], ((uint8_t*)&m)[4], ((uint8_t*)&m)[5]
#endif

#ifndef ipv6_addr_to_printf_arg
#define ipv6_addr_to_printf_arg(ip)                                             \
((uint16_t*)&ip)[0],((uint16_t*)&ip)[1],((uint16_t*)&ip)[2],((uint16_t*)&ip)[3],\
((uint16_t*)&ip)[4],((uint16_t*)&ip)[5],((uint16_t*)&ip)[6],((uint16_t*)&ip)[7]
#endif

#endif /* __PCL_H__ */
