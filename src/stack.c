#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/generic/cscd/cpssGenCscd.h>
#include <cpss/dxCh/dxChxGen/cscd/cpssDxChCscd.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgSrcId.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgEgrFlt.h>

#include <stackd.h>
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
#include <qos.h>
#include <mac.h>
#include <debug.h>

int stack_id = 0, master_id;
uint8_t master_mac[6] = {0, 0, 0, 0, 0, 0};
struct port *stack_pri_port = NULL, *stack_sec_port = NULL;
static int ring = 0;
static uint32_t dev_bmp  = 0;
static uint32_t dev_mask = 0;
static serial_t stack_serial = 0;

static struct stackd_unit stack_units[MAX_STACK_UNITS+1];
static struct stackd_unit stack_units_update[MAX_STACK_UNITS+1];


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
stack_set_master (uint8_t master, uint64_t serial, const uint8_t *mac)
{
DEBUG(">>>stack_set_master (%d, %llu, const uint8_t *mac)\n", master, serial);
  if (master > 16)
    return ST_BAD_VALUE;

  master_id = master;
  if (stack_serial > serial)
    DEBUG ("Stacking error, old stack configuration serial %llu is bigger or equal to new stack conf %llu\n",
        stack_serial, serial);
  stack_serial = serial;
  memcpy (master_mac, mac, 6);

  mac_set_master(master, serial);

  int d;
  for_each_dev (d) {
    CRP (cpssDxChNetIfCpuCodeDesignatedDeviceTableSet
         (d, 1, master));
  }
  DEBUG("cpssDxChNetIfCpuCodeDesignatedDeviceTableSet (d, 1, %d))\n", master);

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

  memset(stack_units, 0, sizeof(stack_units));
  memset (&nul_ports_bmp, 0, sizeof (nul_ports_bmp));
  for_each_dev (d)
    dbmp |= 1 << phys_dev (d);

  for (i = 0; i < 32; i++) {
    for_each_dev (d) {
      CPSS_PORTS_BMP_STC *pbm;

      if (i != stack_id) {
        pbm = i ? &nul_ports_bmp : &nst_ports_bmp[d];
        CPSS_PORTS_BMP_STC tpbm; /* this code is executed once -> fuck the optimization */
        tpbm.ports[0] = pbm->ports[0];
        tpbm.ports[1] = pbm->ports[1];
        if (d == stack_pri_port->ldev) {
          tpbm.ports[0] |= ic0_ports_bmp.ports[0];
          tpbm.ports[1] |= ic0_ports_bmp.ports[1];
        }
        CRP (cpssDxChBrgSrcIdGroupEntrySet (d, i, GT_TRUE, &tpbm));
      }

      if (!(dbmp & (1 << i)))
        CRP (cpssDxChCscdDevMapTableSet (d, i, 0, &lp, 0));
    }

    if (i < stack_id)
      dev_mask |= 1 << i;
  }

  CRP (cpssDxChBrgSrcIdGroupPortAdd
       (stack_pri_port->ldev, 0, stack_pri_port->lport));
  CRP (cpssDxChBrgSrcIdGroupPortAdd
       (stack_sec_port->ldev, 0, stack_sec_port->lport));

  for_each_dev (d) {
    CRP (cpssDxChBrgSrcIdGlobalUcastEgressFilterSet (d, GT_TRUE));
    CRP (cpssDxChCscdCtrlQosSet (d, 7, CPSS_DP_GREEN_E, CPSS_DP_GREEN_E));
  }

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
    memcpy(data, mac_pri, 6);
    port = stack_pri_port;
    break;
  case PSR_SECONDARY:
    memcpy(data, mac_sec, 6);
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
  tp.dsaType = CPSS_DXCH_NET_DSA_CMD_FORWARD_E;
  tp.dsaInfo.forward.srcDev = 0;
  tp.dsaInfo.forward.source.portNum = 63;
  tp.dsaInfo.forward.srcId = 0;
  tp.dsaInfo.forward.qosProfileIndex = QSP_BASE_TC + 7;

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
stack_update_ring (int new_ring, uint32_t new_dev_bmp)
{
  new_ring = !!new_ring;
  if (ring == new_ring && dev_bmp == new_dev_bmp)
    return;

  ring = new_ring;
  dev_bmp = new_dev_bmp;
}

static void
stack_update_dev_map(uint8_t dev, const uint8_t *hops, uint8_t num_pp) {

DEBUG("stack_update_dev_map(%d, %d:%d, %d)\n", dev, hops[0], hops[1], num_pp);
  CPSS_CSCD_LINK_TYPE_STC lp;
  int d;
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
DEBUG("cpssDxChCscdDevMapTableSet(%d, %d, 0, lp %d, CPSS_DXCH_CSCD_TRUNK_LINK_HASH_IS_SRC_PORT_E)",d,dev, lp.linkNum);
    if (num_pp == 2) {
      CRP (cpssDxChCscdDevMapTableSet
           (d, dev + NEXTDEV_INC, 0, &lp, CPSS_DXCH_CSCD_TRUNK_LINK_HASH_IS_SRC_PORT_E));
DEBUG("cpssDxChCscdDevMapTableSet (%d, %d + NEXTDEV_INC, 0, lp %d, CPSS_DXCH_CSCD_TRUNK_LINK_HASH_IS_SRC_PORT_E)", d, dev, lp.linkNum);
    }
  }
}

static uint8_t
//stack_get_port_to_target_pri(uint8_t dev) {
stack_get_hops_from_target_port(uint8_t target, uint8_t p, uint8_t *our_port) {
DEBUG("stack_get_hops_from_target_port(%d, %d, %p )\n", target, p, our_port);

//  uint8_t p = 0;
  uint8_t h = 1;
  uint8_t d = stack_units_update[target].neighbor[p];
  uint8_t od = target;
  while (stack_id != d) {
    if (stack_units_update[d].neighbor[0] == od) {
      p = 1;
      od = d;
      d = stack_units_update[d].neighbor[p];
      h++;
      continue;
    }
    if (stack_units_update[d].neighbor[1] == od) {
      p = 0;
      od = d;
      d = stack_units_update[d].neighbor[p];
      h++;
      continue;
    }
    assert(0); /* cause it's ring topology */
  }
  if (our_port != NULL) {
    if (stack_units_update[d].neighbor[0] == od)
      *our_port = 0;
    else if (stack_units_update[d].neighbor[1] == od)
      *our_port = 1;
    else
      assert(0);
  }
DEBUG("HOPS== %d\n",h);
  return h;
}

static void
stack_update_srcid_groups(uint8_t dev, uint8_t ring, uint8_t n) {

DEBUG("stack_update_srcid_groups(%d, %d, %d)\n", dev, ring, n);
  CRP (cpssDxChBrgSrcIdGroupPortDelete
       (stack_pri_port->ldev, dev, stack_pri_port->lport));
  CRP (cpssDxChBrgSrcIdGroupPortDelete
       (stack_sec_port->ldev, dev, stack_sec_port->lport));

 if (dev == stack_id) {
    if (n == 1) {
      return;
    }
    if (!ring) {
        if (stack_units_update[dev].neighbor[0]) {
          CRP (cpssDxChBrgSrcIdGroupPortAdd
               (stack_pri_port->ldev, dev, stack_pri_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_pri_port->ldev, %d, stack_pri_port->lport)\n", dev);
        }
        if (stack_units_update[dev].neighbor[1]) {
          CRP (cpssDxChBrgSrcIdGroupPortAdd
               (stack_sec_port->ldev, dev, stack_sec_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_sec_port->ldev, %d, stack_sec_port->lport)\n", dev);
        }
    } else {
      CRP (cpssDxChBrgSrcIdGroupPortAdd
           (stack_pri_port->ldev, dev, stack_pri_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_pri_port->ldev, %d, stack_pri_port->lport)\n", dev);
      if (n > 2) {
        CRP (cpssDxChBrgSrcIdGroupPortAdd
             (stack_sec_port->ldev, dev, stack_sec_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_sec_port->ldev, %d, stack_sec_port->lport)\n", dev);
      }
    }
    return;
  }

  if (!ring) {
    if (stack_units_update[dev].hops[0])
      if (stack_units_update[stack_id].neighbor[1]) {
        CRP (cpssDxChBrgSrcIdGroupPortAdd
             (stack_sec_port->ldev, dev, stack_sec_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_sec_port->ldev, %d, stack_sec_port->lport)\n", dev);
      }
    if (stack_units_update[dev].hops[1])
      if (stack_units_update[stack_id].neighbor[0]) {
        CRP (cpssDxChBrgSrcIdGroupPortAdd
             (stack_pri_port->ldev, dev, stack_pri_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_pri_port->ldev, %d, stack_pri_port->lport)\n", dev);
      }
    return;
  }

  /* ring topology */

  if (n == 2 || n == 3)
    return;

  int hops0 = ((int)stack_units_update[dev].hops[0]);
  int hops1 = ((int)stack_units_update[dev].hops[1]);
  int hops_diff = hops0 - hops1;
  if (hops_diff < 0)
    hops_diff = -hops_diff;

DEBUG("hops0== %d, hops1== %d, hops_diff== %d\n", hops0, hops1, hops_diff);
  if (hops_diff > 2)
    if (hops0 < hops1) {
      CRP (cpssDxChBrgSrcIdGroupPortAdd
           (stack_sec_port->ldev, dev, stack_sec_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_sec_port->ldev, %d, stack_sec_port->lport)\n", dev);
    }
    else {
      CRP (cpssDxChBrgSrcIdGroupPortAdd
           (stack_pri_port->ldev, dev, stack_pri_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_pri_port->ldev, %d, stack_pri_port->lport)\n", dev);
    }
  else
    if (hops_diff == 2) {
      uint8_t sp;
      if (stack_get_hops_from_target_port(dev, 0, &sp) < stack_get_hops_from_target_port(dev, 1, NULL)) {
DEBUG("sp = %d\n", sp);
        if (sp == 0) {
          CRP (cpssDxChBrgSrcIdGroupPortAdd
               (stack_sec_port->ldev, dev, stack_sec_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_sec_port->ldev, %d, stack_sec_port->lport)\n", dev);
        }
        else {
          CRP (cpssDxChBrgSrcIdGroupPortAdd
               (stack_pri_port->ldev, dev, stack_pri_port->lport));
DEBUG("cpssDxChBrgSrcIdGroupPortAdd (stack_pri_port->ldev, %d, stack_pri_port->lport)\n", dev);
        }
      }
    }

}

static void
stack_update_all(int n, int is_ring, struct stackd_unit *old_units, struct stackd_unit *new_units) {

DEBUG("stack_update_all(%d, %d,,)\n", n, is_ring);
  int i;
  for (i = 1; i <= MAX_STACK_UNITS; i++) {
    if (i == stack_id) {
      stack_update_srcid_groups(i, is_ring, n);
      continue;
    }
//    if ( !memcmp(&old_units[i], &new_units[i], sizeof(*old_units)))
//      continue;

    if (old_units[i].hops[0] != new_units[i].hops[0] ||
        old_units[i].hops[1] != new_units[i].hops[1]) {
      stack_update_dev_map(i, new_units[i].hops, new_units[i].num_pp);
    }
    if (i == stack_units_update[i].id || i == stack_units[i].id)
      stack_update_srcid_groups(i, is_ring, n);

  }
}

enum status
stack_update_conf(void *data, size_t size) {

  uint32_t new_dev_bmp = dev_bmp;
  struct stackd_ntf_conf_info *conf = (struct stackd_ntf_conf_info*) data;
  struct stackd_unit *units = (struct stackd_unit*) (conf + 1);

DEBUG("STACK recieved conf with %d units, master unit is %d\n", conf->conf_info.num_units, conf->conf_info.master_id);
PRINTHexDump(data, size);

  memset(stack_units_update, 0, sizeof(stack_units_update));
  int i;
//DEBUG("stack_units_update = %p, conf = %p, units = %p",  stack_units_update, conf, units);
  for (i = 0; i < conf->conf_info.num_units; i++) {
//DEBUG("i== %d, units[i].id== %d, memcmp(%p, %p)\n", i, units[i].id, &stack_units_update[units[i].id], &units[i]);
    memcpy(&stack_units_update[units[i].id], &units[i], sizeof(struct stackd_unit));
    if (units[i].hops[0] || units[i].hops[1])
      new_dev_bmp |= 1 << units[i].id;
    else
      new_dev_bmp &= ~(1 << units[i].id);
  }

  stack_update_ring(conf->conf_info.topo == STT_RING, new_dev_bmp);
  stack_update_all(conf->conf_info.num_units, conf->conf_info.topo == STT_RING, stack_units, stack_units_update);
  memcpy(stack_units, stack_units_update, sizeof(stack_units));
DEBUG("STACK \n");
PRINTHexDump(stack_units_update, sizeof(struct stackd_unit) *6);
  return ST_OK;
}

enum status
stack_set_dev_map (uint8_t dev, const uint8_t *hops, uint8_t num_pp)
{
  return ST_OK;
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

  if (hops[0] > 1 || !hops[0]) {
    CRP (cpssDxChBrgSrcIdGroupPortAdd
         (stack_pri_port->ldev, dev, stack_pri_port->lport));
  }
  if (hops[1] > 1 || !hops[1]) {
    CRP (cpssDxChBrgSrcIdGroupPortAdd
         (stack_sec_port->ldev, dev, stack_sec_port->lport));
  }

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
    if (num_pp == 2)
      CRP (cpssDxChCscdDevMapTableSet
           (d, dev + NEXTDEV_INC, 0, &lp, CPSS_DXCH_CSCD_TRUNK_LINK_HASH_IS_SRC_PORT_E));
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
