#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/config/cpssDxChCfgInit.h>

#include <uthash.h>
#include <mll.h>
#include <debug.h>

static int mll_sp;
static GT_U32 mll_ts;
static int *mll_st;

enum status
mll_init (void)
{
  int i;

  CRP (cpssDxChCfgTableNumEntriesGet
       (0, CPSS_DXCH_CFG_TABLE_MLL_PAIR_E, &mll_ts));
  DEBUG ("MLL table size: %u\n", mll_ts);

  mll_st = malloc (sizeof (*mll_st) * mll_ts);
  for (i = 0; i < mll_ts; i++)
    mll_st[i] = i;
  mll_sp = 0;

  return ST_OK;
}

int
mll_get (void)
{
  if (mll_sp >= mll_ts - 1)
    return -1;

  return mll_st[mll_sp++];
}

int
mll_put (int ix)
{
  if (mll_sp == 0)
    return -1;

  mll_st[--mll_sp] = ix;
  return 0;
}
