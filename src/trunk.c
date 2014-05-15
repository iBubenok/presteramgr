#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>

#include <cpss/dxCh/dxChxGen/trunk/cpssDxChTrunk.h>

#include <trunk.h>
#include <gif.h>
#include <port.h>
#include <stack.h>
#include <dev.h>
#include <utils.h>
#include <debug.h>

struct trunk trunks[TRUNK_MAX];

static void
trunk_lib_init (void)
{
  CRP (cpssDxChTrunkInit
       (0, TRUNK_MAX, CPSS_DXCH_TRUNK_MEMBERS_MODE_NATIVE_E));
}

void
trunk_init (void)
{
  trunk_lib_init ();
  memset (trunks, 0, sizeof (trunks));
}

enum status
trunk_set_members (trunk_id_t trunk, int nmem, struct trunk_member *mem)
{
  CPSS_TRUNK_MEMBER_STC e[TRUNK_MAX_MEMBERS], d[TRUNK_MAX_MEMBERS];
  int ne, nd, i;
  GT_STATUS rc;

  if (!in_range (trunk, TRUNK_ID_MIN, TRUNK_ID_MAX))
    return ST_BAD_VALUE;

  if (!in_range (nmem, 0, TRUNK_MAX_MEMBERS))
    return ST_BAD_VALUE;

  memset (e, 0, sizeof (e));
  ne = 0;
  memset (d, 0, sizeof (d));
  nd = 0;

  for (i = 0; i < nmem; i++) {
    struct hw_port hp;
    enum status result;

    if (!in_range (mem[i].id.type, GIFT_FE, GIFT_XG))
      return ST_BAD_VALUE;

    result = gif_get_hw_port (&hp, mem[i].id.type, mem[i].id.dev, mem[i].id.num);
    if (result != ST_OK)
      return result;

    if (mem[i].enabled) {
      DEBUG ("e[%d]: %d:%d\r\n", ne, hp.hw_dev, hp.hw_port);
      e[ne].port = hp.hw_port;
      e[ne].device = hp.hw_dev;
      ne++;
    } else {
      DEBUG ("d[%d]: %d:%d\r\n", nd, hp.hw_dev, hp.hw_port);
      d[nd].port = hp.hw_port;
      d[nd].device = hp.hw_dev;
      nd++;
    }
  }

  DEBUG ("trunk %d, ne %d, nd %d\r\n", trunk, ne, nd);
  rc = CRP (cpssDxChTrunkMembersSet (0, trunk, ne, e, nd, d));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}
