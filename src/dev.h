#ifndef __DEV_H__
#define __DEV_H__

#include <cpssdefs.h>
#include <cpss/extServices/os/gtOs/gtGenTypes.h>

extern GT_U8 phys_dev (GT_U8);
extern void dev_set_map (GT_U8, GT_U8);

extern uint32_t chip_revision[4];
extern const char* get_rev_str();

#endif /* __DEV_H__ */
