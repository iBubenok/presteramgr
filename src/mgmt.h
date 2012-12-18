#ifndef __MGMT_H__
#define __MGMT_H__

#include <cpssdefs.h>
#include <cpss/extServices/os/gtOs/gtGenTypes.h>
#include <sys/types.h>
#include <control-proto.h>

extern int mgmt_init (void);
extern void mgmt_send_frame (GT_U8, GT_U8, const void *, size_t);
extern void mgmt_send_regular_frame (vid_t, const void *, size_t);
extern void mgmt_inject_frame (vid_t, const void *, size_t);

#endif /* __MGMT_H__ */
