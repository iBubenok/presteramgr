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
static uint32_t dev_bmp  = 0;
static uint32_t dev_mask = 0;

void
stack_start (void)
{
  CPSS_PORTS_BMP_STC nul_ports_bmp;
#ifndef VARIANT_ARLAN_3448PGE
  /* FIXME: add multidev support! */
  CPSS_CSCD_LINK_TYPE_STC lp = {
    .linkType = CPSS_CSCD_LINK_TYPE_PORT_E,
    .linkNum  = CPSS_NULL_PORT_NUM_CNS
  };
#endif /* VARIANT_ARLAN_3448PGE */
  int i;

  if (!stack_active ())
    return;

  DEBUG ("doing stack setup\r\n");

  memset (&nul_ports_bmp, 0, sizeof (nul_ports_bmp));
  for (i = 0; i < 32; i++) {
#ifndef VARIANT_ARLAN_3448PGE
    /* FIXME: add multidev support! */
    CPSS_PORTS_BMP_STC *pbm;

    if (i == stack_id)
      pbm = &all_ports_bmp;
    else if (i == 0)
      pbm = &nst_ports_bmp;
    else
      pbm = &nul_ports_bmp;

    CRP (cpssDxChCscdDevMapTableSet (0, i, 0, &lp, 0));
    CRP (cpssDxChBrgSrcIdGroupEntrySet (0, i, GT_TRUE, pbm));
#endif /* VARIANT_ARLAN_3448PGE */

    if (i < stack_id)
      dev_mask |= 1 << i;
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
stack_enable_mc_filter (int en)
{
  static int enable = 0;

  en = !!en;
  if (en == enable)
    return;

  enable = en;
  CRP (cpssDxChBrgPortEgrFltUnkEnable
       (stack_sec_port->ldev, stack_sec_port->lport, gt_bool (enable)));
  CRP (cpssDxChBrgPortEgrFltUregMcastEnable
       (stack_sec_port->ldev, stack_sec_port->lport, gt_bool (enable)));
  CRP (cpssDxChBrgPortEgrFltUregBcEnable
       (stack_sec_port->ldev, stack_sec_port->lport, gt_bool (enable)));
  pcl_enable_mc_drop (stack_sec_port->id, enable);
}

static void
stack_update_ring (int new_ring, uint32_t new_dev_bmp)
{
  new_ring = !!new_ring;
  if (ring == new_ring && dev_bmp == new_dev_bmp)
    return;

  ring = new_ring;
  dev_bmp = new_dev_bmp;
  stack_enable_mc_filter (ring && !(dev_bmp & dev_mask));
}

enum status
stack_set_dev_map (uint8_t dev, const uint8_t *hops)
{
  CPSS_CSCD_LINK_TYPE_STC lp;
  uint32_t new_dev_bmp = dev_bmp;

  if (!stack_active ())
    return ST_BAD_STATE;

  if (hops[0] || hops[1])
    new_dev_bmp |= 1 << dev;
  else
    new_dev_bmp &= ~(1 << dev);
  stack_update_ring (hops[0] && hops[1], new_dev_bmp);

  CRP (cpssDxChBrgSrcIdGroupPortDelete
       (stack_pri_port->ldev, dev, stack_pri_port->lport));
  CRP (cpssDxChBrgSrcIdGroupPortDelete
       (stack_sec_port->ldev, dev, stack_sec_port->lport));

  if (hops[0] > 1 || !hops[0])
    CRP (cpssDxChBrgSrcIdGroupPortAdd
         (stack_pri_port->ldev, dev, stack_pri_port->lport));
  if (hops[1] > 1 || !hops[1])
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

enum status
stack_txen (uint8_t dev, int txen)
{
  GT_STATUS (*func) (GT_U8, GT_U32, GT_U8) = txen
    ? cpssDxChBrgSrcIdGroupPortAdd
    : cpssDxChBrgSrcIdGroupPortDelete;
  int i;

  for (i = 0; i < nports; i++) {
    struct port *port = &ports[i];
    if (is_stack_port (port))
      continue;
    func (port->ldev, dev, port->lport);
  }

  return ST_OK;
}
