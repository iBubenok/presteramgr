#ifndef __PRESTERAMGR_H__
#define __PRESTERAMGR_H__

/*
 * init.c
 */
extern int just_reset;
extern void cpss_start (void);

/*
 * event.c
 */
#define EVENT_PUBSUB_EP "inproc://event.notify"
#define NOTIFY_QUEUE_EP "inproc://queue.notify"
extern void event_init (void);
extern void event_enter_loop (void);
extern void event_start_notify_thread (void);

#endif /* __PRESTERAMGR_H__ */
