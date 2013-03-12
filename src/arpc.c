#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <czmq.h>

#include <arpd.h>
#include <arpc.h>
#include <ret.h>
#include <zcontext.h>

#define ARPD_DOES_NOT_WORK

static void *arpd_sock;

void
arpc_start (void)
{
  arpd_sock = zsocket_new (zcontext, ZMQ_REQ);
  zsocket_connect (arpd_sock, ARPD_COMMAND_EP);
}

static void
arpc_ip_addr_op (const struct gw *gw, arpd_command_t cmd)
{
  zmsg_t *msg = zmsg_new ();

  zmsg_addmem (msg, &cmd, sizeof (cmd));
  arpd_vid_t vid = gw->vid;
  zmsg_addmem (msg, &vid, sizeof (vid));
  arpd_ip_addr_t ip = gw->addr.u32Ip;
  zmsg_addmem (msg, &ip, sizeof (ip));

  zmsg_send (&msg, arpd_sock);

  msg = zmsg_recv (arpd_sock);
  zmsg_destroy (&msg);
}

void
arpc_request_addr (const struct gw *gw)
{
  arpc_ip_addr_op (gw, ARPD_CC_IP_ADDR_ADD);

#ifdef ARPD_DOES_NOT_WORK
  GT_ETHERADDR ea = {
    .arEther = { 0x90, 0x2b, 0x34, 0x54, 0xdb, 0x41 }
  };
  ret_set_mac_addr (gw, &ea, 1);
#endif /* ARPD_DOES_NOT_WORK */
}

void
arpc_release_addr (const struct gw *gw)
{
  arpc_ip_addr_op (gw, ARPD_CC_IP_ADDR_DEL);
}

void
arpc_set_mac_addr (arpd_ip_addr_t ip,
                   arpd_vid_t vid,
                   const uint8_t *mac,
                   arpd_port_id_t pid)
{
  GT_IPADDR ip_addr;
  GT_ETHERADDR mac_addr;
  struct gw gw;

  ip_addr.u32Ip = ip;
  route_fill_gw (&gw, &ip_addr, vid);
  memcpy (mac_addr.arEther, mac, 6);
  ret_set_mac_addr (&gw, &mac_addr, pid);
}
