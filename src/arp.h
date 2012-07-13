#ifndef __ARP_H__
#define __ARP_H__

#include <control-proto.h>

extern int arp_start (void);
extern enum status arp_send_req (vid_t, const ip_addr_t);

#endif /* __ARP_H__ */
