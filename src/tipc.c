#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <linux/tipc.h>

#include <ptipcif.h>
#include <vif.h>
#include <trunk.h>
#include <tipc.h>
#include <stack.h>
#include <log.h>
#include <data.h>
#include <utils.h>
#include <sysdeps.h>
#include <port.h>
#include <mac.h>
#include <string.h>


#define BPDU_IOVLEN   2
#define BPDU_IOV_DATA 1

static int ntf_sock, fdb_tsock;

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
  .addr.nameseq.upper = 64
};

#define PTI_FDB_TYPE (4242)
//#define PTI_FDB_MAX (5000)

struct sockaddr_tipc fdb_dst = {
  .family             = AF_TIPC,
  .addrtype           = TIPC_ADDR_MCAST,
  .addr.nameseq.type  = PTI_FDB_TYPE,
  .addr.nameseq.lower = 0,
  .addr.nameseq.upper = 15
};

static void
tipc_notify_init (void)
{
  ntf_sock = socket (AF_TIPC, SOCK_RDM, 0);
  if (ntf_sock < 0)
    errex ("socket() failed");

  fdb_tsock = socket (AF_TIPC, SOCK_RDM, 0);
  if (fdb_tsock < 0)
    errex ("socket() failed");

  bpdu_hdr.dev = stack_id;
}

void
tipc_notify_bpdu (vif_id_t vif_id, port_id_t pid, vid_t vid, bool_t tagged, size_t len, void *data)
{
  bpdu_hdr.iid = pid;
  bpdu_hdr.vif_id = vif_id;
  bpdu_hdr.vid = vid;
  bpdu_hdr.tagged = tagged;
  bpdu_hdr.len = len;

  bpdu_iov[BPDU_IOV_DATA].iov_base = data;
  bpdu_iov[BPDU_IOV_DATA].iov_len  = len;

  if (TEMP_FAILURE_RETRY (sendmsg (ntf_sock, &bpdu_msg, 0)) < 0)
    err ("sendmsg() failed");
}

void
tipc_notify_link (vif_id_t vifid, port_id_t pid, const struct port_link_state *ps)
{
  static uint8_t buf[PTI_LINK_MSG_SIZE (1)];
  struct pti_link_msg *msg = (struct pti_link_msg *) buf;

  msg->dev = stack_id;
  msg->nlinks = 1;
  msg->link[0].iid = pid;
  msg->link[0].vifid = vifid;
  memcpy(&msg->link[0].state, ps, sizeof(*ps));

  if (TEMP_FAILURE_RETRY
      (sendto (ntf_sock, buf, sizeof (buf), 0,
               (struct sockaddr *) &link_dst, sizeof (link_dst)))
      != PTI_LINK_MSG_SIZE (1))
    err ("sendmsg() failed");
}

void
tipc_bc_link_state (void)
{
  static uint8_t buf[PTI_LINK_MSG_SIZE (NPORTS + TRUNK_ID_MAX)];
  struct pti_link_msg *msg = (struct pti_link_msg *) buf;
  int i, c = NPORTS + ((stack_id == master_id)? TRUNK_ID_MAX : 0);

  vif_rlock();

  msg->dev = stack_id;
  msg->nlinks = c;
  for (i = 0; i < NPORTS; i++) {
    msg->link[i].iid = ports[i].id;
    msg->link[i].vifid = ports[i].vif.id;
    memcpy(&msg->link[i].state, &ports[i].vif.state, sizeof(msg->link[i].state));
  }

  if (stack_id == master_id)
    for (i = TRUNK_ID_MIN; i <= TRUNK_ID_MAX; i++) {
      msg->link[NPORTS + i - TRUNK_ID_MIN].iid = 0;
      struct vif *vif = vif_by_trunkid(i);
      msg->link[NPORTS + i - TRUNK_ID_MIN].vifid = vif->id;
      memcpy(&msg->link[NPORTS + i - TRUNK_ID_MIN].state, &vif->state, sizeof(msg->link[i].state));
    }

  vif_unlock();

  if (TEMP_FAILURE_RETRY
      (sendto (ntf_sock, buf, PTI_LINK_MSG_SIZE (c), 0,
               (struct sockaddr *) &link_dst, sizeof (link_dst)))
      != PTI_LINK_MSG_SIZE (c))
    err ("sendmsg() failed");
}

int
tipc_fdbcomm_connect (void) {
  struct sockaddr_tipc taddr;
  int sock = socket(AF_TIPC, SOCK_RDM, 0);
  if (sock < 0) {
    ERR ("tipc socket() for fdbcomm failed");
    exit (EXIT_FAILURE);
  }

  memset(&taddr, 0, sizeof(taddr));
  taddr.family = AF_TIPC;
  taddr.addrtype  = TIPC_ADDR_MCAST;
  taddr.scope = TIPC_CLUSTER_SCOPE;

  taddr.addr.nameseq.type     = PTI_FDB_TYPE;
  taddr.addr.nameseq.lower =  (1);
  taddr.addr.nameseq.upper =  (15);

  if (bind (sock, (struct sockaddr *) &taddr, sizeof (taddr)) < 0) {
    ERR ("tipc socket bind for fdbcomm failed");
    exit (EXIT_FAILURE);
  }
  return sock;
}

void
tipc_start (zctx_t *zcontext)
{
  tipc_notify_init ();

  DEBUG ("TIPC stared \r\n");

}

