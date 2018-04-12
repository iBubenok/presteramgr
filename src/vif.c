#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <presteramgr.h>
#include <gif.h>
#include <vif.h>
#include <port.h>
#include <trunk.h>
#include <stack.h>
#include <mcg.h>
#include <utils.h>
#include <dev.h>
#include <mgmt.h>
#include <debug.h>
#include <sysdeps.h>
#include <pthread.h>
#include <assert.h>

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgEgrFlt.h>

struct vif_dev_ports {
  int local;
  int n_total;
  int is_active;
  serial_t vif_link_state_serial;
  int n_by_type[VIFT_PORT_TYPES];
  struct port port[CPSS_MAX_PORTS_NUM_CNS];
};

static struct vif_dev_ports vifs[16];
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

  memset (vifs, 0, sizeof (vifs));

  for (i = 0; i < 16; i++) {
    for (j = 0; j < CPSS_MAX_PORTS_NUM_CNS; j++) {
      vif_remote_proc_init(&vifs[i].port[j].vif);
    }
  }

  dp = &vifs[stack_id];
DEBUG("====vif_init(), dp= %p\n", dp);
DEBUG("====vif_init(), sizeof(str port)= %x\n", sizeof(struct port));

  ports = dp->port;
  dp-> is_active = 1;
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
    dp->port[i].vif.state.speed = PORT_SPEED_10;
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
  v->fill_cpss_if = vif_fill_cpss_if_port;
  v->mcg_add_vif = vif_mcg_add_vif_remote;
  v->mcg_del_vif = vif_mcg_del_vif_remote;
  v->set_speed = vif_set_speed_remote;
  v->set_duplex = vif_set_duplex_remote;
  v->shutdown = vif_shutdown_remote;
  v->block = vif_block_remote;
  v->set_access_vid = vif_set_access_vid_remote;
  v->set_comm = vif_set_comm_remote;
  v->set_customer_vid = vif_set_customer_vid_remote;
  v->set_mode = vif_set_mode_remote;
  v->set_pve_dst = vif_set_pve_dst_remote;
  v->set_protected = vif_set_protected_remote;
  v->set_native_vid = vif_set_native_vid_remote;
  v->set_trunk_vlans = vif_set_trunk_vlans_remote;
  v->vlan_translate = vif_vlan_translate_remote;
  v->set_stp_state = vif_set_stp_state_remote;
  v->dgasp_op = vif_dgasp_op_remote;
  v->set_flow_control = vif_set_flow_control_remote;
  v->set_voice_vid = vif_set_voice_vid_remote;
}

void
vif_port_proc_init(struct vif* v) {
  v->fill_cpss_if = vif_fill_cpss_if_port;
  v->mcg_add_vif = vif_mcg_add_vif_port;
  v->mcg_del_vif = vif_mcg_del_vif_port;
  v->set_speed = vif_set_speed_port;
  v->set_duplex = vif_set_duplex_port;
  v->shutdown = vif_shutdown_port;
  v->block = vif_block_port;
  v->set_access_vid = vif_set_access_vid_port;
  v->set_comm = vif_set_comm_port;
  v->set_customer_vid = vif_set_customer_vid_port;
  v->set_mode = vif_set_mode_port;
  v->set_pve_dst = vif_set_pve_dst_port;
  v->set_protected = vif_set_protected_port;
  v->set_native_vid = vif_set_native_vid_port;
  v->set_trunk_vlans = vif_set_trunk_vlans_port;
  v->vlan_translate = vif_vlan_translate_port;
  v->set_stp_state = vif_set_stp_state_port;
  v->dgasp_op = vif_dgasp_op_port;
  v->set_flow_control = vif_set_flow_control_port;
  v->set_voice_vid = vif_set_voice_vid_port;
}

void
vif_trunk_proc_init(struct vif* v) {
  v->fill_cpss_if = vif_fill_cpss_if_trunk;
  v->mcg_add_vif = vif_mcg_add_vif_trunk;
  v->mcg_del_vif = vif_mcg_del_vif_trunk;
  v->set_speed = vif_set_speed_trunk;
  v->set_duplex = vif_set_duplex_trunk;
  v->shutdown = vif_shutdown_trunk;
  v->block = vif_block_trunk;
  v->set_access_vid = vif_set_access_vid_trunk;
  v->set_comm = vif_set_comm_trunk;
  v->set_customer_vid = vif_set_customer_vid_trunk;
  v->set_mode = vif_set_mode_trunk;
  v->set_pve_dst = vif_set_pve_dst_trunk;
  v->set_protected = vif_set_protected_trunk;
  v->set_native_vid = vif_set_native_vid_trunk;
  v->set_trunk_vlans = vif_set_trunk_vlans_trunk;
  v->vlan_translate = vif_vlan_translate_trunk;
  v->set_stp_state = vif_set_stp_state_trunk;
  v->dgasp_op = vif_dgasp_op_trunk;
  v->set_flow_control = vif_set_flow_control_trunk;
  v->set_voice_vid = vif_set_voice_vid_trunk;
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

  if (num > vifs[dev].n_by_type[type] || num == 0)
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
  enum event_notification en = EN_LS;
  zmsg_addmem (msg, &en, sizeof (en));
  zmsg_addmem (msg, &vifid, sizeof (vifid));
  zmsg_addmem (msg, &pid, sizeof (pid));
  zmsg_addmem (msg, ps, sizeof (*ps));
  zmsg_send (&msg, sock);
}

/* should be vif_wlock() protected in callink function */
static enum status
vif_update_trunk_link_status(struct trunk* trunk, void *socket) {
DEBUG(">>>>vif_update_trunk_link_status(%p - %x, void *socket)\n", trunk, trunk->vif.id);

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

    if (stack_id == master_id)
      notify_port_state(trunk->vif.id, 0, &pls, socket);

    trunk->vif.state.link = pls.link;
    trunk->vif.state.speed = pls.speed;
    trunk->vif.state.duplex = pls.duplex;
  }
  return ST_OK;
}

enum status
vif_set_link_status(vif_id_t vifid, struct port_link_state *state, void *socket) {
DEBUG(">>>>vif_set_link_status(%x, %d:%d:%d, void *socket)\n", vifid, state->link, state->speed, state->duplex);

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
    memcpy(&vif->state, &vif_lsh->data[i].state, sizeof(vif->state));
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
for (k = 0; k < nmem; k++) {
  DEBUG("===00vif_set_trunk_members () k=%d, mem[k].ena= %d, mem[k].type=%d, mem[k].dev=%d, mem[k].num=%d\n", k, mem[k].enabled, mem[k].id.type, mem[k].id.dev, mem[k].id.num);
}
for (k = 0; k < ctrunk->nports; k++)
  DEBUG("====1vif_set_trunk_members ( ) k= %d, vp= %x, e= %d\n", k, ctrunk->vif_port[k]->id, ctrunk->port_enabled[k]);

  int i = 0;

  vif_wlock();

  while (i < ctrunk->nports) {
    int j;
    if (!ctrunk->vif_port[i]->valid)
      j = nmem;
    else {
      for (j = 0; j < nmem; j++) {
        struct vif* v = vif_get_by_gif (mem[j].id.type, mem[j].id.dev, mem[j].id.num);
        if (v == NULL)
          continue;
        if (v->id == ctrunk->vif_port[i]->id)
          break;
      }
    }
    if (j == nmem) {
      ctrunk->vif_port[i]->trunk = NULL;
      struct mcg_in_trunk *mcgp, *mcgpt;
      HASH_ITER (hh, ctrunk->mcg_head, mcgp, mcgpt) {
        vif_mcg_del_vif(ctrunk->vif_port[i]->id, mcgp->mcg_key);
      }
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
        if (mem[i].enabled && !ctrunk->port_enabled[j]) {
          struct mcg_in_trunk *mcgp, *mcgpt;
          HASH_ITER (hh, ctrunk->mcg_head, mcgp, mcgpt) {
            vif_mcg_add_vif(ctrunk->vif_port[j]->id, mcgp->mcg_key);
          }
        }
        if (!mem[i].enabled && ctrunk->port_enabled[j]) {
          struct mcg_in_trunk *mcgp, *mcgpt;
          HASH_ITER (hh, ctrunk->mcg_head, mcgp, mcgpt) {
            vif_mcg_del_vif(ctrunk->vif_port[j]->id, mcgp->mcg_key);
          }
        }
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
      if (mem[i].enabled) {
        struct mcg_in_trunk *mcgp, *mcgpt;
        HASH_ITER (hh, ctrunk->mcg_head, mcgp, mcgpt) {
          vif_mcg_del_vif(ctrunk->vif_port[ctrunk->nports]->id, mcgp->mcg_key);
        }
      }
      ctrunk->nports++;
    }
  }

  ctrunk->designated = NULL;
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
  struct vif *vif, *vifp = NULL;
  trunk_id_t trunk = 0;
  enum port_mode mode = 0;
  vid_t native_vid = 0, voice_vid = 0;

//int deb = (*(uint8_t*)data != 1);
//if (deb)
//DEBUG(">>>>vif_tx (%x,, , size=%d )\n", *(uint32_t*) id, size);

  if ((opts->send_to != VIFD_VLAN && opts->send_to != VIFD_VIDX) || opts->exclude_src_port) {

    vif_rlock();

    if (opts->find_iface_by_portid) {
      vifp = vif = vif_get_by_pid (id->dev, id->num);
      if (vif == NULL || !vif->valid) {
        vif_unlock();
        return ST_DOES_NOT_EXIST;
      }
    } else {
      vifp = vif = vif_get(id->type, id->dev, id->num);
//if (deb)
//DEBUG("====1vif_tx, vifp->id== %x, trunkid== %d\n", vifp->id, trunk);
      if (vif == NULL || !vif->valid) {
        vif_unlock();
        return ST_DOES_NOT_EXIST;
      }
      if (vif->vifid.type == VIFT_PC) {
        vifp = ((struct trunk*)vif)->designated;
        if (vifp == NULL || !vifp->valid) {
          vif_unlock();
          return ST_DOES_NOT_EXIST;
        }
        trunk = ((struct trunk*)vif)->id;
//if (deb)
//DEBUG("====2vif_tx, vifp->id== %x, trunkid== %d\n", vifp->id, trunk);
      }
    }

    result = vif_get_hw(&hp, vifp);
    mode = vifp->mode;
    native_vid = vifp->native_vid;
    voice_vid = vifp->voice_vid;

    vif_unlock();
    if (result != ST_OK)
      return result;
  }
//if (deb)
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

//if (deb)
//DEBUG("====vif_tx, mode: %d, native_vid: %d, opts->vid %d\n", mode, native_vid, opts->vid);
      if ((mode == PM_TRUNK) &&
          (native_vid != opts->vid) &&
          (opts->vid)) {
//if (deb)
//DEBUG("====vif_tx, TAGGED\n");
        tp.dsaInfo.fromCpu.extDestInfo.devPort.dstIsTagged = GT_TRUE;
      } else if ((mode == PM_ACCESS) &&
                 (voice_vid != 0) &&
                 (voice_vid == opts->vid)) {
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

  dp->local = 0;
  dp->is_active = 0;
  for (i = 0; i < CPSS_MAX_PORTS_NUM_CNS; i++)
    dp->port[i].vif.valid = 0;
  if (n > 0) {
    dp->n_total = 0;
    for (i = 0; i < VIFT_PORT_TYPES; i++)
      dp->n_by_type[i] = 0;
  }
//  memset (dp, 0, sizeof (*dp));

//  memset (vifp_by_hw[dev], 0, sizeof(vifp_single_dev_t));
//  memset (vifp_by_hw[dev + NEXTDEV_INC], 0, sizeof(vifp_single_dev_t));

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

void
vif_ls_clear_serial (devsbmp_t newdevs_bmp)
{
  int i;
  for (i = 1; i <= 15; i++) {
    if (1 & newdevs_bmp >> i) {
      vif_wlock();
      vifs[i].vif_link_state_serial = 0;
      vif_unlock();
    }
  }
}

static enum status
__vif_enable_eapol (struct vif *vif, bool_t enable)
{
  struct port *port = (struct port *) vif;
  if (enable) {
    struct mac_age_arg_vif aa = {
      .vid  = 0,
      .vifid = vif->id
    };

    port->fdb_insertion_enabled = 0;
    CRP (cpssDxChBrgPortEgrFltUnkEnable (port->ldev, port->lport, GT_TRUE));
    CRP (cpssDxChBrgFdbPortLearnStatusSet
         (port->ldev, port->lport, GT_FALSE, CPSS_LOCK_SOFT_DROP_E));

    mac_flush_vif (&aa, GT_FALSE);
  } else {
    port->fdb_insertion_enabled = 1;
    CRP (cpssDxChBrgPortEgrFltUnkEnable (port->ldev, port->lport, GT_FALSE));
    CRP (cpssDxChBrgFdbPortLearnStatusSet
         (port->ldev, port->lport, GT_FALSE, CPSS_LOCK_FRWRD_E));
  }

  return ST_OK;
}

enum status
vif_enable_eapol (vif_id_t id, bool_t enable)
{
  struct vif *vif = vif_getn (id);

  if (!vif)
    return ST_DOES_NOT_EXIST;

  if (!vif->islocal)
    return ST_REMOTE;
  else
    return __vif_enable_eapol (vif, enable);
}

enum status
vif_eapol_auth (vif_id_t id, vid_t vid, mac_addr_t mac, bool_t auth)
{
  static const mac_addr_t zm = {0, 0, 0, 0, 0, 0};
  struct mac_op_arg_vif op;
  struct vif *vif = vif_getn (id);

  if (!vif)
    return ST_DOES_NOT_EXIST;

  if (!vif->islocal)
    return ST_REMOTE;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  if (!memcmp (mac, zm, sizeof (zm)))
    return __vif_enable_eapol (vif, !auth);

  op.vid = vid;
  op.vifid = id;
  op.drop = 0;
  op.delete = !auth;
  op.type = MET_STATIC;
  memcpy (op.mac, mac, sizeof (op.mac));

  return mac_op_vif (&op);
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

#define VIF_PROC_REMOTE_HEAD(proc, arg...) \
enum status \
vif_##proc##_remote (struct vif *vif, ##arg)

#define VIF_PROC_REMOTE(proc, arg...) \
enum status \
vif_##proc##_remote (struct vif *vif, ##arg) { \
  return ST_REMOTE; \
}

#define VIF_PROC_PORT_HEAD(proc, arg...) \
enum status \
vif_##proc##_port (struct vif *vif, ##arg)

#define VIF_PROC_PORT_BODY(proc, arg...) \
  struct port *port = (struct port*) vif; \
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
  struct vif *vif = vif_getn (nvif); \
  if (!vif) \
    return ST_DOES_NOT_EXIST; \
  return vif->proc (vif, ##arg);

VIF_PROC_REMOTE(mcg_add_vif, mcg_t mcg)

VIF_PROC_PORT_HEAD(mcg_add_vif, mcg_t mcg)
{
  struct port *port = (struct port*) vif;
  return mcg_add_port (mcg, port->id);
}

VIF_PROC_TRUNK_HEAD(mcg_add_vif, mcg_t mcg)
{
  struct trunk *trunk = (struct trunk*) vif;
  int mcgkey = mcg;
  struct mcg_in_trunk *mcgp;

  HASH_FIND_INT(trunk->mcg_head, &mcgkey, mcgp);
  if (mcgp)
    return ST_ALREADY_EXISTS;

  mcgp = calloc (1, sizeof (struct mcg_in_trunk));
  if (!mcgp)
    return ST_HEX;
  mcgp->mcg_key = mcgkey;
  mcgp->designated_vifid = 0;
  HASH_ADD_INT(trunk->mcg_head, mcg_key, mcgp);

  int i;
  for (i = 0; i < trunk->nports; i++) {
    if (trunk->port_enabled[i]) {
      assert(trunk->vif_port[i]);
      vif_mcg_add_vif(trunk->vif_port[i]->id, mcg);
    }
  }
  return ST_OK;
}

VIF_PROC_ROOT_HEAD(mcg_add_vif, mcg_t mcg)
{
VIF_PROC_ROOT_BODY(mcg_add_vif, mcg)
}

VIF_PROC_REMOTE(mcg_del_vif, mcg_t mcg)

VIF_PROC_PORT_HEAD(mcg_del_vif, mcg_t mcg)
{
  struct port *port = (struct port*) vif;
  return mcg_del_port (mcg, port->id);
}

VIF_PROC_TRUNK_HEAD(mcg_del_vif, mcg_t mcg)
{
  struct trunk *trunk = (struct trunk*) vif;
  int mcgkey = mcg;
  struct mcg_in_trunk *mcgp;

  HASH_FIND_INT(trunk->mcg_head, &mcgkey, mcgp);
  if (!mcgp)
    return ST_DOES_NOT_EXIST;

  HASH_DEL(trunk->mcg_head, mcgp);

  int i;
  for (i = 0; i < trunk->nports; i++) {
    if (trunk->port_enabled[i]) {
      assert(trunk->vif_port[i]);
      vif_mcg_del_vif(trunk->vif_port[i]->id, mcg);
    }
  }
  free(mcgp);
  return ST_OK;
}

VIF_PROC_ROOT_HEAD(mcg_del_vif, mcg_t mcg)
{
VIF_PROC_ROOT_BODY(mcg_del_vif, mcg)
}

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


VIF_PROC_REMOTE(shutdown, int shutdown)

VIF_PROC_PORT_HEAD(shutdown, int shutdown)
{
VIF_PROC_PORT_BODY(shutdown, shutdown)
}

VIF_PROC_TRUNK_HEAD(shutdown, int shutdown)
{
VIF_PROC_TRUNK_BODY(shutdown, shutdown)
}

VIF_PROC_ROOT_HEAD(shutdown, int shutdown)
{
VIF_PROC_ROOT_BODY(shutdown, shutdown)
}


VIF_PROC_REMOTE(block, const struct port_block *what)

VIF_PROC_PORT_HEAD(block, const struct port_block *what)
{
VIF_PROC_PORT_BODY(block, what)
}

VIF_PROC_TRUNK_HEAD(block, const struct port_block *what)
{
VIF_PROC_TRUNK_BODY(block, what)
}

VIF_PROC_ROOT_HEAD(block, const struct port_block *what)
{
VIF_PROC_ROOT_BODY(block, what)
}


VIF_PROC_REMOTE(set_access_vid, vid_t vid)

VIF_PROC_PORT_HEAD(set_access_vid, vid_t vid)
{
VIF_PROC_PORT_BODY(set_access_vid, vid)
}

VIF_PROC_TRUNK_HEAD(set_access_vid, vid_t vid)
{
VIF_PROC_TRUNK_BODY(set_access_vid, vid)
}

VIF_PROC_ROOT_HEAD(set_access_vid, vid_t vid)
{
VIF_PROC_ROOT_BODY(set_access_vid, vid)
}


VIF_PROC_REMOTE(set_comm, port_comm_t comm)

VIF_PROC_PORT_HEAD(set_comm, port_comm_t comm)
{
VIF_PROC_PORT_BODY(set_comm, comm)
}

VIF_PROC_TRUNK_HEAD(set_comm, port_comm_t comm)
{
VIF_PROC_TRUNK_BODY(set_comm, comm)
}

VIF_PROC_ROOT_HEAD(set_comm, port_comm_t comm)
{
VIF_PROC_ROOT_BODY(set_comm, comm)
}


VIF_PROC_REMOTE(set_customer_vid, vid_t vid)

VIF_PROC_PORT_HEAD(set_customer_vid, vid_t vid)
{
VIF_PROC_PORT_BODY(set_customer_vid, vid)
}

VIF_PROC_TRUNK_HEAD(set_customer_vid, vid_t vid)
{
VIF_PROC_TRUNK_BODY(set_customer_vid, vid)
}

VIF_PROC_ROOT_HEAD(set_customer_vid, vid_t vid)
{
VIF_PROC_ROOT_BODY(set_customer_vid, vid)
}


VIF_PROC_REMOTE_HEAD(set_mode, enum port_mode mode) {
  vif->mode = mode;
  return ST_REMOTE;
}

VIF_PROC_PORT_HEAD(set_mode, enum port_mode mode)
{
  vif->mode = mode;
VIF_PROC_PORT_BODY(set_mode, mode)
}

VIF_PROC_TRUNK_HEAD(set_mode, enum port_mode mode)
{
VIF_PROC_TRUNK_BODY(set_mode, mode)
}

VIF_PROC_ROOT_HEAD(set_mode, enum port_mode mode)
{
VIF_PROC_ROOT_BODY(set_mode, mode)
}


VIF_PROC_REMOTE(set_pve_dst, port_id_t dpid, int enable)

VIF_PROC_PORT_HEAD(set_pve_dst, port_id_t dpid, int enable)
{
VIF_PROC_PORT_BODY(set_pve_dst, dpid, enable)
}

VIF_PROC_TRUNK_HEAD(set_pve_dst, port_id_t dpid, int enable)
{
VIF_PROC_TRUNK_BODY(set_pve_dst, dpid, enable)
}

VIF_PROC_ROOT_HEAD(set_pve_dst, port_id_t dpid, int enable)
{
VIF_PROC_ROOT_BODY(set_pve_dst, dpid, enable)
}


VIF_PROC_REMOTE(set_protected, bool_t protected)

VIF_PROC_PORT_HEAD(set_protected, bool_t protected)
{
VIF_PROC_PORT_BODY(set_protected, protected)
}

VIF_PROC_TRUNK_HEAD(set_protected, bool_t protected)
{
VIF_PROC_TRUNK_BODY(set_protected, protected)
}

VIF_PROC_ROOT_HEAD(set_protected, bool_t protected)
{
VIF_PROC_ROOT_BODY(set_protected, protected)
}


VIF_PROC_REMOTE_HEAD(set_native_vid, vid_t vid) {
  vif->native_vid = vid;
  return ST_REMOTE;
}

VIF_PROC_PORT_HEAD(set_native_vid, vid_t vid)
{
  vif->native_vid = vid;
VIF_PROC_PORT_BODY(set_native_vid, vid)
}

VIF_PROC_TRUNK_HEAD(set_native_vid,vid_t vid)
{
VIF_PROC_TRUNK_BODY(set_native_vid, vid)
}

VIF_PROC_ROOT_HEAD(set_native_vid, vid_t vid)
{
VIF_PROC_ROOT_BODY(set_native_vid, vid)
}


VIF_PROC_REMOTE(set_trunk_vlans, const uint8_t *bmp)

VIF_PROC_PORT_HEAD(set_trunk_vlans, const uint8_t *bmp)
{
VIF_PROC_PORT_BODY(set_trunk_vlans, bmp)
}

VIF_PROC_TRUNK_HEAD(set_trunk_vlans,const uint8_t *bmp)
{
VIF_PROC_TRUNK_BODY(set_trunk_vlans, bmp)
}

VIF_PROC_ROOT_HEAD(set_trunk_vlans, const uint8_t *bmp)
{
VIF_PROC_ROOT_BODY(set_trunk_vlans, bmp)
}


VIF_PROC_REMOTE(vlan_translate, vid_t from, vid_t to, int add)

VIF_PROC_PORT_HEAD(vlan_translate, vid_t from, vid_t to, int add)
{
VIF_PROC_PORT_BODY(vlan_translate, from, to, add)
}

VIF_PROC_TRUNK_HEAD(vlan_translate, vid_t from, vid_t to, int add)
{
VIF_PROC_TRUNK_BODY(vlan_translate, from, to, add)
}

VIF_PROC_ROOT_HEAD(vlan_translate, vid_t from, vid_t to, int add)
{
VIF_PROC_ROOT_BODY(vlan_translate, from, to, add)
}

enum status
vif_set_stp_state_remote (struct vif *vif,
                          stp_id_t stp_id,
                          int all,
                          enum port_stp_state state) {
  return mac_vif_set_stp_state(vif, stp_id, all, state);
}

VIF_PROC_PORT_HEAD(set_stp_state, stp_id_t stp_id,
  int all, enum port_stp_state state)
{
VIF_PROC_PORT_BODY(set_stp_state, stp_id, all, state)
}

VIF_PROC_TRUNK_HEAD(set_stp_state, stp_id_t stp_id,
  int all, enum port_stp_state state)
{
VIF_PROC_TRUNK_BODY(set_stp_state, stp_id, all, state)
}

VIF_PROC_ROOT_HEAD(set_stp_state, stp_id_t stp_id,
  int all, enum port_stp_state state)
{
VIF_PROC_ROOT_BODY(set_stp_state, stp_id, all, state)
}


VIF_PROC_REMOTE(dgasp_op, int add)

VIF_PROC_PORT_HEAD(dgasp_op, int add)
{
VIF_PROC_PORT_BODY(dgasp_op, add)
}

VIF_PROC_TRUNK_HEAD(dgasp_op,  int add)
{
VIF_PROC_TRUNK_BODY(dgasp_op, add)
}

VIF_PROC_ROOT_HEAD(dgasp_op,  int add)
{
VIF_PROC_ROOT_BODY(dgasp_op, add)
}

VIF_PROC_REMOTE(set_flow_control, flow_control_t fc)

VIF_PROC_PORT_HEAD(set_flow_control, flow_control_t fc)
{
VIF_PROC_PORT_BODY(set_flow_control, fc)
}

VIF_PROC_TRUNK_HEAD(set_flow_control, flow_control_t fc)
{
VIF_PROC_TRUNK_BODY(set_flow_control, fc)
}

VIF_PROC_ROOT_HEAD(set_flow_control, flow_control_t fc)
{
VIF_PROC_ROOT_BODY(set_flow_control, fc)
}


VIF_PROC_REMOTE_HEAD(set_voice_vid, vid_t vid) {
  vif->voice_vid = vid;
  return ST_REMOTE;
}

VIF_PROC_PORT_HEAD(set_voice_vid, vid_t vid)
{
  vif->voice_vid = vid;
VIF_PROC_PORT_BODY(set_voice_vid, vid)
}

VIF_PROC_TRUNK_HEAD(set_voice_vid, vid_t vid)
{
VIF_PROC_TRUNK_BODY(set_voice_vid, vid)
}

VIF_PROC_ROOT_HEAD(set_voice_vid, vid_t vid)
{
VIF_PROC_ROOT_BODY(set_voice_vid, vid)
}
