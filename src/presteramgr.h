#ifndef __PRESTERAMGR_H__
#define __PRESTERAMGR_H__

#include <cpssdefs.h>

#include <cpss/extServices/os/gtOs/gtGenTypes.h>

/*
 * init.c
 */
extern void cpss_start (void);

/*
 * event.c
 */
extern void event_enter_loop (void);

/*
 * port.c
 */

extern GT_STATUS port_set_sgmii_mode (GT_U8, GT_U8);

extern int osPrintSync (const char *, ...);

#endif /* __PRESTERAMGR_H__ */
