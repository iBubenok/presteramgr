#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <control-proto.h>
#include <debug.h>

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
  char name[IFNAMSIZ];
  struct ifaddr *addrs;
  UT_hash_handle hh;
};

static struct iface *ift = NULL;

static const char vlan_if_hdr[] = "pdsa-vlan-";
#define VLAN_IF_HDR_LEN (sizeof (vlan_if_hdr) - 1)

static void
ift_unref (struct iface *iface)
{
  if (iface->addrs)
    return;

  HASH_DEL (ift, iface);
  DEBUG ("delete iface %d (%s)", iface->index, iface->name);
  free (iface);
}

enum status
ift_add (int index, const char *name)
{
  struct iface *iface;

  HASH_FIND_INT (ift, &index, iface);
  if (iface) {
    DEBUG ("iface %d already exists: %s %s", index, name, iface->name);
    return ST_OK;
  }

  if (!strncmp (name, vlan_if_hdr, VLAN_IF_HDR_LEN)) {
    iface = calloc (1, sizeof (*iface));
    iface->index = index;
    strncpy (iface->name, name, IFNAMSIZ);
    HASH_ADD_INT (ift, index, iface);
    DEBUG ("new iface %d (%s)", index, name);
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
  DEBUG ("add addr %d.%d.%d.%d on %s",
         addr[0], addr[1], addr[2], addr[3], iface->name);

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
  DEBUG ("delete addr %d.%d.%d.%d on %s",
         ifaddr->addr[0], ifaddr->addr[1], ifaddr->addr[2], ifaddr->addr[3],
         iface->name);
  ift_unref (iface);

  return ST_OK;
}

enum status
ift_del (int index, const char *name)
{
  struct iface *iface;

  HASH_FIND_INT (ift, &index, iface);
  if (!iface) {
    DEBUG ("iface %d does not exist", index);
    return ST_OK;
  }

  ift_unref (iface);

  return ST_OK;
}
