#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgGen.h>

#include <route.h>
#include <ip.h>
#include <log.h>
#include <debug.h>

enum status
ip_cpss_lib_init (void)
{
  return route_cpss_lib_init ();
}

enum status
ip_start (void)
{
  DEBUG ("enable IPv4 link local MC reception");
  CRP (cpssDxChBrgGenIpLinkLocalMirrorToCpuEnable
       (0, CPSS_IP_PROTOCOL_IPV4_E, GT_TRUE));

  CRP (cpssDxChBrgGenIpLinkLocalProtMirrorToCpuEnable
       (0, CPSS_IP_PROTOCOL_IPV4_E, 18, GT_TRUE));

  return ST_OK;
}
