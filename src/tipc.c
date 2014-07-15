#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <linux/tipc.h>

#include <ptipcif.h>
#include <tipc.h>
#include <stack.h>
#include <log.h>
#include <data.h>
#include <utils.h>
#include <sysdeps.h>
#include <port.h>


#define BPDU_IOVLEN   2
#define BPDU_IOV_DATA 1

static struct sockaddr_tipc bpdu_dst = {
  .family             = AF_TIPC,
  .addrtype           = TIPC_ADDR_MCAST,
  .addr.nameseq.type  = PTI_BPDU_TYPE,
  .addr.nameseq.lower = 0,
  .addr.nameseq.upper = 31
};

static struct pti_bpdu_hdr bpdu_hdr;

static struct iovec bpdu_iov[BPDU_IOVLEN] = {
  {
    .iov_base = &bpdu_hdr,
    .iov_len  = sizeof (bpdu_hdr)
  }
};

static struct msghdr bpdu_msg = {
  .msg_name    = &bpdu_dst,
  .msg_namelen = sizeof (bpdu_dst),
  .msg_iov     = bpdu_iov,
  .msg_iovlen  = BPDU_IOVLEN
};

static struct sockaddr_tipc link_dst = {
  .family             = AF_TIPC,
  .addrtype           = TIPC_ADDR_MCAST,
  .addr.nameseq.type  = PTI_LINK_TYPE,
  .addr.nameseq.lower = 0,
  .addr.nameseq.upper = 62
};

static int ntf_sock;

static void
tipc_notify_init (void)
{
  ntf_sock = socket (AF_TIPC, SOCK_RDM, 0);
  if (ntf_sock < 0)
    errex ("socket() failed");

  bpdu_hdr.dev = stack_id;
}

void
tipc_notify_bpdu (port_id_t pid, size_t len, void *data)
{
  bpdu_hdr.iid = pid;
  bpdu_hdr.len = len;

  bpdu_iov[BPDU_IOV_DATA].iov_base = data;
  bpdu_iov[BPDU_IOV_DATA].iov_len  = len;

  if (TEMP_FAILURE_RETRY (sendmsg (ntf_sock, &bpdu_msg, 0)) < 0)
    err ("sendmsg() failed");
}

void
tipc_notify_link (port_id_t pid, const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  static uint8_t buf[PTI_LINK_MSG_SIZE (1)];
  struct pti_link_msg *msg = (struct pti_link_msg *) buf;

  msg->dev = stack_id;
  msg->nlinks = 1;
  msg->link[0].iid = pid;
  data_encode_port_state (&msg->link[0].state, attrs);

  if (TEMP_FAILURE_RETRY
      (sendto (ntf_sock, buf, sizeof (buf), 0,
               (struct sockaddr *) &link_dst, sizeof (link_dst)))
      != PTI_LINK_MSG_SIZE (1))
    err ("sendmsg() failed");
}

void
tipc_bc_link_state (void)
{
  static uint8_t buf[PTI_LINK_MSG_SIZE (NPORTS)];
  struct pti_link_msg *msg = (struct pti_link_msg *) buf;
  int i;

  msg->dev = stack_id;
  msg->nlinks = NPORTS;
  for (i = 0; i < NPORTS; i++) {
    msg->link[i].iid = ports[i].id;
    data_encode_port_state (&msg->link[i].state, &ports[i].state.attrs);
  }

  if (TEMP_FAILURE_RETRY
      (sendto (ntf_sock, buf, sizeof (buf), 0,
               (struct sockaddr *) &link_dst, sizeof (link_dst)))
      != PTI_LINK_MSG_SIZE (NPORTS))
    err ("sendmsg() failed");
}

void
tipc_start (void)
{
  tipc_notify_init ();
}

