#include <debug.h>

/*Global enable*/
GT_STATUS sflow_set_egress_enable(int enable);
GT_STATUS sflow_set_ingress_enable(int enable);

/**/
GT_STATUS sflow_set_ingress_count_mode();

/**/
GT_STATUS sflow_set_egress_reload_mode();
GT_STATUS sflow_set_ingress_reload_mode();

/**/
GT_STATUS sflow_set_egress_port_limit();
GT_STATUS sflow_set_ingress_port_limit();
