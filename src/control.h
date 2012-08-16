#ifndef __CONTROL_H__
#define __CONTROL_H__

#include <cpss/generic/port/cpssPortCtrl.h>
#include <control-proto.h>

#define INP_SOCK_EP "inproc://command"
#define INP_PUB_SOCK_EP "inproc://notify"

extern int control_init (void);
extern int control_start (void);

enum control_int_command {
  CC_INT_ROUTE_ADD_PREFIX = CC_MAX,
  CC_INT_ROUTE_DEL_PREFIX,
  CC_INT_SPEC_FRAME_FORWARD,
  CC_INT_MAX
};

#endif /* __CONTROL_H__ */
