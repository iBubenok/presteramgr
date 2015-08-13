#ifndef __PCL_H__
#define __PCL_H__

#include <control-proto.h>

extern enum status pcl_cpss_lib_init (int);
extern enum status pcl_port_setup (port_id_t);
extern enum status pcl_enable_port (port_id_t, int);
extern enum status pcl_enable_lbd_trap (port_id_t, int);
extern enum status pcl_enable_dhcp_trap (int);
extern enum status pcl_setup_vt (port_id_t, vid_t, vid_t, int, int);
extern enum status pcl_remove_vt (port_id_t, vid_t, int);
extern void pcl_port_enable_vt (port_id_t, int);
extern void pcl_port_clear_vt (port_id_t);
extern enum status pcl_enable_mc_drop (port_id_t, int);

extern void pcl_source_guard_trap_enable (port_id_t);
extern void pcl_source_guard_trap_disable (port_id_t);
extern void pcl_source_guard_drop_enable (port_id_t);
extern void pcl_source_guard_drop_disable (port_id_t);
extern void pcl_source_guard_rule_set (port_id_t, mac_addr_t, vid_t, ip_addr_t, uint16_t, uint8_t);
extern void pcl_source_guard_rule_unset (port_id_t, uint16_t);
extern int pcl_source_guard_trap_enabled (port_id_t);

enum PCL_ACTION {
  PCL_ACTION_PERMIT = 0,
  PCL_ACTION_DENY   = 1
};

enum PCL_DESTINATION {
  PCL_DESTINATION_INGRESS = 0,
  PCL_DESTINATION_EGRESS  = 1
};

enum PCL_RULE_TYPE {
  PCL_RULE_TYPE_IP   = 0,
  PCL_RULE_TYPE_MAC  = 1,
  PCL_RULE_TYPE_IPV6 = 2
};

typedef struct {
  uint8_t value[16];
} ipv6_addr_t;

struct ip_pcl_rule {
  uint16_t  rule_ix;           /* 2  */
  uint8_t   action;            /* 3  */
  uint8_t   proto;             /* 4  */
  ip_addr_t src_ip;            /* 8  */
  ip_addr_t src_ip_mask;       /* 12 */
  uint16_t  src_ip_port;       /* 14 */
  uint16_t  src_ip_port_mask;  /* 16 */
  ip_addr_t dst_ip;            /* 20 */
  ip_addr_t dst_ip_mask;       /* 24 */
  uint16_t  dst_ip_port;       /* 26 */
  uint16_t  dst_ip_port_mask;  /* 28 */
  uint8_t   dscp;              /* 29 */
  uint8_t   dscp_mask;         /* 30 */
  uint8_t   icmp_type;         /* 31 */
  uint8_t   icmp_type_mask;    /* 32 */
  uint8_t   icmp_code;         /* 33 */
  uint8_t   icmp_code_mask;    /* 34 */
  uint8_t   igmp_type;         /* 35 */
  uint8_t   igmp_type_mask;    /* 36 */
  uint8_t   tcp_flags;         /* 37 */
  uint8_t   tcp_flags_mask;    /* 38 */
} __attribute__ ((packed));

struct mac_pcl_rule {
  uint16_t   rule_ix;          /* 2  */
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
} __attribute__ ((packed));

struct ipv6_pcl_rule {
  uint16_t    rule_ix;         /* 2  */
  uint8_t     action;          /* 3  */
  uint8_t     proto;           /* 4  */
  ipv6_addr_t src;             /* 20 */
  ipv6_addr_t src_mask;        /* 36 */
  uint16_t    src_ip_port;     /* 38 */
  uint16_t    src_ip_port_mask;/* 40 */
  ipv6_addr_t dst;             /* 56 */
  ipv6_addr_t dst_mask;        /* 72 */
  uint16_t    dst_ip_port;     /* 74 */
  uint16_t    dst_ip_port_mask;/* 76 */
  uint8_t     dscp;            /* 77 */
  uint8_t     dscp_mask;       /* 78 */
  uint8_t     icmp_type;       /* 79 */
  uint8_t     icmp_type_mask;  /* 80 */
  uint8_t     icmp_code;       /* 81 */
  uint8_t     icmp_code_mask;  /* 82 */
  uint8_t     tcp_flags;       /* 83 */
  uint8_t     tcp_flags_mask;  /* 84 */
} __attribute__ ((packed));

extern void pcl_ip_rule_set (port_id_t, struct ip_pcl_rule*,
                             enum PCL_DESTINATION, int);
extern void pcl_mac_rule_set (port_id_t, struct mac_pcl_rule*,
                              enum PCL_DESTINATION, int);
extern void pcl_ipv6_rule_set (port_id_t, struct ipv6_pcl_rule*,
                               enum PCL_DESTINATION, int);

#ifndef bool_to_str
#define bool_to_str(value) ((value) ? "true" : "false")
#endif

#ifndef pcl_dest_to_str
#define pcl_dest_to_str(value)                                \
  ((value == PCL_DESTINATION_INGRESS) ? "ingress" :           \
   ((value == PCL_DESTINATION_EGRESS) ? "egress" : "unknown"))
#endif

#ifndef is_tcp_or_udp
#define is_tcp_or_udp(proto) (((proto) == 0x6) || ((proto) == 0x17))
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
