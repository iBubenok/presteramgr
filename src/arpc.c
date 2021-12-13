#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <czmq.h>

#include <arpd.h>
#include <arpc.h>
#include <ret.h>
#include <control-proto.h>
#include <stack.h>
#include <mac.h>
#include <debug.h>
#include <utils.h>

#include <sys/types.h>
#include <fcntl.h>

static void *arpd_sock;

static int arpc_sock_ready = 0;

void
arpc_start (void)
{
  arpc_sock_ready = 0;
  int fd = open("/var/tmp/sock.presteramgr",
             O_WRONLY | O_CREAT | O_TRUNC, S_IROTH | S_IRGRP | S_IWGRP | S_IRUSR | S_IWUSR);
  close(fd);
}

void
arpc_connect (void) {

  DEBUG("sbelo arpc_connect\n");
  arpd_sock = zsock_new (ZMQ_PUSH);
  zsock_connect (arpd_sock, ARPD_COMMAND_EP);
  arpc_sock_ready = 1;
}

void
arpc_send_set_mac_addr (const mac_addr_t addr) {
  DEBUG("sbelo arpc_send_set_mac_addr\n");

  if (!arpc_sock_ready)
    return;
  zmsg_t *msg = zmsg_new ();

  arpd_command_t cmd = ARPD_CC_SET_MAC_ADDR;
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, addr, sizeof (mac_addr_t));

  zmsg_send (&msg, arpd_sock);
}

void
arpc_ip_addr_op (const struct gw *gw, arpd_command_t cmd) {
  DEBUG("sbelo arpc_ip_addr_op\n");

  if (!arpc_sock_ready)
    return;
  if (stack_id != master_id) {
    mac_op_opna(gw, cmd);
    return;
  }
  zmsg_t *msg = zmsg_new ();

  zmsg_addmem (msg, &cmd, sizeof (cmd));
  arpd_vid_t vid = gw->vid;
  zmsg_addmem (msg, &vid, sizeof (vid));
  arpd_ip_addr_t ip = gw->addr.u32Ip;
  zmsg_addmem (msg, &ip, sizeof (ip));

  zmsg_send (&msg, arpd_sock);
}

void
ndpc_ip_addr_op (const struct gw_v6 *gw, arpd_command_t cmd) {
  DEBUG("sbelo arpc_ip_addr_op\n");
  if (!arpc_sock_ready)
    return;

  /* Attention! */
  if (stack_id != master_id) {
    // mac_op_opna(gw, cmd); // change
    return;
  }
  zmsg_t *msg = zmsg_new ();

  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, &gw->vid, sizeof (vid_t));
  zmsg_addmem (msg, gw->addr.u32Ip, sizeof (GT_U32) * 4);

  zmsg_send (&msg, arpd_sock);
}

void
arpc_request_addr (const struct gw *gw)
{
  DEBUG("sbelo arpc_request_addr\n");
  arpc_ip_addr_op (gw, ARPD_CC_IP_ADDR_ADD);
}

void
ndpc_request_addr (const struct gw_v6 *gw)
{
  DEBUG("sbelo ndpc_request_addr\n");
  ndpc_ip_addr_op (gw, NDPD_CC_IP_ADDR_ADD);
}

void
arpc_release_addr (const struct gw *gw)
{
  DEBUG("sbelo arpc_release_addr\n");
  arpc_ip_addr_op (gw, ARPD_CC_IP_ADDR_DEL);
}

void
ndpc_release_addr (const struct gw_v6 *gw)
{
  DEBUG("sbelo ndpc_release_addr\n");
  ndpc_ip_addr_op (gw, NDPD_CC_IP_ADDR_DEL);
}

void
arpc_set_mac_addr (arpd_ip_addr_t ip,
                   arpd_vid_t vid,
                   const uint8_t *mac,
                   arpd_vif_id_t vif)
{
DEBUG(">>>>arpc_set_mac_addr (%x, %d, " MAC_FMT ", %x)\n",
    ip, vid, MAC_ARG(mac), vif);
  GT_IPADDR ip_addr;
  GT_ETHERADDR mac_addr;
  struct gw gw;

  ip_addr.u32Ip = ip;
  route_fill_gw (&gw, &ip_addr, vid);
  memcpy (mac_addr.arEther, mac, 6);
  ret_set_mac_addr (&gw, &mac_addr, vif);
}

void
ndpc_set_mac_addr (const uint32_t* ip,
                   ndp_vid_t vid,
                   const uint8_t* mac,
                   ndp_vif_id_t vif)
{
  DEBUG(">>>>ndpc_set_mac_addr (, %d, " MAC_FMT ", %x)\n", vid, MAC_ARG(mac), vif);
  GT_IPV6ADDR ip_addr;
  GT_ETHERADDR mac_addr;
  struct gw_v6 gw;

  memcpy (ip_addr.u32Ip, ip, 16);
  route_ipv6_fill_gw (&gw, &ip_addr, vid);  
  memcpy (mac_addr.arEther, mac, 6);
  ret_ipv6_set_mac_addr(&gw, &mac_addr, vif);
}
