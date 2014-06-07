#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>

#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/dgasp.h>
#include <dgasp.h>
#include <vlan.h>
#include <port.h>
#include <mcg.h>
#include <sysdeps.h>
#include <dev.h>
#include <debug.h>

#define MAX_PAYLOAD 1024

static int sock;
static pid_t my_pid;
static uint8_t tag[8];

static int __attribute__ ((unused))
dgasp_tx (pid_t to, __u16 type, const void *data, size_t len)
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
  msg.msg_iovlen = data ? 2 : 1;

  return sendmsg (sock, &msg, 0);
}

int
dgasp_init (void)
{
  static struct sockaddr_nl addr;
  CPSS_DXCH_NET_DSA_PARAMS_STC dp;

  my_pid = getpid ();

  sock = socket (PF_NETLINK, SOCK_RAW, NETLINK_DGASP_MGMT);
  if (sock < 0) {
    perror ("socket");
    return -1;
  }

  memset (&addr, 0, sizeof (addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_pid = my_pid;
  if (bind (sock, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    perror ("bind");
    return -1;
  }

  mcg_dgasp_setup ();

  memset (&dp, 0, sizeof (dp));
  dp.commonParams.dsaTagType = CPSS_DXCH_NET_DSA_TYPE_EXTENDED_E;
  dp.commonParams.vid = SVC_VID;
  dp.dsaType = CPSS_DXCH_NET_DSA_CMD_FROM_CPU_E;
  dp.dsaInfo.fromCpu.dstInterface.type = CPSS_INTERFACE_VIDX_E;
  dp.dsaInfo.fromCpu.dstInterface.vidx = DGASP_MCG;
  dp.dsaInfo.fromCpu.srcId = 0;
  dp.dsaInfo.fromCpu.srcDev = phys_dev (CPU_DEV);
  CRP (cpssDxChNetIfDsaTagBuild (CPU_DEV, &dp, tag));

  return 0;
}

enum status
dgasp_enable (int enable)
{
#if 0
  /* FIXME: this code should depend on board dying gasp support. */
  struct dgasp_enable arg = {
    .enable = enable
  };

  dgasp_tx (0, DGASP_ENABLE, &arg, sizeof (arg));
#endif /* 0 */

  return ST_OK;
}

enum status
dgasp_add_packet (size_t size, const void *data)
{
  struct dgasp_packet *p;
  size_t s = size + 8;

  p = malloc (DGASP_PACKET_SIZE (s));
  p->size = s;
  memcpy (p->data, data, 12);
  memcpy (p->data + 12, tag, 8);
  memcpy (p->data + 20, data + 12, size - 12);

  dgasp_tx (0, DGASP_ADD_PACKET, p, DGASP_PACKET_SIZE (s));
  free (p);
  return ST_OK;
}

enum status
dgasp_clear_packets (void)
{
  dgasp_tx (0, DGASP_CLEAR_PACKETS, NULL, 0);
  return ST_OK;
}

enum status
dgasp_port_op (port_id_t pid, int add)
{
  vlan_svc_enable_port (pid, add);
  return mcg_dgasp_port_op (pid, add);
}

enum status
dgasp_send (void)
{
  dgasp_tx (0, DGASP_SEND, NULL, 0);
  return ST_OK;
}
