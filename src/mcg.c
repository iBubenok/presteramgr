#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgMc.h>

#include <uthash.h>
#include <debug.h>
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

  HASH_FIND_INT (groups, &key, group);
  if (group)
    return ST_ALREADY_EXISTS;

  group = calloc (1, sizeof (struct mcast_group));
  group->key = key;
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
