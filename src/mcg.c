#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgMc.h>

#include <uthash.h>
#include <debug.h>
#include <port.h>
#include <mcg.h>


struct mcast_group {
  int key;
  CPSS_PORTS_BMP_STC ports;
  UT_hash_handle hh;
};

static struct mcast_group *groups;

enum status
mcg_create (mcg_t mcg)
{
  GT_STATUS rc;
  struct mcast_group *group;
  int key = mcg;
  int i;

  HASH_FIND_INT (groups, &key, group);
  if (group)
    return ST_ALREADY_EXISTS;

  group = calloc (1, sizeof (struct mcast_group));
  group->key = key;
  for (i = 0; i < nports; i++) {
    struct port *port = port_ptr (i + 1);
    if (is_stack_port (port))
      CPSS_PORTS_BMP_PORT_SET_MAC (&group->ports, port->lport);
  }
  rc = CRP (cpssDxChBrgMcEntryWrite (0, mcg, &group->ports));
  if (rc == GT_OK) {
    HASH_ADD_INT (groups, key, group);
    return ST_OK;
  }

  free (group);
  switch (rc) {
  case GT_BAD_PARAM: return ST_BAD_VALUE;
  case GT_HW_ERROR:  return ST_HW_ERROR;
  default:           return ST_HEX;
  }
}

enum status
mcg_delete (mcg_t mcg)
{
  GT_STATUS rc;
  struct mcast_group *group;
  int key = mcg;

  HASH_FIND_INT (groups, &key, group);
  if (!group)
    return ST_DOES_NOT_EXIST;

  rc = CRP (cpssDxChBrgMcGroupDelete (0, mcg));
  if (rc == GT_OK) {
    HASH_DEL (groups, group);
    free (group);
    return ST_OK;
  }

  switch (rc) {
  case GT_BAD_PARAM: return ST_DOES_NOT_EXIST;
  case GT_HW_ERROR:  return ST_HW_ERROR;
  default:           return ST_HEX;
  }
}

enum status
mcg_add_port (mcg_t mcg, port_id_t pid)
{
  GT_STATUS rc;
  struct mcast_group *group;
  struct port *port;
  int key = mcg;

  port = port_ptr (pid);
  if (!port)
    return ST_BAD_VALUE;

  HASH_FIND_INT (groups, &key, group);
  if (!group)
    return ST_DOES_NOT_EXIST;

  if (CPSS_PORTS_BMP_IS_PORT_SET_MAC (&group->ports, port->lport))
    return ST_ALREADY_EXISTS;

  rc = CRP (cpssDxChBrgMcMemberAdd (0, mcg, port->lport));
  if (rc == GT_OK) {
    CPSS_PORTS_BMP_PORT_SET_MAC (&group->ports, port->lport);
    return ST_OK;
  }

  switch (rc) {
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
mcg_del_port (mcg_t mcg, port_id_t pid)
{
  GT_STATUS rc;
  struct mcast_group *group;
  struct port *port;
  int key = mcg;

  port = port_ptr (pid);
  if (!port)
    return ST_BAD_VALUE;

  HASH_FIND_INT (groups, &key, group);
  if (!group)
    return ST_DOES_NOT_EXIST;

  if (!CPSS_PORTS_BMP_IS_PORT_SET_MAC (&group->ports, port->lport))
    return ST_DOES_NOT_EXIST;

  rc = CRP (cpssDxChBrgMcMemberDelete (0, mcg, port->lport));
  if (rc == GT_OK) {
    CPSS_PORTS_BMP_PORT_CLEAR_MAC (&group->ports, port->lport);
    return ST_OK;
  }

  switch (rc) {
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

void
mcg_dgasp_setup (void)
{
  CPSS_PORTS_BMP_STC bmp;

  memset (&bmp, 0, sizeof (bmp));
  CRP (cpssDxChBrgMcEntryWrite (0, DGASP_MCG, &bmp));
}

enum status
mcg_dgasp_port_op (port_id_t pid, int add)
{
  GT_STATUS rc;
  struct port *port;

  port = port_ptr (pid);
  if (!port)
    return ST_BAD_VALUE;

  if (add)
    rc = CRP (cpssDxChBrgMcMemberAdd (0, DGASP_MCG, port->lport));
  else
    rc = CRP (cpssDxChBrgMcMemberDelete (0, DGASP_MCG, port->lport));

  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

void
mcg_stack_setup (void)
{
  CPSS_PORTS_BMP_STC bmp;
  int i;

  memset (&bmp, 0, sizeof (bmp));
  for (i = 0; i < nports; i++) {
    struct port *port = &ports[i];
    if (is_stack_port (port))
      CPSS_PORTS_BMP_PORT_SET_MAC (&bmp, port->lport);
  }
  CRP (cpssDxChBrgMcEntryWrite (0, STACK_MCG, &bmp));
}
