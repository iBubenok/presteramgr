#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/pdsa.h>
#include <pdsa.h>
#include <debug.h>
#include <log.h>


static struct nl_sock *sock;
static int family;

enum status
pdsa_init (void)
{
  sock = nl_socket_alloc ();
  genl_connect (sock);
  family = genl_ctrl_resolve (sock, PDSA_CONTROL_GENL_FAMILY_NAME);

  return ST_OK;
}

enum status
pdsa_vlan_if_op (vid_t vid, int add)
{
  struct nl_msg *msg;
  int rc;

  msg = nlmsg_alloc ();
  genlmsg_put (msg, NL_AUTO_PID, NL_AUTO_SEQ,
               family, PDSA_CONTROL_HEADER_SIZE,
               NLM_F_REQUEST | NLM_F_ACK,
               add ? PDSA_C_ADD_VLAN : PDSA_C_DEL_VLAN,
               PDSA_CONTROL_VERSION);
  nla_put_string (msg, PDSA_A_IFNAME, "mux0");
  nla_put_u16 (msg, PDSA_A_VID, (__u16) vid);

  rc = nl_send_sync (sock, msg);
  DEBUG ("nl_send_sync() returned %s (%d)\r\n", strerror (-rc), rc);

  return ST_OK;
}
