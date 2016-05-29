#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <gif.h>
#include <vif.h>
#include <port.h>
#include <trunk.h>
#include <stack.h>
#include <utils.h>
#include <dev.h>
#include <mgmt.h>
#include <debug.h>
#include <sysdeps.h>
#include <assert.h>

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>

struct vif_dev_ports {
  int local;
  int n_total;
  int n_by_type[VIFT_PORT_TYPES];
  struct port port[CPSS_MAX_PORTS_NUM_CNS];
};

static struct vif_dev_ports vifs[16];

vifp_single_dev_t vifp_by_hw[32];

void
vif_init (void)
{
  struct vif_dev_ports *dp;
  int i, j;

  memset (vifs, 0, sizeof (vifs));

  dp = &vifs[stack_id];
DEBUG("====vif_init(), dp= %p\n", dp);

  for (i = 0; i < 16; i++) {
    for (j = 0; j < CPSS_MAX_PORTS_NUM_CNS; j++) {
      vifs[i].port[j].vif.set_speed = vif_set_speed_remote;
    }
  }

  ports = dp->port;
DEBUG("====vif_init(), ports= %p\n", ports);

  for (i = 0; i < NPORTS; i++) {
    if (IS_FE_PORT (i))
      dp->port[i].vif.vifid.type = VIFT_FE;
    else if (IS_GE_PORT (i))
      dp->port[i].vif.vifid.type = VIFT_GE;
    else if (IS_XG_PORT (i))
      dp->port[i].vif.vifid.type = VIFT_XG;
    dp->port[i].vif.vifid.dev = stack_id;
    dp->port[i].vif.vifid.num = ++dp->n_by_type[dp->port[i].vif.vifid.type];
    dp->port[i].vif.local = &ports[i];
    dp->port[i].vif.trunk = NULL;
    dp->port[i].vif.islocal = 1;
    dp->port[i].vif.valid = 1;
    dp->port[i].vif.trust_cos = 0;
    dp->port[i].vif.trust_dscp = 0;
    dp->port[i].vif.c_speed = PORT_SPEED_AUTO;
    dp->port[i].vif.c_speed_auto = 1;
    dp->port[i].vif.c_duplex = PORT_DUPLEX_AUTO;
    dp->port[i].vif.c_shutdown = 0;
    dp->port[i].vif.set_speed = vif_set_speed_port;
DEBUG("====vif_init(), vifid(%d,%d,%d) &vif== %p, &ports[i]== %p \n",
    dp->port[i].vif.vifid.type,
    dp->port[i].vif.vifid.dev,
    dp->port[i].vif.vifid.num,
    ports, &dp->port[i]);
    dp->n_total++;
  }

  dp->port[i].vif.vifid.type = VIFT_CPU;
  dp->port[i].vif.vifid.dev = phys_dev (CPU_DEV);
  dp->port[i].vif.vifid.num = ++dp->n_by_type[dp->port[i].vif.vifid.type];
  dp->port[i].vif.local = &ports[i];
  dp->port[i].vif.trunk = NULL;
  dp->port[i].vif.islocal = 1;
  dp->port[i].vif.valid = 1;
  dp->port[i].vif.local->stack_role = PSR_NONE;
  dp->port[i].vif.local->ldev = CPU_DEV;
  dp->port[i].vif.local->lport = CPSS_CPU_PORT_NUM_CNS;
  dp->n_total++;


for (i = 0; i<= VIFT_PC; i++)
DEBUG("====vif_init(), dp->n_by_type[%d]== %d\n", i, dp->n_by_type[i]);
}

void
vif_post_port_init (void)
{
  struct vif_dev_ports *dp;
  int i;

  memset (vifp_by_hw, 0, sizeof (vifp_by_hw));

  dp = &vifs[stack_id];
  for (i = 0; i < NPORTS; i++) {
    vifp_by_hw[phys_dev (vifs[stack_id].port[i].ldev)][vifs[stack_id].port[i].lport] =
      (struct vif*)&vifs[stack_id].port[i];
  }
  vifp_by_hw[0][CPSS_CPU_PORT_NUM_CNS] = (struct vif*)&vifs[stack_id].port[i];
  vifp_by_hw[phys_dev (CPU_DEV)][CPSS_CPU_PORT_NUM_CNS] = (struct vif*)&vifs[stack_id].port[i];
}

struct vif*
vif_get (vif_type_t type, uint8_t dev, uint8_t num) {
//DEBUG(">>>>vif_get (%d, %d, %d)\n", type, dev, num);
  int i, o = 0;

  if (!in_range (type, VIFT_FE, VIFT_PC) ||
      !in_range (dev, 0, 15))
    return NULL;

  if (stack_id == 0) {
    if (dev != 0)
      return NULL;

  } else {
    if (dev == 0)
      dev = stack_id;
  }

  if (type == VIFT_PC) {
    if (!in_range (num, TRUNK_ID_MIN, TRUNK_ID_MAX))
      return NULL;
    return (struct vif*) &trunks[num];
  }

  if (num > vifs[dev].n_by_type[type])
    return NULL;

  for (i = 0; i < type; i++)
    o += vifs[dev].n_by_type[i];
  o += num - 1;

//DEBUG("====vif_get (...); dev== %d, o== %d\n", dev, o);

  assert (o < vifs[dev].n_total);

  return (struct vif*) &vifs[dev].port[o];
}

struct vif*
vif_get_by_pid (uint8_t dev, port_id_t num) {

  if (!in_range (dev, 0, 15))
    return NULL;

  if (stack_id == 0) {
    if (dev != 0)
      return NULL;

  } else {
    if (dev == 0)
      dev = stack_id;
  }

  if (!in_range(num, 1, 64))
    return NULL;

  num -= 1;

  if (!in_range(vifs[dev].port[num].vif.vifid.type, VIFT_FE, VIFT_XG))
    return NULL;

  return (struct vif*) &vifs[dev].port[num];
}

enum status
vif_get_hw (struct hw_port *hp, struct vif *vif) {

  if (vif == NULL)
    return ST_DOES_NOT_EXIST;

  if (vif->islocal) {
    hp->hw_dev  = phys_dev (vif->local->ldev);
    hp->hw_port = vif->local->lport;
  } else {
    hp->hw_dev  = vif->remote.hw_dev;
    hp->hw_port = vif->remote.hw_port;
  }
  return ST_OK;
}

struct vif*
vif_getn (vif_id_t id) {
DEBUG(">>>>vif_getn (%08x) &id == %p\n", id, &id);
  struct vif_id* vif = (struct vif_id*) &id;
DEBUG("====vif_getn () vif == %p\n", vif);
DEBUG("====vif_getn () vif->type== %d, vif->dev== %d, vif->num== %d\n", vif->type, vif->dev, vif->num);
  return vif_get(vif->type, vif->dev, vif->num);
}

enum status
vif_get_hw_port (struct hw_port *hp, vif_type_t type, uint8_t dev, uint8_t num)
{
  int i, o = 0, local;

  if (!in_range (type, VIFT_FE, VIFT_PC) ||
      !in_range (dev, 0, 15))
    return ST_BAD_VALUE;

  if (stack_id == 0) {
    if (dev != 0)
      return ST_BAD_VALUE;

    local = 1;
  } else {
    if (dev == 0)
      dev = stack_id;

    local = dev == stack_id;
  }

  if (num > vifs[dev].n_by_type[type])
    return ST_DOES_NOT_EXIST;

  for (i = 0; i < type; i++)
    o += vifs[dev].n_by_type[i];
  o += num - 1;

  assert (o < vifs[dev].n_total);

  if (local) {
    hp->hw_dev  = phys_dev (vifs[dev].port[o].vif.local->ldev);
    hp->hw_port = vifs[dev].port[o].vif.local->lport;
  } else {
    hp->hw_dev  = vifs[dev].port[o].vif.remote.hw_dev;
    hp->hw_port = vifs[dev].port[o].vif.remote.hw_port;
  }

  return ST_OK;
}

struct vif*
vif_get_by_gif(uint8_t type, uint8_t dev, uint8_t num)
{
  if (!in_range (type, GIFT_FE, GIFT_XG) ||
      !in_range (dev, 0, 15))
    return NULL;

  switch (type) {
    case GIFT_FE:
      return vif_get(VIFT_FE, dev, num);
    case GIFT_GE:
      return vif_get(VIFT_GE, dev, num);
    case GIFT_XG:
      return vif_get(VIFT_XG, dev, num);
    default:
      return NULL;
  }
}

enum status
vif_get_hw_port_by_index (struct hw_port *hp, uint8_t dev, uint8_t num)
{
  int local;

  if (!in_range (dev, 0, 15))
    return ST_BAD_VALUE;

  if (stack_id == 0) {
    if (dev != 0)
      return ST_BAD_VALUE;

    local = 1;
  } else {
    if (dev == 0)
      dev = stack_id;

    local = dev == stack_id;
  }

  if ((num >= 64) || !in_range(vifs[dev].port[num].vif.vifid.type, VIFT_FE, VIFT_PC))
    return ST_DOES_NOT_EXIST;

  if (local) {
    hp->hw_dev  = phys_dev (vifs[dev].port[num].vif.local->ldev);
    hp->hw_port = vifs[dev].port[num].vif.local->lport;
  } else {
    hp->hw_dev  = vifs[dev].port[num].vif.remote.hw_dev;
    hp->hw_port = vifs[dev].port[num].vif.remote.hw_port;
  }

  return ST_OK;
}

void
vif_set_trunk_members (trunk_id_t trunk, int nmem, struct trunk_member *mem) {
DEBUG(">>>>vif_set_trunk_members (%d, %d, )\n", trunk, nmem);

  struct trunk *ctrunk = trunks + trunk;
  int k;
for (k = 0; k < ctrunk->nports; k++)
  DEBUG("====1vif_set_trunk_members ( ) k= %d, vp= %x, e= %d\n", k, ctrunk->vif_port[k]->id, ctrunk->port_enabled[k]);


  int i = 0;

  while (i < ctrunk->nports) {
    int j;
    for (j = 0; j < nmem; j++) {
      struct vif* v = vif_get_by_gif (mem[j].id.type, mem[j].id.dev, mem[j].id.num);
      if (v == NULL)
        continue;
      if (v->id == ctrunk->vif_port[i]->id)
        break;
    }
    if (j == nmem) {
      ctrunk->vif_port[i]->trunk = NULL;
      for (j = i; j < ctrunk->nports-1; j++) {
        ctrunk->vif_port[j] = ctrunk->vif_port[j+1];
        ctrunk->port_enabled[j] = ctrunk->port_enabled[j+1];
      }
      ctrunk->vif_port[ctrunk->nports - 1] = NULL;
      ctrunk->port_enabled[ctrunk->nports - 1] = 0;
      ctrunk->nports--;
    } else
      i++;
  }

for (k = 0; k < ctrunk->nports; k++)
  DEBUG("====2vif_set_trunk_members ( ) k= %d, vp= %x, e= %d\n", k, ctrunk->vif_port[k]->id, ctrunk->port_enabled[k]);
  for (i = 0; i < nmem; i++) {
    struct vif* v = vif_get_by_gif (mem[i].id.type, mem[i].id.dev, mem[i].id.num);
    if (v == NULL)
      continue;
    int j;
    for (j = 0; j < ctrunk->nports; j++) {
      if (ctrunk->vif_port[j]->id == v->id) {
        ctrunk->port_enabled[j] = mem[i].enabled;
        ctrunk->vif_port[j]->trunk = NULL;
        ctrunk->vif_port[j] = v;
        ctrunk->vif_port[j]->trunk = (struct vif*)ctrunk;
        break;
      }
    }
    if (j == ctrunk->nports) {
      assert(ctrunk->vif_port[ctrunk->nports] == NULL);
      ctrunk->vif_port[ctrunk->nports] = v;
      ctrunk->port_enabled[ctrunk->nports] = mem[i].enabled;
      ctrunk->vif_port[ctrunk->nports]->trunk = (struct vif*)ctrunk;
      ctrunk->nports++;
    }
  }

  for (i = 0; i < ctrunk->nports; i++)
    if (ctrunk->port_enabled[i]) {
      ctrunk->designated = ctrunk->vif_port[i];
      break;
    }
  if (i == ctrunk->nports)
    ctrunk->designated = NULL;

for (k = 0; k < ctrunk->nports; k++)
  DEBUG("====3vif_set_trunk_members ( ) k= %d, vp= %x, e= %d, tr= %x\n",
      k, ctrunk->vif_port[k]->id, ctrunk->port_enabled[k], ctrunk->vif_port[k]->trunk->id);
if (ctrunk->designated)
DEBUG("====4vif_set_trunk_members ( ) designated= %x\n", ctrunk->designated->id);
}

enum status
vif_tx (const struct vif_id *id,
        const struct vif_tx_opts *opts,
        uint16_t size,
        const void *data)
{
  CPSS_DXCH_NET_DSA_PARAMS_STC tp;
  uint8_t tag[8];
  enum status result;
  struct hw_port hp;
  struct vif *vif, *vifp;
  trunk_id_t trunk = 0;

DEBUG(">>>>vif_tx (%x,, , size=%d )\n", *(uint32_t*) id, size);

  if (opts->find_iface_by_portid) {
    vifp = vif = vif_get_by_pid (id->dev, id->num);
  } else {
    vifp = vif = vif_get(id->type, id->dev, id->num);
DEBUG("====1vif_tx, vifp->id== %x, trunkid== %d\n", vifp->id, trunk);
    if (vif == NULL)
      return ST_DOES_NOT_EXIST;
    if (vif->vifid.type == VIFT_PC) {
      vifp = ((struct trunk*)vif)->designated;
      trunk = ((struct trunk*)vif)->id;
DEBUG("====2vif_tx, vifp->id== %x, trunkid== %d\n", vifp->id, trunk);
    }
  }

  result = vif_get_hw(&hp, vifp);
  if (result != ST_OK)
    return result;

DEBUG("====vif_tx, hp.hw_dev== %x, hp.hw_port== %d\n", hp.hw_dev, hp.hw_port);
  memset (&tp, 0, sizeof (tp));

  switch (opts->send_to)
  {
    case VIFD_PORT: {

      tp.commonParams.dsaTagType = CPSS_DXCH_NET_DSA_TYPE_EXTENDED_E;
      tp.commonParams.vid = opts->vid;
      tp.commonParams.vpt = 7;
      tp.dsaType = CPSS_DXCH_NET_DSA_CMD_FROM_CPU_E;
      tp.dsaInfo.fromCpu.tc = 7;
      tp.dsaInfo.fromCpu.dstInterface.type = CPSS_INTERFACE_PORT_E;
      tp.dsaInfo.fromCpu.dstInterface.devPort.devNum = hp.hw_dev;
      tp.dsaInfo.fromCpu.dstInterface.devPort.portNum = hp.hw_port;
      tp.dsaInfo.fromCpu.egrFilterEn = gt_bool ( !opts->ignore_stp);
      tp.dsaInfo.fromCpu.srcDev = stack_id;
      tp.dsaInfo.fromCpu.srcId = stack_id;

      struct port *port = port_ptr (id->num);

      if ((port->mode == PM_TRUNK) &&
          (port->native_vid != opts->vid) &&
          (opts->vid)) {

        tp.dsaInfo.fromCpu.extDestInfo.devPort.dstIsTagged = GT_TRUE;
      }

    } break;

    case VIFD_VLAN: {

      tp.commonParams.dsaTagType = CPSS_DXCH_NET_DSA_TYPE_EXTENDED_E;
      tp.commonParams.vid = opts->vid;
      tp.commonParams.vpt = 7;
      tp.dsaType = CPSS_DXCH_NET_DSA_CMD_FROM_CPU_E;
      tp.dsaInfo.fromCpu.tc = 7;
      tp.dsaInfo.fromCpu.dstInterface.type = CPSS_INTERFACE_VID_E;
      tp.dsaInfo.fromCpu.dstInterface.vlanId = opts->vid;
      tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludeInterface = gt_bool(opts->exclude_src_port);
      tp.dsaInfo.fromCpu.extDestInfo.multiDest.mirrorToAllCPUs = GT_TRUE;
      if (opts->exclude_src_port) {
        if (!trunk) {
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.type = CPSS_INTERFACE_PORT_E;
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.devPort.devNum = hp.hw_dev;
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.devPort.portNum = hp.hw_port;
        } else {
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.type = CPSS_INTERFACE_TRUNK_E;
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.trunkId = trunk;
        }
      }
      tp.dsaInfo.fromCpu.egrFilterEn = gt_bool ( !opts->ignore_stp);
      tp.dsaInfo.fromCpu.srcDev = stack_id;
      tp.dsaInfo.fromCpu.srcId = stack_id;
    } break;

    case VIFD_VIDX: {

      tp.commonParams.dsaTagType = CPSS_DXCH_NET_DSA_TYPE_EXTENDED_E;
      tp.commonParams.vid = opts->vid;
      tp.commonParams.vpt = 7;
      tp.dsaType = CPSS_DXCH_NET_DSA_CMD_FROM_CPU_E;
      tp.dsaInfo.fromCpu.tc = 7;
      tp.dsaInfo.fromCpu.dstInterface.type = CPSS_INTERFACE_VIDX_E;
      tp.dsaInfo.fromCpu.dstInterface.vidx = opts->mcg;
      tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludeInterface = gt_bool(opts->exclude_src_port);
      tp.dsaInfo.fromCpu.extDestInfo.multiDest.mirrorToAllCPUs = GT_TRUE;
      if (opts->exclude_src_port) {
        if (!trunk) {
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.type = CPSS_INTERFACE_PORT_E;
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.devPort.devNum = hp.hw_dev;
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.devPort.portNum = hp.hw_port;
        } else {
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.type = CPSS_INTERFACE_TRUNK_E;
          tp.dsaInfo.fromCpu.extDestInfo.multiDest.excludedInterface.trunkId = trunk;
        }
      }
      tp.dsaInfo.fromCpu.egrFilterEn = gt_bool ( !opts->ignore_stp);
      tp.dsaInfo.fromCpu.srcDev = stack_id;
      tp.dsaInfo.fromCpu.srcId = stack_id;
    } break;

    default: {
      return ST_BAD_VALUE;
    } break;
  }

  CRP (cpssDxChNetIfDsaTagBuild (CPU_DEV, &tp, tag));

  mgmt_send_gen_frame (tag, data, size);

  return ST_OK;
}

enum status
vif_get_hw_ports (struct vif_def *pd)
{
  struct vif_dev_ports *dp;
  int i;

  dp = &vifs[stack_id];

  for (i = 0; i < dp->n_total; i++) {
    *(vif_id_t*)&pd[i].id = dp->port[i].vif.id;
    pd[i].hp.hw_dev = phys_dev (dp->port[i].vif.local->ldev);
    pd[i].hp.hw_port = dp->port[i].vif.local->lport;
    pd[i].hp.sr = dp->port[i].vif.local->stack_role;
  }

  return ST_OK;
}

static void __attribute__ ((unused))
vif_dump_hw_ports (void)
{
  int d, p;

  DEBUG ("\r\n*** BEGIN KNOWN PORT DUMP ***\r\n");
  for (d = 0; d < 16; d++) {
    struct vif_dev_ports *dp = &vifs[d];

    for (p = 0; p < dp->n_total; p++) {
      char *t, *r;
      int hd, hp, sr;

      switch (dp->port[p].vif.vifid.type) {
      case VIFT_FE: t = "fastethernet"; break;
      case VIFT_GE: t = "gigabitethernet"; break;
      case VIFT_XG: t = "tengigabitethernet"; break;
      default:      t = "unknown";
      }

      if ((d == 0 && stack_id == 0) || d == stack_id - 1) {
        hd = phys_dev (dp->port[p].vif.local->ldev);
        hp = dp->port[p].vif.local->lport;
        sr = dp->port[p].vif.local->stack_role;
      } else {
        hd = dp->port[p].vif.remote.hw_dev;
        hp = dp->port[p].vif.remote.hw_port;
        sr = dp->port[p].vif.remote.sr;
      }

      switch (sr) {
      case PSR_PRIMARY:   r = "primary"; break;
      case PSR_SECONDARY: r = "secondary"; break;
      case PSR_NONE:      r = "none"; break;
      default:            r = "unknown";
      }

      DEBUG ("%s %d/%d, %d:%d, %s\r\n",
             t, dp->port[p].vif.vifid.dev, dp->port[p].vif.vifid.num,
             hd, hp, r);
    }
  }
  DEBUG ("\r\n*** END KNOWN PORT DUMP ***\r\n");
}

enum status
vif_set_hw_ports (uint8_t dev, uint8_t n, const struct vif_def *pd) {

  struct vif_dev_ports *dp;
  int i;

  if (!stack_active ()
      || !in_range (dev, 1, 16)
      || dev == stack_id
      || !in_range (n, 0, 60))
    return ST_BAD_VALUE;

  dp = &vifs[dev];
  memset (dp, 0, sizeof (*dp));

  memset (vifp_by_hw[dev], 0, sizeof(vifp_single_dev_t));
  memset (vifp_by_hw[dev + NEXTDEV_INC], 0, sizeof(vifp_single_dev_t));
DEBUG("memset (%p, 0, %d)", &vifp_by_hw[dev], sizeof(vifp_single_dev_t));

  for (i = 0; i < n; i++) {
    assert(pd[i].hp.hw_dev == dev || pd[i].hp.hw_dev == dev + NEXTDEV_INC);
    dp->port[i].vif.id = *(vif_id_t*)&pd[i].id;
DEBUG("====set_vif_port() in: %x\n", *(vif_id_t*)&pd[i].id);
    memcpy (&dp->port[i].vif.remote, &pd[i].hp, sizeof (struct hw_port));
    dp->port[i].vif.set_speed = vif_set_speed_remote;
    dp->port[i].vif.valid = 1;
    dp->n_by_type[pd[i].id.type]++;
    dp->n_total++;
    vifp_by_hw[pd[i].hp.hw_dev][pd[i].hp.hw_port] = (struct vif*)&dp->port[i];
  }
for (i = 0; i<= VIFT_PC; i++)
DEBUG("====set_vif_(), dp->n_by_type[%d]== %d\n", i, dp->n_by_type[i]);

  return ST_OK;
}

enum status
vif_set_speed_remote (struct vif *vif, const struct port_speed_arg *psa) {
  return ST_REMOTE;
}

enum status
vif_set_speed_port (struct vif *vif, const struct port_speed_arg *psa) {
DEBUG(">>>>vif_set_speed_port (%p, const struct port_speed_arg *psa), vif->n == %d\n", vif, vif->vifid.num);
  struct port *port = (struct port*) vif;
DEBUG("====vif_set_speed_port (), port == %p, pid == %d\n", port, port->id);
  return port_set_speed (port->id, psa);
}

enum status
vif_set_speed_trunk (struct vif *vif, const struct port_speed_arg *psa) {
  struct trunk *trunk = (struct trunk*) vif;
  struct vif **vifp = (struct vif**) &trunk->vif_port;
  enum status status = ST_OK;
  while (*vifp) {
    enum status st;
    st = (*vifp)->set_speed (*vifp, psa);
    if (st != ST_OK)
      status = st;
    vifp++;
  }
  return status;
}

enum status
vif_set_speed (vif_id_t nvif, const struct port_speed_arg *psa) {
DEBUG(">>>>vif_set_speed (%08x, const struct port_speed_arg *psa)\n", nvif);
  struct vif *vif = vif_getn (nvif);
DEBUG("====vif_set_speed (...) vif== %p\n, vif->n == %d\n", vif, vif->vifid.num);

  vif->c_speed = psa->speed;
  vif->c_speed_auto = psa->speed_auto;

  return vif->set_speed (vif, psa);
}
