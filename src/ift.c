#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <control-proto.h>
#include <debug.h>
#include <ift.h>

#include <linux/if.h>

#include <uthash.h>


struct ifaddr {
  ip_addr_t addr;
  UT_hash_handle hh;
};
#define HASH_FIND_IP(head, findip, out)                 \
  HASH_FIND (hh, head, findip, sizeof (ip_addr_t), out)
#define HASH_ADD_IP(head, ipfield, add)                 \
  HASH_ADD (hh, head, ipfield, sizeof (ip_addr_t), add)

struct iface {
  int index;
  unsigned flags;
  char name[IFNAMSIZ];
  struct ifaddr *addrs;
  UT_hash_handle hh;
};

static struct iface *ift = NULL;

static const char vlan_if_hdr[] = "pdsa-vlan-";
#define VLAN_IF_HDR_LEN (sizeof (vlan_if_hdr) - 1)

static void
ift_ip_addr_op (const struct ifaddr *ifaddr, int add)
{
  zmsg_t *msg = zmsg_new ();

  command_t cmd = add ? CC_MGMT_IP_ADD : CC_MGMT_IP_DEL;
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, ifaddr->addr, sizeof (ifaddr->addr));
  rtnl_control (&msg);
}

static void
ift_if_ip_addrs_op (struct iface *iface, int add)
{
  struct ifaddr *ifaddr, *tmp;

  DEBUG ("%s brought %s, %s addresses",
         iface->name, add ? "up" : "down", add ? "adding" : "deleting");
  HASH_ITER (hh, iface->addrs, ifaddr, tmp) {
    DEBUG ("%d.%d.%d.%d",
           ifaddr->addr[0], ifaddr->addr[1],
           ifaddr->addr[2], ifaddr->addr[3]);
    ift_ip_addr_op (ifaddr, add);
  }
}

enum status
ift_add (const struct ifinfomsg *ifi, const char *name)
{
  struct iface *iface;

  HASH_FIND_INT (ift, &ifi->ifi_index, iface);
  if (iface) {
    if (ifi->ifi_change & IFF_UP)
      ift_if_ip_addrs_op (iface, ifi->ifi_flags & IFF_UP);
    iface->flags = ifi->ifi_flags;
    return ST_OK;
  }

  if (!strncmp (name, vlan_if_hdr, VLAN_IF_HDR_LEN)) {
    iface = calloc (1, sizeof (*iface));
    iface->index = ifi->ifi_index;
    iface->flags = ifi->ifi_flags;
    strncpy (iface->name, name, IFNAMSIZ);
    HASH_ADD_INT (ift, index, iface);
    DEBUG ("add iface %d (%s)", ifi->ifi_index, name);
  }

  return ST_OK;
}

enum status
ift_add_addr (int index, ip_addr_t addr)
{
  struct iface *iface;
  struct ifaddr *ifaddr;

  HASH_FIND_INT (ift, &index, iface);
  if (!iface)
    return ST_OK;

  HASH_FIND_IP (iface->addrs, addr, ifaddr);
  if (ifaddr)
    return ST_OK;

  ifaddr = calloc (1, sizeof (*ifaddr));
  memcpy (ifaddr->addr, addr, sizeof (addr));
  HASH_ADD_IP (iface->addrs, addr, ifaddr);

  if (iface->flags & IFF_UP)
    ift_ip_addr_op (ifaddr, 1);

  DEBUG ("add addr %d.%d.%d.%d on %s",
         ifaddr->addr[0], ifaddr->addr[1], ifaddr->addr[2], ifaddr->addr[3],
         iface->name);

  return ST_OK;
}

enum status
ift_del_addr (int index, ip_addr_t addr)
{
  struct iface *iface;
  struct ifaddr *ifaddr;

  HASH_FIND_INT (ift, &index, iface);
  if (!iface)
    return ST_OK;

  HASH_FIND_IP (iface->addrs, addr, ifaddr);
  if (!ifaddr)
    return ST_OK;

  HASH_DEL (iface->addrs, ifaddr);

  if (iface->flags & IFF_UP)
    ift_ip_addr_op (ifaddr, 0);

  DEBUG ("delete addr %d.%d.%d.%d on %s",
         ifaddr->addr[0], ifaddr->addr[1], ifaddr->addr[2], ifaddr->addr[3],
         iface->name);

  free (ifaddr);

  return ST_OK;
}

enum status
ift_del (const struct ifinfomsg *ifi, const char *name)
{
  struct iface *iface;
  struct ifaddr *ifaddr, *tmp;

  HASH_FIND_INT (ift, &ifi->ifi_index, iface);
  if (!iface)
    return ST_OK;

  HASH_DEL (ift, iface);

  if (iface->flags & IFF_UP)
    ift_if_ip_addrs_op (iface, 0);

  HASH_ITER (hh, iface->addrs, ifaddr, tmp) {
    HASH_DEL (iface->addrs, ifaddr);
    free (ifaddr);
  }

  DEBUG ("delete iface %s", iface->name);

  free (iface);

  return ST_OK;
}
