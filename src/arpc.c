#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <czmq.h>

#include <arpd.h>
#include <arpc.h>
#include <ret.h>
#include <control-proto.h>
#include <zcontext.h>
#include <debug.h>

#include <sys/types.h>
#include <fcntl.h>

static void *arpd_sock;

void
arpc_start (void)
{
  arpd_sock = zsocket_new (zcontext, ZMQ_PUB);
/*  uint64_t hwm = 1000;
  zmq_setsockopt(arpd_sock, ZMQ_HWM, &hwm, sizeof (hwm)); */
  zsocket_bind (arpd_sock, ARPD_COMMAND_EP);

  int fd = open("/var/tmp/sock.presteramgr",
             O_WRONLY | O_CREAT | O_TRUNC, S_IROTH | S_IRGRP | S_IWGRP | S_IRUSR | S_IWUSR);
  close(fd);
}

void
arpc_send_set_mac_addr (const mac_addr_t addr) {

  zmsg_t *msg = zmsg_new ();

  arpd_command_t cmd = ARPD_CC_SET_MAC_ADDR;
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, addr, sizeof (mac_addr_t));

  zmsg_send (&msg, arpd_sock);
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
}

void
arpc_request_addr (const struct gw *gw)
{
  arpc_ip_addr_op (gw, ARPD_CC_IP_ADDR_ADD);
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
