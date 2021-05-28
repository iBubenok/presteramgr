#include <sflow.h>

void enable_sampling()
{
  DEBUG("%s\n", __FUNCTION__);
  pcl_enable_sflow_sampling();
}

void get_count_sflow()
{
  DEBUG("%s\n", __FUNCTION__);
  pcl_get_sflow_count();
}
