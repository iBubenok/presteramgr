#ifndef __PRESTERAMGR_H__
#define __PRESTERAMGR_H__

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

#endif /* __PRESTERAMGR_H__ */
