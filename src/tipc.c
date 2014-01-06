#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <linux/tipc.h>

#include <ptipcif.h>
#include <tipc.h>
#include <stack.h>
#include <log.h>
#include <utils.h>


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

static int bpdu_sock;

static void
tipc_start_bpdu_notify (void)
{
  bpdu_sock = socket (AF_TIPC, SOCK_RDM, 0);
  if (bpdu_sock < 0)
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

  if (TEMP_FAILURE_RETRY (sendmsg (bpdu_sock, &bpdu_msg, 0)) < 0)
    err ("sendmsg() failed");
}

void
tipc_start (void)
{
  tipc_start_bpdu_notify ();
}

