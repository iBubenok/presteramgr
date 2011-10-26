#ifndef __CONTROL_H__
#define __CONTROL_H__

#include <cpss/generic/port/cpssPortCtrl.h>

extern int control_init (void);
extern void control_notify_port_state (int, const CPSS_PORT_ATTRIBUTES_STC *);

#endif /* __CONTROL_H__ */
