#include <debug.h>
#include <control-proto.h>

/*Global enable*/
enum status
sflow_set_enable (
  sflow_type_t type,
  int enable);

/**/
enum status
sflow_set_ingress_count_mode (
  sflow_count_mode_t mode);

/**/
enum status
sflow_set_reload_mode (
  sflow_type_t type,
  sflow_count_reload_mode_t mode);

/*Port limit*/
enum status
sflow_set_port_limit (
  port_id_t pid,
  sflow_type_t type,
  uint32_t limit);

/*Convert GT_STATUS to enum status*/
enum status
convert_status (
    GT_STATUS st);
