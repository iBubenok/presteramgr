#ifndef __PRESTERAMGR_H__
#define __PRESTERAMGR_H__

#include <cpssdefs.h>
#include <cpss/extServices/os/gtOs/gtGenTypes.h>
#include <sys/types.h>


/*
 * init.c
 */
extern void cpss_start (void);

/*
 * event.c
 */
extern void event_enter_loop (void);

/*
 * mgmt.c
 */
extern int mgmt_init (void);
extern void mgmt_send_frame (GT_U8, GT_U8, const void *, size_t);


#endif /* __PRESTERAMGR_H__ */
