#ifndef __CONTROL_PROTO_H__
#define __CONTROL_PROTO_H__

enum control_notification {
  CN_PORT_STATE,
  CN_MAX /* NB: must be the last. */
};

struct control_port_state {
  int port;
  int link;
  int speed;
  int duplex;
};

#endif /* __CONTROL_PROTO_H__ */
