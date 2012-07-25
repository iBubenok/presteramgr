#ifndef __RTNL_H__
#define __RTNL_H__

#include <control-proto.h>

#include <zmq.h>
#include <czmq.h>

extern int rtnl_open (void);
extern enum status rtnl_control (zmsg_t **msg);

#endif /* __RTNL_H__ */
