#ifndef __CONTROL_PROTO_H__
#define __CONTROL_PROTO_H__

#include <zmq.h>
#include <czmq.h>
#include <stdint.h>

#define PUB_SOCK_EP "ipc:///tmp/presteramgr.notify"
#define CMD_SOCK_EP "ipc:///tmp/presteramgr.command"

enum control_notification {
  CN_PORT_LINK_STATE,
  CN_MAX /* NB: must be the last. */
};

static inline int
control_notification_subscribe (void *sock, enum control_notification cn)
{
  uint16_t tmp = cn;
  return zmq_setsockopt (sock, ZMQ_SUBSCRIBE, &tmp, sizeof (tmp));
}

static inline int
control_notification_unsubscribe (void *sock, enum control_notification cn)
{
  uint16_t tmp = cn;
  return zmq_setsockopt (sock, ZMQ_UNSUBSCRIBE, &tmp, sizeof (tmp));
}

static inline int
control_notification_connect (void *sock)
{
  return zsocket_connect (sock, PUB_SOCK_EP);
}

enum port_speed {
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

struct port_link_state {
  uint32_t port   : 10;
  uint32_t link   : 1;
  uint32_t speed  : 4;
  uint32_t duplex : 1;
} __attribute__ ((packed));

enum control_command {
  CC_SET_FDB_MAP,
  CC_PORT_SEND_BPDU,
  CC_PORT_SHUTDOWN,
  CC_PORT_GET_STATE,
  CC_PORT_FDB_FLUSH,
  CC_PORT_SET_STP_STATE,
  CC_MAX /* must be the last. */
};

enum port_stp_state {
    STP_STATE_DISABLED = 0,
    STP_STATE_DISCARDING,
    STP_STATE_LEARNING,
    STP_STATE_FORWARDING,
    STP_STATE_MAX /* must be the last */
};

enum error_code {
  EC_BAD_REQUEST,
  EC_BAD_VALUE
};

#endif /* __CONTROL_PROTO_H__ */
