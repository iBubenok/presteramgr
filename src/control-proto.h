#ifndef __CONTROL_PROTO_H__
#define __CONTROL_PROTO_H__

#include <zmq.h>
#include <czmq.h>
#include <stdint.h>

#define PUB_SOCK_EP "ipc:///tmp/presteramgr.notify"
#define CMD_SOCK_EP "ipc:///tmp/presteramgr.command"

typedef uint16_t port_num_t;
typedef uint16_t notification_t;
typedef uint16_t command_t;
typedef uint16_t status_t;
typedef uint16_t stp_id_t;
typedef uint8_t  stp_state_t;

enum control_notification {
  CN_PORT_LINK_STATE,
  CN_BPDU,
  CN_MAX /* NB: must be the last. */
};

static inline int
control_notification_subscribe (void *sock, enum control_notification cn)
{
  notification_t tmp = cn;
  return zmq_setsockopt (sock, ZMQ_SUBSCRIBE, &tmp, sizeof (tmp));
}

static inline int
control_notification_unsubscribe (void *sock, enum control_notification cn)
{
  notification_t tmp = cn;
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
  uint8_t link;
  uint8_t speed;
  uint8_t duplex;
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

enum status {
  ST_OK = 0,
  ST_BAD_FORMAT,
  ST_BAD_REQUEST,
  ST_BAD_VALUE,
  ST_HW_ERROR,
  ST_NOT_SUPPORTED,
  ST_NOT_IMPLEMENTED,
  ST_DOES_NOT_EXIST,
  ST_HEX
};

#endif /* __CONTROL_PROTO_H__ */
