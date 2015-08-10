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
  PCL_RULE_TYPE_IPV4 = 2
};

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
};

extern void pcl_ip_rule_set (port_id_t, struct ip_pcl_rule*,
                             enum PCL_DESTINATION, int);
extern void pcl_ip_rule_diff (struct ip_pcl_rule*, struct ip_pcl_rule*);
extern void pcl_mac_rule_set ();
extern void pcl_ipv6_rule_set ();

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

#ifndef ip_addr_to_printf_arg
#define ip_addr_to_printf_arg(ip) (ip)[0], (ip)[1], (ip)[2], (ip)[3]
#endif

#endif /* __PCL_H__ */
