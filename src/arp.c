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
#include <vlan.h>
#include <log.h>
#include <presteramgr.h>
#include <ret.h>
#include <route-p.h>
#include <utils.h>

static void *req_sock;
static void *sub_sock;
static void *ctl_sock;

struct arp_entry {
  struct gw gw;
  mac_addr_t addr;
  port_id_t pid;
  int reqs_sent;
  struct arp_entry *prev, *next;
  UT_hash_handle hh;
};

static struct arp_entry *aes;

static enum status
arp_send_req (vid_t vid, const ip_addr_t addr)
{
  unsigned char buf[256];
  struct arphdr *ah = (struct arphdr*) (buf + ETH_HLEN);
  unsigned char *p = (unsigned char *) (ah + 1);

  memset (buf, 0, sizeof (buf));

  memset (buf, 0xFF, ETH_ALEN);
  vlan_get_mac_addr (vid, buf + ETH_ALEN);
  buf[2 * ETH_ALEN] = 0x08;
  buf[2 * ETH_ALEN + 1] = 0x06;

  ah->ar_hrd = htons (ARPHRD_ETHER);
  ah->ar_pro = htons (ETH_P_IP);
  ah->ar_hln = 6;
  ah->ar_pln = 4;
  ah->ar_op  = htons (ARPOP_REQUEST);

  vlan_get_mac_addr (vid, p);
  p += ETH_ALEN;
  vlan_get_ip_addr (vid, p);
  p += 4;
  memset (p, 0x00, ETH_ALEN);
  p += ETH_ALEN;
  memcpy (p, addr, 4);
  p += 4;

  zmsg_t *msg = zmsg_new ();
  command_t cmd = CC_SEND_FRAME;
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, &vid, sizeof (vid));
  zmsg_addmem (msg, buf, p - buf);
  zmsg_send (&msg, req_sock);
  msg = zmsg_recv (req_sock);
  zmsg_destroy (&msg);

  return ST_OK;
}

struct aelh {
  struct arp_entry *head, *cur;
  int len;
};

static void
aelist_add (struct aelh *h, struct arp_entry *e)
{
  if (!h->head) {
    e->prev = e->next = h->cur = h->head = e;
  } else {
    e->prev = h->head->prev;
    e->next = h->head;
    h->head->prev = e->prev->next = e;
  }

  h->len += 1;
}

static void
aelist_del (struct aelh *h, struct arp_entry *e)
{
  if (h->len == 0)
    return;

  if (h->len == 1) {
    h->head = h->cur = NULL;
  } else {
    if (h->head == e)
      h->head = e->next;
    if (h->cur == e)
      h->cur = e->next;
    e->prev->next = e->next;
    e->next->prev = e->prev;
  }

  h->len -= 1;
  e->prev = e->next = NULL;
}

static struct arp_entry *
aelist_cur (struct aelh *h)
{
  return h->cur;
}

static void
aelist_next (struct aelh *h)
{
  if (h->cur)
    h->cur = h->cur->next;
}

static struct aelh unk = {
  .head = NULL,
  .cur  = NULL,
  .len  = 0
};

static int
arp_fast_timer (zloop_t *loop, zmq_pollitem_t *pi, void *dummy)
{
  int i = 0;

  while (i < 10 && i < unk.len) {
    struct arp_entry *e = aelist_cur (&unk);
    if (!e)
      break;

    /* if (e->reqs_sent >= 3) { */
    /*   aelist_del (&unk, e); */
    /*   continue; */
    /* } */

    DEBUG ("sending req #%d for IP %d.%d.%d.%d on VLAN %d\r\n",
           ++e->reqs_sent, e->gw.addr.arIP[0], e->gw.addr.arIP[1],
           e->gw.addr.arIP[2], e->gw.addr.arIP[3], e->gw.vid);
    arp_send_req (e->gw.vid, e->gw.addr.arIP);

    i += 1;
    aelist_next (&unk);
  }

  return 0;
}


DECLARE_HANDLER (AC_ADD_IP);
DECLARE_HANDLER (AC_DEL_IP);

static cmd_handler_t handlers[] = {
  HANDLER (AC_ADD_IP),
  HANDLER (AC_DEL_IP)
};

static void
arp_reply_handler (zmsg_t *msg)
{
  zframe_t *frame;
  struct gw gw;
  port_id_t pid;

  memset (&gw, 0, sizeof (gw));

  frame = zmsg_next (msg);
  gw.vid = *((vid_t *) zframe_data (frame));

  frame = zmsg_next (msg);
  pid = *((port_id_t *) zframe_data (frame));

  frame = zmsg_next (msg);

  struct arphdr *ah = (struct arphdr *) (zframe_data (frame) + ETH_HLEN);
  unsigned char *p = (unsigned char *) (ah + 1);
  if (ah->ar_pro != htons (ETH_P_IP) ||
      ah->ar_op != htons (ARPOP_REPLY) ||
      ah->ar_hln != 6 ||
      ah->ar_pln != 4)
    return;

  memcpy (gw.addr.arIP, p + ETH_ALEN, 4);

  struct arp_entry *e;
  HASH_FIND_GW (aes, &gw, e);
  if (e) {
    DEBUG ("found entry\r\n");
    e->pid = pid;
    aelist_del (&unk, e);
  }

  zmsg_t *tmp = zmsg_new ();
  command_t cmd = CC_INT_RET_SET_MAC_ADDR;
  zmsg_addmem (tmp, &cmd, sizeof (cmd));
  zmsg_addmem (tmp, &gw, sizeof (gw));
  zmsg_addmem (tmp, &pid, sizeof (pid));
  zmsg_addmem (tmp, p, ETH_ALEN);
  zmsg_send (&tmp, req_sock);
  tmp = zmsg_recv (req_sock);
  zmsg_destroy (&tmp);
}

static void
stp_state_handler (zmsg_t *msg)
{
  zframe_t *frame;
  port_id_t pid;
  stp_state_t state;

  frame = zmsg_next (msg);
  pid = *((port_id_t *) zframe_data (frame));

  frame = zmsg_next (msg);
  state = *((stp_state_t *) zframe_data (frame));

  if (state == STP_STATE_DISCARDING) {
    struct arp_entry *e, *tmp;
    HASH_ITER (hh, aes, e, tmp) {
      if (e->pid == pid) {
        e->pid = 0;
        aelist_add (&unk, e);
      }
    }
  }
}

static int
sub_handler (zloop_t *loop, zmq_pollitem_t *pi, void *sock)
{
  zmsg_t *msg = zmsg_recv (sock);
  zframe_t *frame = zmsg_first (msg);
  notification_t n = *((notification_t *) zframe_data (frame));

  switch (n) {
  case CN_ARP_REPLY_TO_ME:
    arp_reply_handler (msg);
    break;
  case CN_STP_STATE:
    stp_state_handler (msg);
    break;
  default:
    break;
  }

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
  control_notification_subscribe (sub_sock, CN_STP_STATE);
  zmq_pollitem_t sub_pi = { sub_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &sub_pi, sub_handler, sub_sock);

  ctl_sock = zsocket_new (zcontext, ZMQ_REP);
  if (!ctl_sock) {
    ERR ("failed to create ZMQ socket %s\n", ARPD_CTL_EP);
    exit (1);
  }
  zsocket_bind (ctl_sock, ARPD_CTL_EP);
  zmq_pollitem_t ctl_pi = { ctl_sock, 0, ZMQ_POLLIN };
  struct handler_data ctl_hd = { ctl_sock, handlers, ARRAY_SIZE (handlers) };
  zloop_poller (loop, &ctl_pi, control_handler, &ctl_hd);

  zloop_timer (loop, 100, 0, arp_fast_timer, NULL);

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


void *
arp_ctl_connect (void)
{
  void *sock = zsocket_new (zcontext, ZMQ_REQ);
  zsocket_connect (sock, ARPD_CTL_EP);
  return sock;
}

DEFINE_HANDLER (AC_ADD_IP)
{
  ip_addr_t addr;
  vid_t vid;
  enum status result;
  struct gw gw;
  struct arp_entry *entry;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&addr);
  if (result != ST_OK)
    goto out;

  memset (&gw, 0, sizeof (gw));
  memcpy (gw.addr.arIP, addr, sizeof (addr));
  gw.vid = vid;

  HASH_FIND_GW (aes, &gw, entry);
  if (entry) {
    DEBUG ("MAC addr for IP %d.%d.%d.%d on VLAN %d is already in list\r\n",
           addr[0], addr[1], addr[2], addr[3], vid);
    result = ST_OK;
    goto out;
  }

  DEBUG ("adding MAC addr for IP %d.%d.%d.%d on VLAN %d\r\n",
         addr[0], addr[1], addr[2], addr[3], vid);
  entry = calloc (1, sizeof (*entry));
  memcpy (&entry->gw, &gw, sizeof (gw));
  HASH_ADD_GW (aes, gw, entry);
  aelist_add (&unk, entry);

 out:
  report_status (result);
}

enum status
arp_add_ip (void *sock, vid_t vid, const ip_addr_t addr)
{
  zmsg_t *msg = zmsg_new ();
  status_t result;
  command_t cmd = AC_ADD_IP;

  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, &vid, sizeof (vid));
  zmsg_addmem (msg, addr, sizeof (addr));
  zmsg_send (&msg, sock);

  msg = zmsg_recv (sock);
  result = *((status_t *) zframe_data (zmsg_first (msg)));
  zmsg_destroy (&msg);

  return result;
}

DEFINE_HANDLER (AC_DEL_IP)
{
  ip_addr_t addr;
  vid_t vid;
  enum status result;
  struct gw gw;
  struct arp_entry *entry;

  result = POP_ARG (&vid);
  if (result != ST_OK)
    goto out;

  result = POP_ARG (&addr);
  if (result != ST_OK)
    goto out;

  memset (&gw, 0, sizeof (gw));
  memcpy (gw.addr.arIP, addr, sizeof (addr));
  gw.vid = vid;

  HASH_FIND_GW (aes, &gw, entry);
  if (!entry) {
    DEBUG ("no ARPd entry for IP %d.%d.%d.%d on VLAN %d\r\n",
           addr[0], addr[1], addr[2], addr[3], vid);
    result = ST_DOES_NOT_EXIST;
    goto out;
  }

  DEBUG ("deleting ARPd entry for IP %d.%d.%d.%d on VLAN %d\r\n",
         addr[0], addr[1], addr[2], addr[3], vid);
  HASH_DEL (aes, entry);
  aelist_del (&unk, entry);
  free (entry);
  result = ST_OK;

 out:
  report_status (result);
}

enum status
arp_del_ip (void *sock, vid_t vid, const ip_addr_t addr)
{
  zmsg_t *msg = zmsg_new ();
  status_t result;
  command_t cmd = AC_DEL_IP;

  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, &vid, sizeof (vid));
  zmsg_addmem (msg, addr, sizeof (addr));
  zmsg_send (&msg, sock);

  msg = zmsg_recv (sock);
  result = *((status_t *) zframe_data (zmsg_first (msg)));
  zmsg_destroy (&msg);

  return result;
}
