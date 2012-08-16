#ifndef __PRESTERAMGR_H__
#define __PRESTERAMGR_H__

#include <cpssdefs.h>
#include <cpss/extServices/os/gtOs/gtGenTypes.h>
#include <sys/types.h>
#include <control-proto.h>


/*
 * init.c
 */
extern void cpss_start (void);

/*
 * event.c
 */
#define EVENT_PUBSUB_EP "inproc://event.notify"
extern void event_init (void);
extern void event_enter_loop (void);

/*
 * mgmt.c
 */
extern int mgmt_init (void);
extern void mgmt_send_frame (GT_U8, GT_U8, const void *, size_t);
extern void mgmt_send_regular_frame (vid_t, const void *, size_t);


#endif /* __PRESTERAMGR_H__ */
