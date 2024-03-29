#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/pdsa-mgmt.h>
#include <control-proto.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <mgmt.h>
#include <port.h>
#include <control.h>
#include <vlan.h>
#include <stack.h>
#include <dev.h>
#include <debug.h>
#include <log.h>

#define MAX_PAYLOAD 2048

static int sock;
static void *inp_sock;
static pid_t my_pid;
static pthread_t thread;

static int
mgmt_tx (pid_t to, __u16 type, const void *data, size_t len)
{
  struct iovec iov[2];
  static struct msghdr msg;
  struct nlmsghdr nlh;
  struct sockaddr_nl addr;

  nlh.nlmsg_len = NLMSG_SPACE (len);
  nlh.nlmsg_type = type;
  nlh.nlmsg_flags = 0;
  nlh.nlmsg_seq = 0;
  nlh.nlmsg_pid = my_pid;

  iov[0].iov_base = &nlh;
  iov[0].iov_len = NLMSG_HDRLEN;
  iov[1].iov_base = (void *) data;
  iov[1].iov_len = len;

  addr.nl_family = AF_NETLINK;
  addr.nl_pad = 0;
  addr.nl_pid = to;
  addr.nl_groups = 0;

  msg.msg_name = (void *) &addr;
  msg.msg_namelen = sizeof (addr);
  msg.msg_iov = iov;
  msg.msg_iovlen = 2;

  return TEMP_FAILURE_RETRY (sendmsg (sock, &msg, 0));
}

DEFINE_PDSA_MGMT_HANDLER (PDSA_MGMT_SET_VLAN_MAC_ADDR)
{
  zmsg_t *msg = zmsg_new ();
  command_t cmd = CC_VLAN_SET_MAC_ADDR;
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  struct pdsa_vlan_mac_addr *addr = NLMSG_DATA (nlh);
  zmsg_addmem (msg, addr, sizeof (*addr));
  zmsg_send (&msg, inp_sock);

  msg = zmsg_recv (inp_sock);
  zmsg_destroy (&msg);
}

DEFINE_PDSA_MGMT_HANDLER (PDSA_MGMT_SPEC_FRAME_RX)
{
  struct pdsa_spec_frame *frame = NLMSG_DATA (nlh);
/*
  port_id_t pid = port_id (frame->dev, frame->port);

  if (!pid && frame->port != 63) {
    ERR ("invalid port spec %d-%d\n", frame->dev, frame->port);
    return;
  }
*/
  if (PDSA_SPEC_FRAME_SIZE (frame->len) > MAX_PAYLOAD) {
    ERR ("CPU captured oversized for %u bytes buffer frame in %u bytes\n",
        MAX_PAYLOAD - sizeof(struct pdsa_spec_frame), frame->len);
    return;
  }

  control_spec_frame(frame);
  return;
}

static PDSA_MGMT_HANDLERS (handlers) = {
  PDSA_MGMT_HANDLER (PDSA_MGMT_SET_VLAN_MAC_ADDR),
  PDSA_MGMT_HANDLER (PDSA_MGMT_SPEC_FRAME_RX)
};

static void *
mgmt_thread (void *unused)
{
  struct sockaddr_nl addr;
  static unsigned char buf [NLMSG_SPACE (MAX_PAYLOAD)];
  struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
  struct msghdr msg;
  struct iovec iov;

  inp_sock = zsock_new (ZMQ_REQ);
  if (!inp_sock) {
    ERR ("failed to create ZMQ socket %s\n", INP_SOCK_EP);
    exit (1);
  }
  zsock_connect (inp_sock, INP_SOCK_EP);

  iov.iov_base = buf;
  iov.iov_len = NLMSG_SPACE (MAX_PAYLOAD);

  msg.msg_name = (void *) &addr;
  msg.msg_namelen = sizeof (addr);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  DEBUG ("registering pdsa manager\r\n");
  if (mgmt_tx (0, PDSA_MGMT_SET_MGR, NULL, 0) < 0) {
    ERR ("mgmt_tx(): %s\r\n", strerror (errno));
    return NULL;
  }

  DEBUG ("configuring device number %d\r\n", stack_id);
  struct pdsa_dev_num devnum = { .num = stack_id };
  if (mgmt_tx (0, PDSA_MGMT_SET_DEV_NUM, &devnum, sizeof (devnum)) < 0) {
    ERR ("mgmt_tx(): %s\r\n", strerror (errno));
    return NULL;
  }

  prctl(PR_SET_NAME, "mgmt", 0, 0, 0);

  DEBUG ("receiving messages from kernel\r\n");
  while (1) {
    if (TEMP_FAILURE_RETRY (recvmsg (sock, &msg, 0) < 0)) {
      ERR ("recvmsg(): %s\r\n", strerror (errno));
      break;
    }

    if (pdsa_mgmt_invoke_handler (handlers, nlh) < 0)
      ERR ("invalid management command %d from %d\n",
             nlh->nlmsg_type, nlh->nlmsg_pid);
  }

  return NULL;
}

int
mgmt_init (void)
{
  static struct sockaddr_nl addr;

  my_pid = getpid ();

  sock = socket (PF_NETLINK, SOCK_RAW, NETLINK_PDSA_MGMT);
  if (sock < 0) {
    perror ("socket");
    return -1;
  }

  memset (&addr, 0, sizeof (addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_pid = my_pid;
  if (bind (sock, (struct sockaddr*) &addr, sizeof (addr)) < 0) {
    perror ("bind");
    return -1;
  }

  pthread_create (&thread, NULL, mgmt_thread, NULL);

  return 0;
}

void
mgmt_send_frame (GT_U8 dev, GT_U8 port, const void *data, size_t len)
{
  struct pdsa_spec_frame *frame;

  frame = malloc (PDSA_SPEC_FRAME_SIZE (len));
  frame->dev = phys_dev (dev);
  frame->port = port;
  frame->len = len;
  memcpy (frame->data, data, len);

  mgmt_tx (0, PDSA_MGMT_SPEC_FRAME_TX, frame, PDSA_SPEC_FRAME_SIZE (len));

  free (frame);
}

void
mgmt_send_regular_frame (vid_t vid, const void *data, size_t len)
{
  struct pdsa_reg_frame *frame;

  frame = malloc (PDSA_REG_FRAME_SIZE (len));
  frame->vid = vid;
  frame->len = len;
  memcpy (frame->data, data, len);

  mgmt_tx (0, PDSA_MGMT_REG_FRAME_TX, frame, PDSA_REG_FRAME_SIZE (len));

  free (frame);
}

void
mgmt_send_gen_frame (const void *tag, const void *data, size_t len)
{
  struct pdsa_gen_frame *frame;
  size_t full_len = len + 8;

  frame = malloc (PDSA_GEN_FRAME_SIZE (full_len));
  frame->len = full_len;
  memcpy (frame->data, data, 12);
  memcpy (frame->data + 12, tag, 8);
  memcpy (frame->data + 20, data + 12, len - 12);

  mgmt_tx (0, PDSA_MGMT_GEN_FRAME_TX, frame, PDSA_GEN_FRAME_SIZE (full_len));

  free (frame);
}

void
mgmt_inject_frame (vid_t vid, const void *data, size_t len)
{
  struct pdsa_inj_frame *frame;

  frame = malloc (PDSA_INJ_FRAME_SIZE (len));
  frame->iface_type = PDSA_MGMT_IFTYPE_VLAN;
  frame->iface.vid = vid;
  frame->len = len;
  memcpy (frame->data, data, len);

  mgmt_tx (0, PDSA_MGMT_INJECT_FRAME, frame, PDSA_INJ_FRAME_SIZE (len));

  free (frame);
}
