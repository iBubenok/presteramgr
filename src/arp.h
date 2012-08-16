#ifndef __ARP_H__
#define __ARP_H__

#include <control-proto.h>

#define ARPD_CTL_EP "inproc://arpd-ctl"

enum arpd_cmd {
  AC_ADD_IP,
  AC_DEL_IP
};

extern int arp_start (void);
extern void arp_handle_reply (vid_t, port_id_t, unsigned char *, int);

extern void *arp_ctl_connect (void);
extern enum status arp_add_ip (void *, vid_t, const ip_addr_t);
extern enum status arp_del_ip (void *, vid_t, const ip_addr_t);

#endif /* __ARP_H__ */
