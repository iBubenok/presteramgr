#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#define NETLINK_PDSA_MGMT 30

#define MAX_PAYLOAD 1024

static int sock;
static struct sockaddr_nl src_addr, dst_addr;
static unsigned char _buf [NLMSG_SPACE (MAX_PAYLOAD)];
static struct nlmsghdr *nlh = (struct nlmsghdr *) _buf;
static struct iovec iov;
static struct msghdr msg;

int
mgmt_init (void)
{
  sock = socket (PF_NETLINK, SOCK_RAW, NETLINK_PDSA_MGMT);
  if (sock < 0) {
    perror ("socket");
    return -1;
  }

  memset (&src_addr, 0, sizeof (src_addr));
  src_addr.nl_family = AF_NETLINK;
  src_addr.nl_pid = getpid ();
  bind (sock, (struct sockaddr*) &src_addr, sizeof (src_addr));

  memset (&dst_addr, 0, sizeof (dst_addr));
  dst_addr.nl_family = AF_NETLINK;

  memset (nlh, 0, NLMSG_SPACE (MAX_PAYLOAD));
  nlh->nlmsg_len = NLMSG_SPACE (MAX_PAYLOAD);
  nlh->nlmsg_pid = getpid ();
  nlh->nlmsg_flags = 0;
  strcpy (NLMSG_DATA (nlh), "XPEH BAM B POT!");
  iov.iov_base = (void *) nlh;
  iov.iov_len = nlh->nlmsg_len;
  msg.msg_name = (void *) &dst_addr;
  msg.msg_namelen = sizeof (dst_addr);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  printf ("Sending message to kernel\n");
  sendmsg (sock, &msg, 0);

  recvmsg (sock, &msg, 0);
  printf ("Received message payload: %s\n", (char *) NLMSG_DATA (nlh));

  return 0;
}
