#ifndef __QOS_H__
#define __QOS_H__

#include <control-proto.h>

#define QSP_BASE_TC     0
#define QSP_NUM_TC      8

//Time interval for storm control notifications
#define DELAY       30000


extern int mls_qos_trust;
extern const uint8_t qos_default_wrr_weights[8];

extern enum status qos_start (void);
extern enum status qos_set_mls_qos_trust (int);
extern enum status qos_set_port_mls_qos_trust_cos (port_id_t, bool_t);
extern enum status qos_set_port_mls_qos_trust_dscp (port_id_t, bool_t);
extern enum status qos_set_dscp_prio (int, const struct dscp_map *);
extern enum status qos_set_cos_prio (const queue_id_t *);
extern enum status qos_set_prioq_num (int);
extern enum status qos_set_wrr_queue_weights (const uint8_t *);
extern enum status qos_set_wrtd (int);
extern enum status qos_profile_manage (struct qos_profile_mgmt *, qos_profile_id_t *);

void *storm_control_thread (void *dummy);
int storm_detect (zloop_t *loop, int timer_id, void *notify_socket);
void send_storm_detected_event (void *notify_socket);


#endif /* __QOS_H__ */
