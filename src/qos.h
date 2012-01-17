#ifndef __QOS_H__
#define __QOS_H__

#include <control-proto.h>

extern int mls_qos_trust;

extern enum status qos_start (void);
extern enum status qos_set_mls_qos_trust (int);
extern enum status qos_set_port_mls_qos_trust_cos (port_id_t, bool_t);
extern enum status qos_set_port_mls_qos_trust_dscp (port_id_t, bool_t);
extern enum status qos_set_dscp_prio (int, const struct dscp_map *);


#endif /* __QOS_H__ */
