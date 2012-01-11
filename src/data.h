#ifndef __DATA_H__
#define __DATA_H__

#include <cpss/generic/port/cpssPortCtrl.h>
#include <cpss/generic/bridge/cpssGenBrgVlanTypes.h>
#include <control-proto.h>

extern enum status data_encode_port_state (struct port_link_state *, const CPSS_PORT_ATTRIBUTES_STC *);
extern enum status data_decode_port_speed (CPSS_PORT_SPEED_ENT *, enum port_speed);
extern enum status data_decode_stp_state (CPSS_STP_STATE_ENT *, enum port_stp_state);
extern void data_encode_fdb_addrs (zmsg_t *, vid_t);

#endif /* __DATA_H__ */


