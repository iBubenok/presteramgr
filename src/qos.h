#ifndef __QOS_H__
#define __QOS_H__

#include <control-proto.h>

extern int mls_qos_trust;

extern enum status qos_set_mls_qos_trust (int);
extern enum status qos_set_port_mls_qos_trust_cos (port_num_t, bool_t);
extern enum status qos_set_port_mls_qos_trust_dscp (port_num_t, bool_t);

#endif /* __QOS_H__ */
