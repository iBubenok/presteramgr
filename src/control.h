#ifndef __CONTROL_H__
#define __CONTROL_H__

#include <cpss/generic/port/cpssPortCtrl.h>
#include <control-proto.h>

#define INP_SOCK_EP "inproc://command"

extern int control_init (void);
extern int control_start (void);
extern void control_notify_port_state (port_id_t, const CPSS_PORT_ATTRIBUTES_STC *);

enum control_int_command {
  CC_INT_ROUTE_ADD_PREFIX = CC_MAX,
  CC_INT_ROUTE_DEL_PREFIX,
  CC_INT_SPEC_FRAME_FORWARD,
  CC_INT_MAX
};

#endif /* __CONTROL_H__ */
