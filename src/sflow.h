#include <debug.h>
#include <control-proto.h>

/*Global enable*/
GT_STATUS
sflow_set_enable (
  sflow_type_t type,
  int enable);

/**/
GT_STATUS
sflow_set_ingress_count_mode (
  sflow_count_mode_t mode);

/**/
GT_STATUS
sflow_set_reload_mode (
  sflow_type_t type,
  sflow_count_reload_mode_t mode);

/*Port limit*/
GT_STATUS
sflow_set_port_limit (
  port_id_t pid,
  sflow_type_t type,
  uint32_t limit);
