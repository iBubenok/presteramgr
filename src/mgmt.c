#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <pthread.h>

#define NETLINK_PDSA_MGMT 30

#define MAX_PAYLOAD 1024

static int sock;
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

  return sendmsg (sock, &msg, 0);
}

static void *
mgmt_thread (void *unused)
{
  struct sockaddr_nl addr;
  static unsigned char buf [NLMSG_SPACE (MAX_PAYLOAD)];
  struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
  struct msghdr msg;
  struct iovec iov;

  iov.iov_base = buf;
  iov.iov_len = NLMSG_SPACE (MAX_PAYLOAD);

  msg.msg_name = (void *) &addr;
  msg.msg_namelen = sizeof (addr);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  printf ("receiving messages from kernel\n");
  while (1) {
    if (recvmsg (sock, &msg, 0) < 0) {
      perror ("recvmsg");
      break;
    }

    /* TODO: handle messages. */
    printf ("Received message: %s\n", (char *) NLMSG_DATA (nlh));
  }

  return NULL;
}

int
mgmt_init (void)
{
  static struct sockaddr_nl addr;
  const char *str = "XPEH BAM BCEM B POT!";

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

  printf ("Sending message to kernel\n");
  if (mgmt_tx (0, 115, str, strlen (str) + 1) < 0) {
    perror ("mgmt_tx");
    return -1;
  }

  pthread_create (&thread, NULL, mgmt_thread, NULL);

  return 0;
}
