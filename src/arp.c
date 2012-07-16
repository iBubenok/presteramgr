#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_arp.h>

#include <zcontext.h>
#include <control.h>
#include <control-utils.h>
#include <arp.h>
#include <log.h>
#include <route-p.h>

/* static void *inp_sock; */

enum status
arp_send_req (vid_t vid, const ip_addr_t addr)
{
  static const unsigned char mac[] = { 0x00, 0x50, 0x43, 0xA3, 0xDD, 0x5F };
  ip_addr_t ip = { 192, 168, 0, 178 };
  unsigned char buf[256];
  struct arphdr *ah = (struct arphdr*) (buf + ETH_HLEN);
  unsigned char *p = (unsigned char *) (ah + 1);

  memset (buf, 0, sizeof (buf));

  memset (buf, 0xFF, ETH_ALEN);
  memcpy (buf + ETH_ALEN, mac, ETH_ALEN);
  buf[2 * ETH_ALEN] = 0x08;
  buf[2 * ETH_ALEN + 1] = 0x06;

  ah->ar_hrd = htons (ARPHRD_ETHER);
  ah->ar_pro = htons (ETH_P_IP);
  ah->ar_hln = 6;
  ah->ar_pln = 4;
  ah->ar_op  = htons (ARPOP_REQUEST);

  memcpy (p, mac, ETH_ALEN);
  p += ETH_ALEN;
  memcpy (p, ip, 4);
  p += 4;
  memset (p, 0x00, ETH_ALEN);
  p += ETH_ALEN;
  memcpy (p, addr, 4);
  p += 4;

  DEBUG ("where is %d.%d.%d.%d?", addr[0], addr[1], addr[2], addr[3]);

  mgmt_send_regular_frame (vid, buf, p - buf);

  /* zmsg_t *msg = zmsg_new (); */
  /* command_t cmd = CC_SEND_FRAME; */
  /* zmsg_addmem (msg, &cmd, sizeof (cmd)); */
  /* zmsg_addmem (msg, buf, p - buf); */
  /* /\* zmsg_send (&msg, inp_sock); *\/ */

  /* /\* msg = zmsg_recv (inp_sock); *\/ */
  /* zmsg_destroy (&msg); */

  return ST_OK;
}

void
arp_handle_reply (vid_t vid, port_id_t pid, unsigned char *frame, int len)
{
  struct arphdr *ah = (struct arphdr *) (frame + ETH_HLEN);
  unsigned char *p = (unsigned char *) (ah + 1);

  if (ah->ar_pro != htons (ETH_P_IP) ||
      ah->ar_op != htons (ARPOP_REPLY) ||
      ah->ar_hln != 6 ||
      ah->ar_pln != 4) {
    DEBUG ("bad ARP reply");
    return;
  }

  DEBUG ("%d.%d.%d.%d is at %02x:%02x:%02x:%02x:%02x:%02x, port %d",
         p[6], p[7], p[8], p[9], p[0], p[1], p[2], p[3], p[4], p[5], pid);

  struct gw gw;

  memset (&gw, 0, sizeof (gw));
  memcpy (&gw.addr, &p[6], 4);
  gw.vid = vid;

  ret_set_mac_addr (&gw, p, pid);
}

static void *req_sock;
static void *sub_sock;

static int
sub_handler (zloop_t *loop, zmq_pollitem_t *pi, void *sock)
{
  zmsg_t *msg = zmsg_recv (sock);

  DEBUG ("got arp reply!!!\r\n");

  zmsg_destroy (&msg);
  return 0;
}

static void *
arp_thread (void *dummy)
{
  assert (zcontext);

  zloop_t *loop = zloop_new ();

  req_sock = zsocket_new (zcontext, ZMQ_REQ);
  if (!req_sock) {
    ERR ("failed to create ZMQ socket %s\n", INP_SOCK_EP);
    exit (1);
  }
  zsocket_connect (req_sock, INP_SOCK_EP);

  sub_sock = zsocket_new (zcontext, ZMQ_SUB);
  if (!sub_sock) {
    ERR ("failed to create ZMQ socket %s\n", INP_PUB_SOCK_EP);
    exit (1);
  }
  zsocket_connect (sub_sock, INP_PUB_SOCK_EP);
  zmq_setsockopt (sub_sock, ZMQ_UNSUBSCRIBE, "", 0);
  control_notification_subscribe (sub_sock, CN_ARP_REPLY_TO_ME);

  zmq_pollitem_t sub_pi = { sub_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &sub_pi, sub_handler, sub_sock);

  zloop_start (loop);

  return NULL;
}

int
arp_start (void)
{
  pthread_t tid;

  pthread_create (&tid, NULL, arp_thread, NULL);

  DEBUG ("arp_start(): all done");

  return 0;
}

