#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <log.h>
#include <rtnl.h>
#include <zcontext.h>
#include <control.h>
#include <route.h>

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include <ift.h>

#define MAX_PAYLOAD (1024 * 1024)

static int fd;
static pthread_t thread;
static void *inp_sock;

static inline __u32
nl_mgrp (__u32 group)
{
  if (group > 31 ) {
    CRIT ("use setsockopt for group %d", group);
    exit (-1);
  }

  return group ? (1 << (group - 1)) : 0;
}

#ifndef NDA_RTA
#define NDA_RTA(r) \
	((struct rtattr*) (((char*) (r)) + NLMSG_ALIGN (sizeof (struct ndmsg))))
#endif

enum status
rtnl_control (zmsg_t **msg)
{
  zmsg_send (msg, inp_sock);

  zmsg_t *rep = zmsg_recv (inp_sock);
  zmsg_destroy (&rep);

  return ST_OK;
}

static int
parse_rtattr (struct rtattr *tb[], int max, struct rtattr *rta, int len)
{
  memset (tb, 0, sizeof (struct rtattr *) * (max + 1));
  while (RTA_OK (rta, len)) {
    if ((rta->rta_type <= max) && (!tb[rta->rta_type]))
      tb[rta->rta_type] = rta;
    rta = RTA_NEXT (rta,len);
  }
  if (len)
    ERR ("deficit %d, rta_len=%d\n", len, rta->rta_len);
  return 0;
}

static void
rtnl_handle_link (struct nlmsghdr *nlh, int add)
{
  struct ifinfomsg *ifi = NLMSG_DATA (nlh);
  struct rtattr *a[IFLA_MAX+1];

  if (nlh->nlmsg_len < NLMSG_LENGTH (sizeof (*ifi))) {
    ERR ("invalid nlmsg len %d", nlh->nlmsg_len);
    return;
  }

  parse_rtattr (a, IFLA_MAX, IFLA_RTA (ifi), IFLA_PAYLOAD (nlh));
  if (!a[IFLA_IFNAME])
    return;

  if (add)
    ift_add (ifi, (char *) RTA_DATA (a[IFLA_IFNAME]));
  else
    ift_del (ifi, (char *) RTA_DATA (a[IFLA_IFNAME]));
}

static void
rtnl_handle_route (struct nlmsghdr *nlh, int add)
{
  struct rtmsg *r = NLMSG_DATA (nlh);
  struct rtattr *a[RTA_MAX+1];
  struct route rt;

  if (nlh->nlmsg_len < NLMSG_LENGTH (sizeof (*r))) {
    ERR ("invalid nlmsg len %d", nlh->nlmsg_len);
    return;
  }

  parse_rtattr (a, RTA_MAX, RTM_RTA (r),
                nlh->nlmsg_len - NLMSG_LENGTH (sizeof (*r)));

  if (!a[RTA_GATEWAY] ||
      RTA_PAYLOAD (a[RTA_GATEWAY]) != 4 ||
      !a[RTA_OIF])
    return;

  memset (&rt, 0, sizeof (rt));
  memcpy (&rt.gw, RTA_DATA (a[RTA_GATEWAY]), 4);
  rt.ifindex = *((int *) RTA_DATA (a[RTA_OIF]));
  if (a[RTA_DST] && RTA_PAYLOAD (a[RTA_DST]) == 4) {
    memcpy (&rt.pfx.addr, RTA_DATA (a[RTA_DST]), 4);
    rt.pfx.alen = r->rtm_dst_len;
  }

  zmsg_t *msg = zmsg_new ();

  command_t cmd = add ? CC_INT_ROUTE_ADD_PREFIX : CC_INT_ROUTE_DEL_PREFIX;
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, &rt, sizeof (rt));
  rtnl_control (&msg);
}

static void
rtnl_handle_addr (struct nlmsghdr *nlh, int add)
{
  struct ifaddrmsg *ifa = NLMSG_DATA (nlh);
  struct rtattr *a[IFA_MAX + 1];
  uint8_t *p;

  if (nlh->nlmsg_len < NLMSG_LENGTH (sizeof (*ifa))) {
    ERR ("invalid nlmsg len %d", nlh->nlmsg_len);
    return;
  }

  if (ifa->ifa_family != AF_INET)
    return;

  memset (a, 0, sizeof (a));
  parse_rtattr (a, IFA_MAX, IFA_RTA (ifa),
                nlh->nlmsg_len - NLMSG_LENGTH (sizeof (*ifa)));

  if (!a[IFA_LOCAL])
    a[IFA_LOCAL] = a[IFA_ADDRESS];
  if (!a[IFA_LOCAL])
    return;

  p = RTA_DATA (a[IFA_LOCAL]);
  if (add)
    ift_add_addr (ifa->ifa_index, p);
  else
    ift_del_addr (ifa->ifa_index, p);
}

static void
rtnl_handle_msg (struct nlmsghdr *nlh)
{
  switch (nlh->nlmsg_type) {
  case RTM_NEWROUTE:
    rtnl_handle_route (nlh, 1);
    break;
  case RTM_DELROUTE:
    rtnl_handle_route (nlh, 0);
    break;
  case RTM_NEWADDR:
    rtnl_handle_addr (nlh, 1);
    break;
  case RTM_DELADDR:
    rtnl_handle_addr (nlh, 0);
    break;
  case RTM_NEWLINK:
    rtnl_handle_link (nlh, 1);
    break;
  case RTM_DELLINK:
    rtnl_handle_link (nlh, 0);
    break;
  default:
    break;
  }
}

static void *
rtnl_handler (void *unused)
{
  struct sockaddr_nl addr;
  static unsigned char buf [NLMSG_SPACE (MAX_PAYLOAD)];
  struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
  struct msghdr msg;
  struct iovec iov;
  int rc;

  msg.msg_name = (void *) &addr;
  msg.msg_namelen = sizeof (addr);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  DEBUG ("receiving messages from kernel");
  iov.iov_base = buf;
  while (1) {
    iov.iov_len = NLMSG_SPACE (MAX_PAYLOAD);
    if ((rc = TEMP_FAILURE_RETRY (recvmsg (fd, &msg, 0))) < 0) {
      ERR ("recvmsg(): %s", strerror (errno));
      continue;
    }

    for (nlh = (struct nlmsghdr *) buf; rc >= sizeof (*nlh); ) {
      int len = nlh->nlmsg_len;
      int l = len - sizeof (*nlh);

      if (l < 0 || len > rc) {
        if (!(msg.msg_flags & MSG_TRUNC))
          ERR ("malformed message");
        break;
      }

      rtnl_handle_msg (nlh);
      rc -= NLMSG_ALIGN (len);
      nlh = (struct nlmsghdr*) ((char*) nlh + NLMSG_ALIGN (len));
    }

    if (msg.msg_flags & MSG_TRUNC) {
      INFO ("message truncated");
      continue;
    }

    if (rc)
      ERR ("remnant of size %d", rc);
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
  addr.nl_groups =
    nl_mgrp (RTNLGRP_IPV4_ROUTE) |
    nl_mgrp (RTNLGRP_IPV4_IFADDR) |
    nl_mgrp (RTNLGRP_LINK);
  if (bind (fd, (struct sockaddr*) &addr, sizeof (addr)) < 0) {
    CRIT ("failed to bind rtnetlink socket: %s", strerror (errno));
    goto close_fd;
  }

  assert (zcontext);
  inp_sock = zsocket_new (zcontext, ZMQ_REQ);
  if (!inp_sock) {
    ERR ("failed to create ZMQ socket %s\n", INP_SOCK_EP);
    return -1;
  }
  zsocket_connect (inp_sock, INP_SOCK_EP);

  pthread_create (&thread, NULL, rtnl_handler, NULL);
  DEBUG ("rtnl_open(): all done");
  return 0;

 close_fd:
  close (fd);
 err:
  return 1;
}
