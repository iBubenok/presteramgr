#ifndef __ARPC_H__
#define __ARPC_H__

#include <arpd.h>
#include <route-p.h>

extern void arpc_start (void);
extern void arpc_connect (void);
extern void arpc_request_addr (const struct gw *);
extern void arpc_release_addr (const struct gw *);
extern void arpc_set_mac_addr (arpd_ip_addr_t, arpd_vid_t, const uint8_t *, arpd_vif_id_t);
extern void arpc_ip_addr_op (const struct gw *, arpd_command_t);
extern void arpc_send_set_mac_addr (const mac_addr_t);

#endif /* __ARPC_H__ */
