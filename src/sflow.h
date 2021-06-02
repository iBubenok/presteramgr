#include <debug.h>
#include <control-proto.h>

/*Global enable*/
GT_STATUS sflow_set_egress_enable(int enable);
GT_STATUS sflow_set_ingress_enable(int enable);

/**/
GT_STATUS sflow_set_ingress_count_mode(sflow_count_mode_t mode);

/**/
GT_STATUS sflow_set_egress_reload_mode(sflow_count_reload_mode_t mode);
GT_STATUS sflow_set_ingress_reload_mode(sflow_count_reload_mode_t mode);

/*Port limit*/
GT_STATUS sflow_set_egress_port_limit();
GT_STATUS sflow_set_ingress_port_limit();
