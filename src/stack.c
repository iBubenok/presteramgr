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
#include <sysdeps.h>
#include <debug.h>

int stack_id = 0, master_id;
uint8_t master_mac[6] = {0, 0, 0, 0, 0, 0};
struct port *stack_pri_port = NULL, *stack_sec_port = NULL;
static int ring = 0;
static uint32_t dev_bmp  = 0;
static uint32_t dev_mask = 0;

void
stack_init (void)
{
  const char *mac, *p;
  char *e;
  int i;

  master_id = stack_id;

  mac = getenv ("MAC");
  if (!mac) {
    ERR ("MAC environment variable not defined\r\n");
    exit (1);
  }

  for (i = 0, p = mac; i < 6; i++, p = e + 1) {
    unsigned long b = strtoul (p, &e, 16);
    if ((b > 255) || ((i < 5) && (*e != ':'))) {
      ERR ("bad MAC environment variable format\r\n");
      exit (1);
    }
    master_mac[i] = b;
  }
  if (*e) {
    ERR ("bad MAC environment variable format\r\n");
    exit (1);
  }
}

enum status
stack_set_master (uint8_t master, const uint8_t *mac)
{
  if (master > 16)
    return ST_BAD_VALUE;

  master_id = master;
  memcpy (master_mac, mac, 6);

  return ST_OK;
}

void
stack_start (void)
{
  CPSS_PORTS_BMP_STC nul_ports_bmp;
  CPSS_CSCD_LINK_TYPE_STC lp = {
    .linkType = CPSS_CSCD_LINK_TYPE_PORT_E,
    .linkNum  = CPSS_NULL_PORT_NUM_CNS
  };
  int i, d;
  uint32_t dbmp = 0;

  if (!stack_active ())
    return;

  DEBUG ("doing stack setup\r\n");

  memset (&nul_ports_bmp, 0, sizeof (nul_ports_bmp));
  for_each_dev (d)
    dbmp |= 1 << phys_dev (d);

  for (i = 0; i < 32; i++) {
    for_each_dev (d) {
      CPSS_PORTS_BMP_STC *pbm;

      if (i != stack_id) {
        pbm = i ? &nul_ports_bmp : &nst_ports_bmp[d];
        CRP (cpssDxChBrgSrcIdGroupEntrySet (d, i, GT_TRUE, pbm));
      }

      if (!(dbmp & (1 << i)))
        CRP (cpssDxChCscdDevMapTableSet (d, i, 0, &lp, 0));
    }

    if (i < stack_id)
      dev_mask |= 1 << i;
  }

  for_each_dev (d)
    CRP (cpssDxChBrgSrcIdGlobalUcastEgressFilterSet (d, GT_TRUE));

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
  tp.dsaInfo.fromCpu.tc = 7;
  tp.dsaInfo.fromCpu.dstInterface.type = CPSS_INTERFACE_PORT_E;
  tp.dsaInfo.fromCpu.dstInterface.devPort.devNum = phys_dev (port->ldev);
  tp.dsaInfo.fromCpu.dstInterface.devPort.portNum = port->lport;
  tp.dsaInfo.fromCpu.cascadeControl = GT_FALSE;
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
  int d;

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

  for_each_dev (d) {
    struct port *port;

    lp.linkType = CPSS_CSCD_LINK_TYPE_PORT_E;
    lp.linkNum  = CPSS_NULL_PORT_NUM_CNS;

    if (hops[0]) {
      if (hops[1])
        port = (hops[1] >= hops[0])
          ? stack_pri_port
          : stack_sec_port;
      else
        port = stack_pri_port;
    } else if (hops[1])
      port = stack_sec_port;
    else
      port = NULL;

    if (port) {
      if (port->ldev == d)
        lp.linkNum = port->lport;
      else {
        lp.linkType = CPSS_CSCD_LINK_TYPE_TRUNK_E;
        lp.linkNum = SYSD_CSCD_TRUNK;
      }
    }

    CRP (cpssDxChCscdDevMapTableSet
         (d, dev, 0, &lp, CPSS_DXCH_CSCD_TRUNK_LINK_HASH_IS_SRC_PORT_E));
  }

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
