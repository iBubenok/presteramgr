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
#include <mac.h>
#include <string.h>


#define BPDU_IOVLEN   2
#define BPDU_IOV_DATA 1

#define PTI_FDB_VERSION (1)

#define TIPC_MSG_MAX_LEN (11900)

static int ntf_sock, fdb_tsock;
static void *ctl_sock;

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

static struct sockaddr_tipc fdb_dst = {
  .family             = AF_TIPC,
  .addrtype           = TIPC_ADDR_MCAST,
  .addr.nameseq.type  = PTI_FDB_TYPE,
  .addr.nameseq.lower = 0,
  .addr.nameseq.upper = 15
};

struct pti_fdbr_msg {
  uint8_t version;
  uint8_t stack_id;
  uint16_t nfdb;
  struct pti_fdbr data[];
} __attribute__ ((packed));

#define PTI_FDBR_MSG_SIZE(n) \
    (sizeof (struct pti_fdbr_msg) + sizeof (struct pti_fdbr) * (n))

struct fdb_thrd {
  void *fdb_ctl_sock;
  void *tipc_ctl_sock;
  zloop_t *loop;
  int fdb_sock;
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

enum status
tipc_fdb_ctl(unsigned n, const struct pti_fdbr *arg) {
  zmsg_t *msg = zmsg_new ();

  uint8_t cmd = PTI_CMD_FDB_SEND;
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, &n, sizeof (n));
  zmsg_addmem (msg, arg, n * sizeof(*arg));

  zmsg_send (&msg, ctl_sock);

  return ST_OK;
}

static void
tipc_notify_fdb (unsigned n, const struct pti_fdbr *pf, struct fdb_thrd *a) {
  static uint8_t *buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

#define TIPC_FDB_NREC ( (TIPC_MSG_MAX_LEN - sizeof(struct pti_fdbr_msg)) / sizeof(struct pti_fdbr) - 1 )

  int nf = 0;
  do { /* splitting up to TIPC message maximum size */
    fdb_msg->version = PTI_FDB_VERSION;
    fdb_msg->stack_id = stack_id;
    uint16_t nr = (n - nf > TIPC_FDB_NREC)? TIPC_FDB_NREC : n - nf;
    fdb_msg->nfdb = htons(nr);
    memcpy(fdb_msg->data, pf+nf, sizeof(struct pti_fdbr) * nr);

    unsigned i;
    for (i = 0; i < nr; i++)
      fdb_msg->data[i].vid = htons(fdb_msg->data[i].vid);

    size_t msglen = sizeof (struct pti_fdbr_msg) + sizeof (struct pti_fdbr) * nr;

    if (TEMP_FAILURE_RETRY
        (sendto (a->fdb_sock, buf, msglen, 0,
                 (struct sockaddr *) &fdb_dst, sizeof (fdb_dst)))
        != msglen)
      err ("TIPC fdb sendmsg() failed");

    DEBUG("tipc-fdb-msg sent, msglen==%d\n", msglen); //TODO remove
    DEBUG("tipc-fdb-msg nf==%d, fdb_msg->nfdb==%hu\n", nf, nr);
    PRINTHexDump(buf, msglen);

    nf += TIPC_FDB_NREC;
  } while (nf < n );
  DEBUG("tipc-fdb-msg OUT nf==%d, n==%u, TIPC_FDB_NREC==%u\n", nf, n, TIPC_FDB_NREC); // TODO remove
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

static int
tipc_fdb_ctl_handler (zloop_t *loop, zmq_pollitem_t *pi, struct fdb_thrd *a) {
  status_t status = ST_BAD_FORMAT;

  zmsg_t *msg = zmsg_recv (a->tipc_ctl_sock);
  zframe_t *cmdframe = zmsg_first (msg);
  if (!cmdframe)
    goto out;
  uint8_t cmd = *((uint8_t *) zframe_data (cmdframe));

  zframe_t *nframe = zmsg_next (msg);
  if (!nframe)
    goto out;
  unsigned n = *((unsigned *) zframe_data (nframe));

  zframe_t *aframe = zmsg_next (msg);
  if (!aframe)
    goto out;
  struct pti_fdbr *pf = (struct pti_fdbr*) zframe_data (aframe);

  if (cmd != PTI_CMD_FDB_SEND) {
    status = ST_BAD_REQUEST;
    goto out;
  }

  tipc_notify_fdb (n, pf, a);

out:
  zmsg_destroy (&msg);
  return 1;
}

static int
tipc_fdb_handler (zloop_t *loop, zmq_pollitem_t *pi, struct fdb_thrd *a) {
  static char *buf[TIPC_MSG_MAX_LEN];
  ssize_t mlen;
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  mlen = TEMP_FAILURE_RETRY(recv(a->fdb_sock, buf, sizeof(buf), 0));
  if (mlen <= 0) {
    ERR("tipc recv() failed(%s)\r\n", strerror(errno));
    return 0;
  }

  DEBUG("tipc-fdb-msg recvd, len==%d\n", mlen); //TODO remove
  PRINTHexDump(buf, mlen);

  if (fdb_msg->version != PTI_FDB_VERSION){
    return 0;
  }
  if (fdb_msg->stack_id == stack_id)
    return 0;

  fdb_msg->nfdb = ntohs(fdb_msg->nfdb);
  unsigned i;
  for (i = 0; i < fdb_msg->nfdb; i++)
    fdb_msg->data[i].vid = ntohs(fdb_msg->data[i].vid);

  mac_op_foreign_blck(fdb_msg->nfdb, fdb_msg->data, a->fdb_ctl_sock);

  return 1;
}

static inline zloop_fn *
zfn (int (fn) (zloop_t *loop, zmq_pollitem_t *item, struct fdb_thrd *arg)) {
  return (zloop_fn *) fn;
}

static volatile int tipc_thread_started = 0;

static void *
tipc_thread(void *z) {
  struct sockaddr_tipc taddr;
  static struct fdb_thrd ft;
  zctx_t *zcontext = (zctx_t *) z;

  ft.fdb_sock = socket(AF_TIPC, SOCK_RDM, 0);
  if (ft.fdb_sock < 0) {
    ERR ("tipc socket() failed");
    exit (EXIT_FAILURE);
  }

  ft.loop = zloop_new();
  assert(ft.loop);

  memset(&taddr, 0, sizeof(taddr));
  taddr.family = AF_TIPC;
  taddr.addrtype  = TIPC_ADDR_MCAST;
  taddr.scope = TIPC_CLUSTER_SCOPE;

  taddr.addr.nameseq.type     = PTI_FDB_TYPE;
  taddr.addr.nameseq.lower =  (1);
  taddr.addr.nameseq.upper =  (15);

  if (bind (ft.fdb_sock, (struct sockaddr *) &taddr, sizeof (taddr)) < 0) {
    ERR ("tipc socket bind failed");
    exit (EXIT_FAILURE);
  }
  int rc;

  ft.fdb_ctl_sock = zsocket_new (zcontext, ZMQ_REQ);
  assert (ft.fdb_ctl_sock);
  rc=zsocket_connect (ft.fdb_ctl_sock, FDB_CONTROL_EP);

  ft.tipc_ctl_sock = zsocket_new (zcontext, ZMQ_SUB);
  assert (ft.tipc_ctl_sock);
  rc=zmq_setsockopt (ft.tipc_ctl_sock, ZMQ_SUBSCRIBE, NULL, 0);
  rc=zsocket_connect (ft.tipc_ctl_sock, TIPC_POST_EP);

  zmq_pollitem_t pitp = {NULL, ft.fdb_sock, ZMQ_POLLIN};
  zloop_poller(ft.loop, &pitp, zfn (tipc_fdb_handler), &ft);

  zmq_pollitem_t pit = { ft.tipc_ctl_sock, 0, ZMQ_POLLIN };
  rc=zloop_poller(ft.loop, &pit, zfn (tipc_fdb_ctl_handler), &ft);

  tipc_thread_started = 1;
  zloop_start(ft.loop);
  return NULL;
}

void
tipc_start (zctx_t *zcontext)
{
  pthread_t tid;
  tipc_notify_init ();

  ctl_sock = zsocket_new (zcontext, ZMQ_PUB);
  assert (ctl_sock);
  zsocket_bind (ctl_sock, TIPC_POST_EP);

  pthread_create (&tid, NULL, tipc_thread, zcontext);
  DEBUG ("waiting for TIPCThr startup\r\n");
  unsigned n = 0;
  while (!tipc_thread_started) {
    n++;
    usleep (100000);
  }
  DEBUG ("TIPCThr startup finished after %d iteractions\r\n", n);

  DEBUG ("TIPC stared \r\n");

}

