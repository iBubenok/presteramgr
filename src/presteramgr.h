#ifndef __PRESTERAMGR_H__
#define __PRESTERAMGR_H__

#include <cpssdefs.h>

/*
 * init.c
 */
void cpss_start (void);

/*
 * event.c
 */
void event_enter_loop (void);

extern int osPrintSync (const char *, ...);

#endif /* __PRESTERAMGR_H__ */
