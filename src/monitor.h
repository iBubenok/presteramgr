#ifndef __MONITOR_H__
#define __MONITOR_H__

#include <control-proto.h>

extern void mon_cpss_lib_init (int);
extern enum status mon_session_add (mon_session_t);
extern enum status mon_session_set_src (mon_session_t, int, const struct mon_if *);
extern enum status mon_session_set_dst (mon_session_t, struct mon_if *, vid_t);
extern enum status mon_session_enable (mon_session_t, int);
extern enum status mon_session_del (mon_session_t);

#endif /* __MONITOR_H__ */
