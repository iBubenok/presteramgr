#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/generic/cscd/cpssGenCscd.h>
#include <cpss/dxCh/dxChxGen/cscd/cpssDxChCscd.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgSrcId.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgEgrFlt.h>

#include <stack.h>
#include <vlan.h>
#include <mcg.h>
#include <dev.h>
#include <port.h>
#include <mgmt.h>
#include <control.h>
#include <log.h>
#include <utils.h>
#include <pcl.h>
#include <debug.h>

int stack_id = 0;
struct port *stack_pri_port = NULL, *stack_sec_port = NULL;
static int ring = 0;

void
stack_start (void)
{
  CPSS_CSCD_LINK_TYPE_STC lp = {
    .linkType = CPSS_CSCD_LINK_TYPE_PORT_E,
    .linkNum  = CPSS_NULL_PORT_NUM_CNS
  };
  int i;

  if (!stack_active ())
    return;

  DEBUG ("doing stack setup\r\n");

  for (i = 0; i < 32; i++) {
    CRP (cpssDxChCscdDevMapTableSet (0, i, 0, &lp, 0));
    CRP (cpssDxChBrgSrcIdGroupEntrySet
         (0, i, GT_TRUE,
          (i == stack_id)
          ? &all_ports_bmp
          : &nst_ports_bmp));
  }

  CRP (cpssDxChBrgSrcIdGlobalSrcIdAssignModeSet
       (0, CPSS_BRG_SRC_ID_ASSIGN_MODE_PORT_DEFAULT_E));
  CRP (cpssDxChBrgSrcIdGlobalUcastEgressFilterSet (0, GT_TRUE));

  vlan_stack_setup ();
  mcg_stack_setup ();

  DEBUG ("done stack setup\r\n");
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
  tp.dsaInfo.fromCpu.srcDev = stack_id;
  tp.dsaInfo.fromCpu.srcId = stack_id;
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

uint8_t
stack_port_get_state (enum port_stack_role role)
{
  if (!stack_active ())
    return 0;

  switch (role) {
  case PSR_PRIMARY:
    return stack_pri_port->state.attrs.portLinkUp == GT_TRUE;
  case PSR_SECONDARY:
    return stack_sec_port->state.attrs.portLinkUp == GT_TRUE;
  default:
    return 0;
  }
}

static void
stack_enable_mc_filter (int enable)
{
  pcl_enable_mc_drop (stack_sec_port->id, enable);
}

static void
stack_update_ring (int new_ring)
{
  new_ring = !!new_ring;
  if (ring == new_ring)
    return;

  ring = new_ring;
  stack_enable_mc_filter (ring);
}

enum status
stack_set_dev_map (uint8_t dev, const uint8_t *hops)
{
  CPSS_CSCD_LINK_TYPE_STC lp;

  if (!stack_active ())
    return ST_BAD_STATE;

  stack_update_ring (hops[0] && hops[1]);

  CRP (cpssDxChBrgSrcIdGroupPortDelete
       (stack_pri_port->ldev, dev, stack_pri_port->lport));
  CRP (cpssDxChBrgSrcIdGroupPortDelete
       (stack_sec_port->ldev, dev, stack_sec_port->lport));

  if (hops[0] > 1)
    CRP (cpssDxChBrgSrcIdGroupPortAdd
         (stack_pri_port->ldev, dev, stack_pri_port->lport));
  if (hops[1] > 1)
    CRP (cpssDxChBrgSrcIdGroupPortAdd
         (stack_sec_port->ldev, dev, stack_sec_port->lport));

  lp.linkType = CPSS_CSCD_LINK_TYPE_PORT_E;
  if (hops[0]) {
    if (hops[1])
      lp.linkNum = (hops[1] >= hops[0])
        ? stack_pri_port->lport
        : stack_sec_port->lport;
    else
      lp.linkNum = stack_pri_port->lport;
  } else if (hops[1])
    lp.linkNum = stack_sec_port->lport;
  else
    lp.linkNum = CPSS_NULL_PORT_NUM_CNS;
  CRP (cpssDxChCscdDevMapTableSet (0, dev, 0, &lp, 0));

  return ST_OK;
}
