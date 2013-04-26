#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>

#include <stack.h>
#include <mcg.h>
#include <log.h>
#include <debug.h>

int stack_id = 0;

void
stack_start (void)
{
  DEBUG ("doing stack setup\r\n");
  mcg_stack_setup ();
  DEBUG ("done stack setup\r\n");

  uint8_t tag[8];
  CPSS_DXCH_NET_DSA_PARAMS_STC tp;

  memset (&tp, 0, sizeof (tp));
  tp.commonParams.dsaTagType = CPSS_DXCH_NET_DSA_TYPE_EXTENDED_E;
  //tp.commonParams.vid = 1;
  tp.dsaType = CPSS_DXCH_NET_DSA_CMD_FROM_CPU_E;
  tp.dsaInfo.fromCpu.dstInterface.type = CPSS_INTERFACE_PORT_E;
  tp.dsaInfo.fromCpu.dstInterface.devPort.devNum = 1;
  tp.dsaInfo.fromCpu.dstInterface.devPort.portNum = 63;
  tp.dsaInfo.fromCpu.cascadeControl = GT_TRUE;

  tp.dsaInfo.fromCpu.srcDev = 1;
  cpssDxChNetIfDsaTagBuild (0, &tp, tag);
  DEBUG ("tag: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
         tag[0], tag[1], tag[2], tag[3],
         tag[4], tag[5], tag[6], tag[7]);

  tp.dsaInfo.fromCpu.srcDev = 31;
  cpssDxChNetIfDsaTagBuild (0, &tp, tag);
  DEBUG ("tag: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
         tag[0], tag[1], tag[2], tag[3],
         tag[4], tag[5], tag[6], tag[7]);

  memset (&tp, 0, sizeof (tp));
  tp.commonParams.dsaTagType = CPSS_DXCH_NET_DSA_TYPE_EXTENDED_E;
  //tp.commonParams.vid = 1;
  tp.dsaType = CPSS_DXCH_NET_DSA_CMD_FROM_CPU_E;
  tp.dsaInfo.fromCpu.dstInterface.type = CPSS_INTERFACE_VIDX_E;
  tp.dsaInfo.fromCpu.dstInterface.vidx = 4094;
  tp.dsaInfo.fromCpu.cascadeControl = GT_TRUE;
  tp.dsaInfo.fromCpu.extDestInfo.multiDest.mirrorToAllCPUs = GT_TRUE;

  tp.dsaInfo.fromCpu.srcDev = 1;
  CRP (cpssDxChNetIfDsaTagBuild (0, &tp, tag));
  DEBUG ("tag: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
         tag[0], tag[1], tag[2], tag[3],
         tag[4], tag[5], tag[6], tag[7]);

  tp.dsaInfo.fromCpu.srcDev = 31;
  CRP (cpssDxChNetIfDsaTagBuild (0, &tp, tag));
  DEBUG ("tag: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
         tag[0], tag[1], tag[2], tag[3],
         tag[4], tag[5], tag[6], tag[7]);

}
