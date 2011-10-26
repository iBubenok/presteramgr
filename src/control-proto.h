#ifndef __CONTROL_PROTO_H__
#define __CONTROL_PROTO_H__

#include <stdint.h>

#define PUB_SOCK_EP "ipc://tmp/presteramgr.notify"

enum control_notification {
  CN_PORT_STATE,
  CN_MAX /* NB: must be the last. */
};

enum control_port_speed {
  PORT_SPEED_10,
  PORT_SPEED_100,
  PORT_SPEED_1000,
  PORT_SPEED_10000,
  PORT_SPEED_12000,
  PORT_SPEED_2500,
  PORT_SPEED_5000,
  PORT_SPEED_13600,
  PORT_SPEED_20000,
  PORT_SPEED_40000,
  PORT_SPEED_16000,
  PORT_SPEED_NA
};

struct control_port_state {
  uint32_t port   : 10;
  uint32_t link   : 1;
  uint32_t speed  : 4;
  uint32_t duplex : 1;
} __attribute__ ((packed));

#endif /* __CONTROL_PROTO_H__ */
