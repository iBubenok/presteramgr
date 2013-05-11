#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/generic/cscd/cpssGenCscd.h>
#include <cpss/dxCh/dxChxGen/cscd/cpssDxChCscd.h>

#include <stack.h>
#include <vlan.h>
#include <mcg.h>
#include <dev.h>
#include <port.h>
#include <mgmt.h>
#include <control.h>
#include <log.h>
#include <debug.h>

int stack_id = 0;
struct port *stack_pri_port = NULL, *stack_sec_port = NULL;

enum status
stack_set_dev_map (uint8_t tdev, uint8_t port)
{
  GT_STATUS rc;
  CPSS_CSCD_LINK_TYPE_STC lp = {
    .linkNum  = port,
    .linkType = CPSS_CSCD_LINK_TYPE_PORT_E
  };

  rc = CRP (cpssDxChCscdDevMapTableSet (0, tdev, 0, &lp, 0));
  switch (rc) {
  case GT_OK:        return ST_OK;
  case GT_HW_ERROR:  return ST_HW_ERROR;
  case GT_BAD_PARAM: return ST_BAD_VALUE;
  default:           return ST_HEX;
  }
}

static void __attribute__ ((unused))
stack_set_test_dev_map (void)
{
  int i, tdev;

  switch (stack_id) {
  case 1:  tdev = 2; break;
  case 2:  tdev = 1; break;
  default: return;
  };

  for (i = 0; i < nports; i++) {
    if (ports[i].stack_role == PSR_PRIMARY) {
      stack_set_dev_map (tdev, ports[i].lport);
      break;
    }
  }
}

void
stack_start (void)
{
  DEBUG ("doing stack setup\r\n");
  vlan_stack_setup ();
  mcg_stack_setup ();
  /* FIXME: for testing purposes only! */
  stack_set_test_dev_map ();
  DEBUG ("done stack setup\r\n");

  uint8_t tag[8];
  CPSS_DXCH_NET_DSA_PARAMS_STC tp;

  memset (&tp, 0, sizeof (tp));
  tp.commonParams.dsaTagType = CPSS_DXCH_NET_DSA_TYPE_EXTENDED_E;
  tp.commonParams.vid = 4095;
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
  tp.commonParams.vid = 4095;
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

enum status
stack_mail (enum port_stack_role role, void *data, size_t len)
{
  struct port *port;
  uint8_t tag[8];
  CPSS_DXCH_NET_DSA_PARAMS_STC tp;

  if (!stack_active ())
    return ST_BAD_STATE;

  switch (role) {
  case PSR_PRIMARY:
    port = stack_pri_port;
    break;
  case PSR_SECONDARY:
    port = stack_sec_port;
    break;
  default:
    return ST_BAD_VALUE;
  }

  if (!port)
    return ST_BAD_STATE;

  memset (&tp, 0, sizeof (tp));
  tp.commonParams.dsaTagType = CPSS_DXCH_NET_DSA_TYPE_EXTENDED_E;
  tp.commonParams.vid = 4095;
  tp.dsaType = CPSS_DXCH_NET_DSA_CMD_FROM_CPU_E;
  tp.dsaInfo.fromCpu.dstInterface.type = CPSS_INTERFACE_PORT_E;
  tp.dsaInfo.fromCpu.dstInterface.devPort.devNum = phys_dev (port->ldev);
  tp.dsaInfo.fromCpu.dstInterface.devPort.portNum = port->lport;
  tp.dsaInfo.fromCpu.cascadeControl = GT_TRUE;
  tp.dsaInfo.fromCpu.extDestInfo.devPort.mailBoxToNeighborCPU = GT_TRUE;
  CRP (cpssDxChNetIfDsaTagBuild (port->ldev, &tp, tag));

  mgmt_send_gen_frame (tag, data, len);

  return ST_OK;
}

void
stack_handle_mail (port_id_t pid, uint8_t *data, size_t len)
{
  struct port *port = port_ptr (pid);

  if (!port || !is_stack_port (port))
    return;

  cn_mail (port->stack_role, data, len);
}
