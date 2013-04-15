#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <dev.h>

static GT_U8 dev_map[32];

GT_U8
phys_dev (GT_U8 ldev)
{
  assert (ldev < 32);
  return dev_map[ldev];
}

void
dev_set_map (GT_U8 ldev, GT_U8 pdev)
{
  assert (ldev < 32);
  assert (pdev < 32);
  dev_map[ldev] = pdev;
}
