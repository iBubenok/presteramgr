#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgGen.h>

#include <route.h>
#include <ip.h>
#include <sysdeps.h>
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
  int d;

  DEBUG ("enable IPv4 link local MC reception");

  for_each_dev (d) {
    CRP (cpssDxChBrgGenArpBcastToCpuCmdSet
       (d, CPSS_PACKET_CMD_MIRROR_TO_CPU_E));
    CRP (cpssDxChBrgGenIpLinkLocalMirrorToCpuEnable
         (d, CPSS_IP_PROTOCOL_IPV4_E, GT_TRUE));
    CRP (cpssDxChBrgGenIpLinkLocalProtMirrorToCpuEnable
         (d, CPSS_IP_PROTOCOL_IPV4_E, 18, GT_TRUE));
  }

  return ST_OK;
}
