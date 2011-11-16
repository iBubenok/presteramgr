#ifndef __CONTROL_H__
#define __CONTROL_H__

#include <cpss/generic/port/cpssPortCtrl.h>
#include <control-proto.h>

extern int control_init (void);
extern int control_start (void);
extern void control_notify_port_state (port_num_t, const CPSS_PORT_ATTRIBUTES_STC *);
extern void control_notify_spec_frame (port_num_t, uint8_t, const unsigned char *, size_t);

#endif /* __CONTROL_H__ */
