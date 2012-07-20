#ifndef __ARP_H__
#define __ARP_H__

#include <control-proto.h>

extern int arp_start (void);
extern enum status arp_send_req (vid_t, const ip_addr_t);
extern void arp_handle_reply (vid_t, port_id_t, unsigned char *, int);

#endif /* __ARP_H__ */
