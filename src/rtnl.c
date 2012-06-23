#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <log.h>
#include <rtnl.h>

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#define MAX_PAYLOAD (1024 * 1024)

static int fd;
static pthread_t thread;

static inline __u32
nl_mgrp (__u32 group)
{
  if (group > 31 ) {
    CRIT ("use setsockopt for group %d", group);
    exit (-1);
  }

  return group ? (1 << (group - 1)) : 0;
}

static void *
rtnl_handler (void *unused)
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

  DEBUG ("receiving messages from kernel");
  while (1) {
    if (recvmsg (fd, &msg, 0) < 0) {
      ERR ("recvmsg(): %s", strerror (errno));
      break;
    }
    INFO ("got message from kernel");
    /* TODO: handle msg. */
  }

  return NULL;
}

int
rtnl_open (void)
{
  struct sockaddr_nl addr;

  fd = socket (AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (fd < 0) {
    CRIT ("failed to open rtnetlink socket: %s", strerror (errno));
    goto err;
  }

  memset (&addr, 0, sizeof (addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_pid = getpid ();
  addr.nl_groups = nl_mgrp (RTNLGRP_NEIGH);
  if (bind (fd, (struct sockaddr*) &addr, sizeof (addr)) < 0) {
    CRIT ("failed to bind rtnetlink socket: %s", strerror (errno));
    goto close_fd;
  }

  pthread_create (&thread, NULL, rtnl_handler, NULL);
  DEBUG ("rtnl_open(): all done");
  return 0;

 close_fd:
  close (fd);
 err:
  return 1;
}
