#ifndef __DATA_H__
#define __DATA_H__

#include <cpss/generic/port/cpssPortCtrl.h>
#include <cpss/generic/bridge/cpssGenBrgVlanTypes.h>

int data_encode_port_state (struct port_link_state *, int, const CPSS_PORT_ATTRIBUTES_STC *);
int data_decode_stp_state (CPSS_STP_STATE_ENT *, enum port_stp_state);

#endif /* __DATA_H__ */


