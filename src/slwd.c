#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <linux/tipc.h>
#include <getopt.h>

#include <czmq.h>

#include <control-proto.h>
#include <ptipcif.h>
#include <log.h>
#include <utils.h>

zctx_t *zctx;
void *ntf_sock;
int stack_id = 0;
int tfd;

struct link {
  uint8_t dev;
  uint8_t iid;
  uint32_t vifid;
  uint8_t link;
  uint8_t speed;
  uint8_t duplex;
} __attribute__ ((packed));

static void
ntf_init (void)
{
  zctx = zctx_new ();

  ntf_sock = zsocket_new (zctx, ZMQ_PUSH);
  zsocket_bind (ntf_sock, SLWD_NTF_SOCK_EP);
}

static void
tipc_init (void)
{
  struct sockaddr_tipc addr;

  tfd = socket (AF_TIPC, SOCK_RDM, 0);
  if (tfd < 0) {
    ERR ("socket() failed");
    exit (EXIT_FAILURE);
  }

  memset (&addr, 0, sizeof (addr));
  addr.family                  = AF_TIPC;
  addr.addrtype                = TIPC_ADDR_NAME;
  addr.scope                   = TIPC_CLUSTER_SCOPE;
  addr.addr.name.name.type     = PTI_LINK_TYPE;
  addr.addr.name.name.instance = stack_id + 32;

  if (bind (tfd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    ERR ("bind() failed");
    exit (EXIT_FAILURE);
  }
}

static int
link_handler (zloop_t *loop, zmq_pollitem_t *pi, void *_)
{
  static uint8_t buf[PTI_LINK_MSG_SIZE (64 + 64)];
  struct pti_link_msg *msg = (struct pti_link_msg *) buf;
  ssize_t len;
  int i;

  if ((len = TEMP_FAILURE_RETRY
       (recvfrom (tfd, buf, sizeof (buf), 0, NULL, NULL))) < 0) {
    ERR ("recvmsg() failed (%s)", strerror (errno));
    return 0;
  }

  if (len < PTI_LINK_MSG_SIZE (1)
      || msg->dev > 31
      || msg->nlinks > 64 + 64
      || len != PTI_LINK_MSG_SIZE (msg->nlinks)) {
    ERR ("malformed link message");
    return 0;
  }

  for (i = 0; i < msg->nlinks; i++) {
    struct link link = {
      .dev    = msg->dev,
      .iid    = msg->link[i].iid,
      .vifid  = msg->link[i].vifid,
      .link   = msg->link[i].state.link,
      .speed  = msg->link[i].state.speed,
      .duplex = msg->link[i].state.duplex,
    };

    zmsg_t *ntf = zmsg_new ();
    zmsg_addmem (ntf, &link, sizeof (link));
    zmsg_send (&ntf, ntf_sock);

    if (msg->link[i].state.link) {
      DEBUG ("port %d:%d:%x link up at speed %d, %s duplex",
             msg->dev, msg->link[i].iid, msg->link[i].vifid, msg->link[i].state.speed,
             msg->link[i].state.duplex ? "full" : "half");
    } else
      DEBUG ("port %d:%d:%x link down", msg->dev, msg->link[i].iid, msg->link[i].vifid);
  }

  return 0;
}

int
main (int argc, char **argv)
{
  zloop_t *loop;
  int daemonize = 1, debug = 0;
  char *endptr;

  while (1) {
    int c, option_index = 0;
    static struct option opts[] = {
      {"angel",           no_argument,       NULL, 'a'},
      {"debug",           no_argument,       NULL, 'd'},
      {"stack-id",        required_argument, NULL, 'i'},
      {NULL, 0, NULL, 0}
    };

    c = getopt_long (argc, argv, "adi:", opts, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'a':
      daemonize = 0;
      break;
    case 'd':
      debug = 1;
      break;
    case 'i':
      stack_id = strtol (optarg, &endptr, 10);
      if (*endptr || stack_id > 31) {
        fprintf (stderr, "invalid command line arguments\n");
        exit (EXIT_FAILURE);
      }
      break;
    default:
      fprintf (stderr, "invalid command line arguments\n");
      exit (EXIT_FAILURE);
    }
  }

  if (daemonize)
    daemon (0, 0);

  openlog ("slwd", LOG_CONS | (daemonize ? 0 : LOG_PERROR), LOG_DAEMON);
  setlogmask (LOG_UPTO (debug ? LOG_DEBUG : LOG_INFO));

  loop = zloop_new ();

  ntf_init ();

  tipc_init ();
  zmq_pollitem_t pi = { NULL, tfd, ZMQ_POLLIN };
  zloop_poller (loop, &pi, link_handler, NULL);

  zloop_start (loop);

  return 0;
}
