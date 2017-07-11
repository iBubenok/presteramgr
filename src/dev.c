#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <dev.h>

static GT_U8 dev_map[32];
uint32_t chip_revision[4];

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

const char* get_rev_str()
{
  int val_0x4c = chip_revision[0] & 0xf;
  int val_0x54 = (chip_revision[2] & 0xc) >> 2;

  if (val_0x4c == 0x2) {
    /* A1 */
    return "A1";
  } else if (val_0x4c == 0x3 && val_0x54 == 0x3) {
    /* B0 */
    return "B0";
  } else if (val_0x4c == 0x3) {
    /* A2 */
    return "A2";
  } else {
    /* Unknown */
    return "UNKNOWN";
  }
}
