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
#include <pthread.h>
#include <assert.h>

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>

struct vif_dev_ports {
  int local;
  int n_total;
  serial_t vif_link_state_serial;
  int n_by_type[VIFT_PORT_TYPES];
  struct port port[CPSS_MAX_PORTS_NUM_CNS];
};

static struct vif_dev_ports vifs[16];
static serial_t vif_stp_data_serial[16];

vifp_single_dev_t vifp_by_hw[32];

static pthread_rwlock_t vif_lock = PTHREAD_RWLOCK_INITIALIZER;

void
vif_rlock (void) {
  pthread_rwlock_rdlock (&vif_lock);
}

void
vif_wlock (void) {
  pthread_rwlock_wrlock (&vif_lock);
}

void
vif_unlock (void) {
  pthread_rwlock_unlock (&vif_lock);
}

static void
vif_fill_cpss_if_port (struct vif* vif, CPSS_INTERFACE_INFO_STC *iface) {

  iface->type = CPSS_INTERFACE_PORT_E;
  if (vif->islocal) {
    iface->devPort.devNum = phys_dev(vif->local->ldev);
    iface->devPort.portNum = vif->local->lport;
  } else {
    iface->devPort.devNum = vif->remote.hw_dev;
    iface->devPort.portNum = vif->remote.hw_port;
  }
}

static void
vif_fill_cpss_if_trunk (struct vif* vif, CPSS_INTERFACE_INFO_STC *iface) {

  iface->type = CPSS_INTERFACE_TRUNK_E;
  iface->trunkId = ((struct trunk*)vif)->id;
}

void
vif_init (void)
{
  struct vif_dev_ports *dp;
  int i, j;

  memset (vif_stp_data_serial, 0, sizeof(vif_stp_data_serial));
  vif_stp_data_serial[stack_id] = 1;

  memset (vifs, 0, sizeof (vifs));

  dp = &vifs[stack_id];
DEBUG("====vif_init(), dp= %p\n", dp);
DEBUG("====vif_init(), sizeof(str port)= %x\n", sizeof(struct port));

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
    int j;
    for (j = 0; j < 256; j++)
      dp->port[i].vif.stg_state[j] = STP_STATE_DISABLED;
    dp->port[i].vif.trust_cos = 0;
    dp->port[i].vif.trust_dscp = 0;
    dp->port[i].vif.c_speed = PORT_SPEED_AUTO;
    dp->port[i].vif.c_speed_auto = 1;
    dp->port[i].vif.c_duplex = PORT_DUPLEX_AUTO;
    dp->port[i].vif.c_shutdown = 0;

    dp->port[i].vif.state.link = 0;
    dp->port[i].vif.state.speed = 0;
    dp->port[i].vif.state.duplex = 0;

    vif_port_proc_init(&dp->port[i].vif);

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

void
vif_remote_proc_init(struct vif* v) {
  v->set_speed = vif_set_speed_remote;
  v->set_duplex = vif_set_duplex_remote;
  v->fill_cpss_if = vif_fill_cpss_if_port;
}

void
vif_port_proc_init(struct vif* v) {
  v->set_speed = vif_set_speed_port;
  v->set_duplex = vif_set_duplex_port;
  v->fill_cpss_if = vif_fill_cpss_if_port;
}

void
vif_trunk_proc_init(struct vif* v) {
  v->set_speed = vif_set_speed_trunk;
  v->set_duplex = vif_set_duplex_trunk;
  v->fill_cpss_if = vif_fill_cpss_if_trunk;
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
//DEBUG(">>>>vif_getn (%08x) &id == %p\n", id, &id);
  struct vif_id* vif = (struct vif_id*) &id;
//DEBUG("====vif_getn () vif == %p\n", vif);
//DEBUG("====vif_getn () vif->type== %d, vif->dev== %d, vif->num== %d\n", vif->type, vif->dev, vif->num);
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

static void
notify_port_state (vif_id_t vifid, port_id_t pid, const struct port_link_state *ps, void *sock) {
  zmsg_t *msg = zmsg_new ();
  assert (msg);
  zmsg_addmem (msg, &vifid, sizeof (vifid));
  zmsg_addmem (msg, &pid, sizeof (pid));
  zmsg_addmem (msg, ps, sizeof (*ps));
  zmsg_send (&msg, sock);
}

/* should be vif_wlock() protected in callink function */
static enum status
vif_update_trunk_link_status(struct trunk* trunk, void *socket) {

  int i;
  struct port_link_state pls = {.link = 0, .speed = PORT_SPEED_10, .duplex = 0};
  for (i = 0; i < trunk->nports; i++) {
    if (trunk->port_enabled[i]) {
      if (trunk->vif_port[i]->state.link) {
        pls.link = 1;
        if (trunk->vif_port[i]->state.speed > pls.speed)
          pls.speed = trunk->vif_port[i]->state.speed;
        if (trunk->vif_port[i]->state.duplex > pls.duplex)
          pls.duplex = trunk->vif_port[i]->state.duplex;
      }
    }
  }

  if (pls.link != trunk->vif.state.link
      || pls.speed != trunk->vif.state.speed
      || pls.duplex != trunk->vif.state.duplex) {

    notify_port_state(trunk->vif.id, 0, &pls, socket);

    trunk->vif.state.link = pls.link;
    trunk->vif.state.speed = pls.speed;
    trunk->vif.state.duplex = pls.duplex;
  }
  return ST_OK;
}

enum status
vif_set_link_status(vif_id_t vifid, struct port_link_state *state, void *socket) {

  static uint8_t buf[sizeof(struct vif_link_state_header) + sizeof(struct vif_link_state)];
  struct vif_link_state_header *vif_lsh = (struct vif_link_state_header*) buf;

  vif_wlock();

  vif_lsh->n = 1;
  vif_lsh->stack_id = stack_id;
  vif_lsh->serial = ++vifs[stack_id].vif_link_state_serial;
  vif_lsh->data[0].vifid = vifid;
  memcpy(&vif_lsh->data[0].state, state, sizeof(*state));
  mac_op_send_vif_ls(vif_lsh);

  struct vif *vif = vif_getn(vifid);
  if (!vif) {
    vif_unlock();
    return ST_DOES_NOT_EXIST;
  }

  vif->state.link = state->link;
  vif->state.speed = state->speed;
  vif->state.duplex = state->duplex;

  if (!vif->trunk) {
    vif_unlock();
    return ST_OK;
  }

#if 0
  int i;
  struct port_link_state pls = {0,0,0};
  for (i = 0; i < ((struct trunk*) vif->trunk)->nports; i++) {
    if (((struct trunk*) vif->trunk)->port_enabled[i]) {
      if (((struct trunk*) vif->trunk)->vif_port[i]->state.link) {
        pls.link = 1;
        if (((struct trunk*) vif->trunk)->vif_port[i]->state.speed > pls.speed)
          pls.speed = ((struct trunk*) vif->trunk)->vif_port[i]->state.speed;
        if (((struct trunk*) vif->trunk)->vif_port[i]->state.duplex > pls.duplex)
          pls.duplex = ((struct trunk*) vif->trunk)->vif_port[i]->state.duplex;
      }
    }
  }

  if (pls.link != vif->trunk->state.link
      || pls.speed != vif->trunk->state.speed
      || pls.duplex != vif->trunk->state.duplex) {

    notify_port_state(vif->trunk->id, 0, pls, socket);

    vif->trunk->state.link = pls.link;
    vif->trunk->state.speed = pls.speed;
    vif->trunk->state.duplex = pls.duplex;
  }
#endif //0

  vif_update_trunk_link_status((struct trunk*)vif->trunk, socket);

  vif_unlock();
  return ST_OK;
}

enum status
vif_process_ls_pkt(struct vif_link_state_header *vif_lsh, void *socket) {
DEBUG(">>>>>vif_process_ls_pkt(%p) vif_lsh->n== %d, ->stack_id== %d, ->serial== %llu\n",
   vif_lsh, vif_lsh->n, vif_lsh->stack_id, vif_lsh->serial);

  vif_wlock();
  if (vif_lsh->serial <= vifs[vif_lsh->stack_id].vif_link_state_serial || vif_lsh->stack_id == stack_id) {
    vif_unlock();
    return ST_ALREADY_EXISTS;
  }

  vif_lsh->serial = vifs[vif_lsh->stack_id].vif_link_state_serial;
  int i;
  for (i = 0; i < vif_lsh->n; i++) {
    struct vif *vif = vif_getn(vif_lsh->data[i].vifid);
    if (!vif) {
DEBUG("====vif_process_ls_pkt() vifid= %x SKIPPED\n", vif_lsh->data[i].vifid);
      continue;
    }
    memcpy(&vif->state, &vif_lsh->data[0].state, sizeof(vif->state));
    if (vif->trunk)
      vif_update_trunk_link_status((struct trunk*)vif->trunk, socket);
  }

  vif_unlock();
  return ST_OK;
}

struct vif_link_state_header *
vif_form_ls_sync_pkt(void *buf, size_t buflen) {
DEBUG(">>>>vif_form_ls_sync_pkt(void)\n");
  if (sizeof(struct vif_link_state_header) + NPORTS * sizeof(struct vif_link_state) > buflen)
    return NULL;

  struct vif_link_state_header *vif_lsh = (struct vif_link_state_header*) buf;

  vif_rlock();

  vif_lsh->n = NPORTS;
  vif_lsh->stack_id = stack_id;
  vif_lsh->serial = vifs[stack_id].vif_link_state_serial;

  int i;
  for (i = 0; i < NPORTS; i++) {
    struct vif *vif = vif_get_by_pid(stack_id, i + 1);
    if (!vif)
      continue;
    vif_lsh->data[i].vifid = vif->id;
    memcpy(&vif_lsh->data[i].state, &vif->state, sizeof(vif_lsh->data[i].state));
  }

  vif_unlock();
  return vif_lsh;
}

void
vif_set_trunk_members (trunk_id_t trunk, int nmem, struct trunk_member *mem, void *socket) {
DEBUG(">>>>vif_set_trunk_members (%d, %d, )\n", trunk, nmem);

  struct trunk *ctrunk = trunks + trunk;
  int k;
for (k = 0; k < ctrunk->nports; k++)
  DEBUG("====1vif_set_trunk_members ( ) k= %d, vp= %x, e= %d\n", k, ctrunk->vif_port[k]->id, ctrunk->port_enabled[k]);

  int i = 0;

  vif_wlock();

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

  vif_update_trunk_link_status(ctrunk, socket);

  vif_unlock();

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

  if ((opts->send_to != VIFD_VLAN && opts->send_to != VIFD_VIDX) || opts->exclude_src_port) {
    if (opts->find_iface_by_portid) {
      vifp = vif = vif_get_by_pid (id->dev, id->num);
    } else {
      vifp = vif = vif_get(id->type, id->dev, id->num);
//DEBUG("====1vif_tx, vifp->id== %x, trunkid== %d\n", vifp->id, trunk);
      if (vif == NULL)
        return ST_DOES_NOT_EXIST;
      if (vif->vifid.type == VIFT_PC) {
        vifp = ((struct trunk*)vif)->designated;
        trunk = ((struct trunk*)vif)->id;
//DEBUG("====2vif_tx, vifp->id== %x, trunkid== %d\n", vifp->id, trunk);
      }
    }

    result = vif_get_hw(&hp, vifp);
    if (result != ST_OK)
      return result;
  }
//DEBUG("====vif_tx, hp.hw_dev== %x, hp.hw_port== %d\n", hp.hw_dev, hp.hw_port);
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
      || !in_range (dev, 1, 15)
      || dev == stack_id
      || !in_range (n, 0, 60))
    return ST_BAD_VALUE;

  vif_wlock();

  dp = &vifs[dev];
  memset (dp, 0, sizeof (*dp));

  memset (vifp_by_hw[dev], 0, sizeof(vifp_single_dev_t));
  memset (vifp_by_hw[dev + NEXTDEV_INC], 0, sizeof(vifp_single_dev_t));

  for (i = 0; i < n; i++) {
    assert(pd[i].hp.hw_dev == dev || pd[i].hp.hw_dev == dev + NEXTDEV_INC);
    dp->port[i].vif.id = *(vif_id_t*)&pd[i].id;
DEBUG("====set_vif_port() in: %x\n", *(vif_id_t*)&pd[i].id);
    memcpy (&dp->port[i].vif.remote, &pd[i].hp, sizeof (struct hw_port));
    vif_remote_proc_init(&dp->port[i].vif);
    dp->port[i].vif.valid = 1;
    dp->n_by_type[pd[i].id.type]++;
    dp->n_total++;
    vifp_by_hw[pd[i].hp.hw_dev][pd[i].hp.hw_port] = (struct vif*)&dp->port[i];
  }

  vif_unlock();
for (i = 0; i<= VIFT_PC; i++)
DEBUG("====set_vif_(), dp->n_by_type[%d]== %d\n", i, dp->n_by_type[i]);

  return ST_OK;
}

enum status
//vif_stg_get (serial_t *ser, struct vif_stg *st) {
vif_stg_get (void *b) {
  struct vif_dev_ports *dp;
  int i;
  struct vif_stgblk_header *hd = (struct vif_stgblk_header*) b;

  uint16_t msk0 = 1;
  msk0 <<= STP_STATE_BITS_WIDTH;
  msk0--;
  uint8_t msk = msk0;
  dp = &vifs[stack_id];
//  *(uint8_t*)b = dp->n_total;
  hd->n = dp->n_total;
//  *((uint8_t*)b + 1) = stack_id;
  hd->st_id = stack_id;
//  *(serial_t*)((uint8_t*)b + 2) = vif_stp_data_serial[stack_id];
  hd->serial = vif_stp_data_serial[stack_id];
//  struct vif_stg *st = (struct vif_stg *)((serial_t*)((uint8_t*)b + 2) + 1);
  struct vif_stg *st = (struct vif_stg *) hd->data;

  for (i = 0; i < dp->n_total; i++) {
    *(vif_id_t*)&st[i].id = dp->port[i].vif.id;
    int j;
    for (j = 0; j < 256; j++)
      st[i].stgs[j / STP_STATES_PER_BYTE] =
        (st[i].stgs[j / STP_STATES_PER_BYTE] & ~(msk << j % STP_STATES_PER_BYTE * STP_STATE_BITS_WIDTH))
          | (dp->port[i].vif.stg_state[j] << j % STP_STATES_PER_BYTE * STP_STATE_BITS_WIDTH);
  }
  return ST_OK;
}

enum status
vif_stg_set (void *b) {
DEBUG(">>>>vif_stg_set (%p)\n", b);
//PRINTHexDump(b, sizeof(struct vif_stgblk_header) + sizeof(struct vif_stg) * ((struct vif_stgblk_header*) b) ->n);

  struct vif_stgblk_header *hd = (struct vif_stgblk_header*) b;
//  int i, n = *(uint8_t*)b;
  int i, n = hd->n;
//  uint8_t dev = *((uint8_t*)b + 1);
  uint8_t dev = hd->st_id;

  if (!stack_active ()
      || !in_range (dev, 1, 15)
      || dev == stack_id
      || !in_range (n, 0, 60))
    return ST_BAD_VALUE;

//  if (vif_stp_data_serial[dev] > *(serial_t*)((uint8_t*)b + 2))
  if (vif_stp_data_serial[dev] > hd->serial)
    return ST_OK;

  uint16_t msk0 = 1;
  msk0 <<= STP_STATE_BITS_WIDTH;
  msk0--;
  uint8_t msk = msk0;
//  struct vif_stg *st = (struct vif_stg *)((serial_t*)((uint8_t*)b + 2) + 1);
  struct vif_stg *st = (struct vif_stg *) hd->data;

  vif_wlock();
//  vif_stp_data_serial[dev] = *(serial_t*)((uint8_t*)b + 2);
  vif_stp_data_serial[dev] = hd->serial;

  for (i = 0; i < n; i++) {
    struct vif *vif = vif_getn(*(vif_id_t*)&st[i].id);
    if (!vif)
      continue;
    int j;
    for (j = 0; j < 256; j++) {
      vif->stg_state[j] =
        st[i].stgs[j / STP_STATES_PER_BYTE] & (msk << j % STP_STATES_PER_BYTE * STP_STATE_BITS_WIDTH);
    }
  }

  vif_unlock();

  return ST_OK;
}

enum status
vif_stg_get_single (struct vif *vif, uint8_t *buf, int inc_serial) {
  struct vif_stgblk_header *hd = (struct vif_stgblk_header*) buf;
  if (inc_serial)
    vif_stp_data_serial[stack_id]++;

  uint16_t msk0 = 1;
  msk0 <<= STP_STATE_BITS_WIDTH;
  msk0--;
  uint8_t msk = msk0;

//  *buf = 1;
  hd->n  = 1;
//  *(buf + 1) = stack_id;
  hd->st_id = stack_id;
//  *(serial_t*)(buf + 2) = vif_stp_data_serial[stack_id];
  hd->serial = vif_stp_data_serial[stack_id];

//  struct vif_stg *st = (struct vif_stg *)((serial_t*)(buf + 2) + 1);
  struct vif_stg *st = (struct vif_stg*)hd->data;
  *(vif_id_t*)&st[0].id = vif->id;
  int j;
  for (j = 0; j < 256; j++)
    st[0].stgs[j / STP_STATES_PER_BYTE] =
      (st[0].stgs[j / STP_STATES_PER_BYTE] & ~(msk << j % STP_STATES_PER_BYTE * STP_STATE_BITS_WIDTH))
        | (vif->stg_state[j] << j % STP_STATES_PER_BYTE * STP_STATE_BITS_WIDTH);
  return ST_OK;
}

#if 0
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
  if (!vif)
    return ST_DOES_NOT_EXIST;

  vif->c_speed = psa->speed;
  vif->c_speed_auto = psa->speed_auto;

  return vif->set_speed (vif, psa);
}
#endif

#define VIF_PROC_REMOTE(proc, arg...) \
enum status \
vif_##proc##_remote (struct vif *vif, ##arg) { \
  return ST_REMOTE; \
}

#define VIF_PROC_PORT_HEAD(proc, arg...) \
enum status \
vif_##proc##_port (struct vif *vif, ##arg)

#define VIF_PROC_PORT_BODY(proc, arg...) \
DEBUG(">>>>vif_" #proc "_port (%p, ...), vif->n == %d\n", vif, vif->vifid.num); \
  struct port *port = (struct port*) vif; \
DEBUG("====vif_" #proc "_port (), port == %p, pid == %d\n", port, port->id); \
  return port_##proc (port->id, ##arg);

#define VIF_PROC_TRUNK_HEAD(proc, arg...) \
enum status \
vif_##proc##_trunk (struct vif *vif, ##arg)

#define VIF_PROC_TRUNK_BODY(proc, arg...) \
  struct trunk *trunk = (struct trunk*) vif; \
  struct vif **vifp = (struct vif**) &trunk->vif_port; \
  enum status status = ST_OK; \
  while (*vifp) { \
    enum status st; \
    st = (*vifp)-> proc (*vifp, ##arg); \
    if (st != ST_OK) \
      status = st; \
    vifp++; \
  } \
  return status;

#define VIF_PROC_ROOT_HEAD(proc, arg...) \
enum status \
vif_##proc (vif_id_t nvif, ##arg)

#define VIF_PROC_ROOT_BODY(proc, arg...) \
DEBUG(">>>>vif_" #proc "(%08x, ...)\n", nvif); \
  struct vif *vif = vif_getn (nvif); \
DEBUG("====vif_" #proc "(...) vif== %p\n, vif->n == %d\n", vif, vif->vifid.num); \
  if (!vif) \
    return ST_DOES_NOT_EXIST; \
  return vif->proc (vif, ##arg);

VIF_PROC_REMOTE(set_speed, const struct port_speed_arg *psa)

VIF_PROC_PORT_HEAD(set_speed, const struct port_speed_arg *psa)
{
VIF_PROC_PORT_BODY(set_speed, psa)
}

VIF_PROC_TRUNK_HEAD(set_speed, const struct port_speed_arg *psa)
{
VIF_PROC_TRUNK_BODY(set_speed, psa)
}

VIF_PROC_ROOT_HEAD(set_speed, const struct port_speed_arg *psa)
{
VIF_PROC_ROOT_BODY(set_speed, psa)
}


VIF_PROC_REMOTE(set_duplex, enum port_duplex duplex)

VIF_PROC_PORT_HEAD(set_duplex, enum port_duplex duplex)
{
VIF_PROC_PORT_BODY(set_duplex, duplex)
}

VIF_PROC_TRUNK_HEAD(set_duplex, enum port_duplex duplex)
{
VIF_PROC_TRUNK_BODY(set_duplex, duplex)
}

VIF_PROC_ROOT_HEAD(set_duplex, enum port_duplex duplex)
{
VIF_PROC_ROOT_BODY(set_duplex, duplex)
}
