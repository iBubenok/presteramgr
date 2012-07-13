#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_arp.h>

#include <zcontext.h>
#include <control.h>
#include <arp.h>
#include <log.h>


static void *inp_sock;

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

int
arp_start (void)
{
  assert (zcontext);
  inp_sock = zsocket_new (zcontext, ZMQ_REQ);
  if (!inp_sock) {
    ERR ("failed to create ZMQ socket %s\n", INP_SOCK_EP);
    return -1;
  }
  zsocket_connect (inp_sock, INP_SOCK_EP);

  DEBUG ("rtnl_open(): all done");
  return 0;
}

