#ifndef __MONITOR_H__
#define __MONITOR_H__

#include <control-proto.h>

extern enum status mon_session_add (mon_session_t);
extern enum status mon_session_enable (mon_session_t, int);
extern enum status mon_session_del (mon_session_t);

#endif /* __MONITOR_H__ */
