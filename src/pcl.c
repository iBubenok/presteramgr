#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/pcl/cpssDxChPcl.h>

#include <pcl.h>
#include <log.h>
#include <debug.h>

enum status
pcl_cpss_lib_init (void)
{
  CRP (cpssDxChPclInit (0));
  CRP (cpssDxChPclIngressPolicyEnable (0, GT_TRUE));

  return ST_OK;
}
