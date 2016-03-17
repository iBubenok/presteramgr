#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdbHash.h>

#include <mac.h>
#include <stack.h>
#include <zcontext.h>
#include <trunk.h>
#include <port.h>
#include <vlan.h>
#include <dev.h>
#include <mcg.h>
#include <utils.h>
#include <debug.h>
#include <log.h>

#include <sys/prctl.h>

#define FDB_CONTROL_EP "inproc://fdb-control"

enum fdb_ctl_cmd {
  FCC_MAC_OP,
  FCC_OWN_MAC_OP,
  FCC_MC_IP_OP,
  FCC_MAC_OP_FOREIGN_BLCK,
  FCC_FLUSH
};

static void *gctl_sock;
/** FDB sync records array to form sync message from fdb thread */
static struct pti_fdbr fdbr[FDB_MAX_ADDRS];
/** FDB sync records array to form sync message from main control thread */
//static struct pti_fdbr fdbr_ctl[FDB_MAX_ADDRS];

static enum status __attribute__ ((unused))
fdb_ctl (int cmd, const void *arg, int size)
{
  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, arg, size);

  zmsg_send (&msg, gctl_sock);

  msg = zmsg_recv (gctl_sock);
  status_t status = *((status_t *) zframe_data (zmsg_first (msg)));
  zmsg_destroy (&msg);

  return status;
}

static enum status __attribute__ ((unused))
fdb_ctl2 (int cmd, unsigned n, const void *arg, size_t size, void *ctl_sock) {
  zmsg_t *msg = zmsg_new ();

  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, &n, sizeof (n));
  zmsg_addmem (msg, arg, size);

  zmsg_send (&msg, ctl_sock);

  msg = zmsg_recv (ctl_sock);
  status_t status = *((status_t *) zframe_data (zmsg_first (msg)));
  zmsg_destroy (&msg);

  return status;
}

static void
mac_form_fdbr (CPSS_MAC_ENTRY_EXT_STC *me, enum pti_fdb_op operation, struct pti_fdbr* fr) {
  switch (me->dstInterface.type) {
    case CPSS_INTERFACE_PORT_E:
      fr->type = IFTYPE_PORT;
      fr->port.hwdev = me->dstInterface.devPort.devNum;
      fr->port.hwport = me->dstInterface.devPort.portNum;
      break;
    case CPSS_INTERFACE_TRUNK_E:
//DEBUG("TRUNK mE.dst.type==%hhu, mE.dst.dPort.pN==%hhu, mE.dst.trunkId==%hhu\n", // TODO remove
//  me->dstInterface.type, me->dstInterface.devPort.portNum, me->dstInterface.trunkId);
      fr->type = IFTYPE_TRUNK;
      fr->trunk.trunkId = me->dstInterface.trunkId;
      break;
    default:
      break;
  }
  fr->vid = me->key.key.macVlan.vlanId;
  memcpy(fr->mac, me->key.key.macVlan.macAddr.arEther, 6);
  fr->operation = operation;
}

enum status
mac_mc_ip_op (const struct mc_ip_op_arg *arg)
{
  if (!vlan_valid (arg->vid))
    return ST_BAD_VALUE;

  if (!arg->delete && !mcg_exists (arg->mcg))
    return ST_DOES_NOT_EXIST;

  return fdb_ctl (FCC_MC_IP_OP, arg, sizeof (*arg));
}

enum status
mac_op (const struct mac_op_arg *arg)
{
  if (!vlan_valid (arg->vid))
    return ST_BAD_VALUE;

  return fdb_ctl (FCC_MAC_OP, arg, sizeof (*arg));
}

enum status
mac_op_own (vid_t vid, mac_addr_t mac, int add)
{
  struct mac_op_arg arg;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  arg.vid = vid;
  memcpy (arg.mac, mac, sizeof (arg.mac));
  arg.delete = !add;
  /* Everything else is irrelevant for own MAC addr. */

  return fdb_ctl (FCC_OWN_MAC_OP, &arg, sizeof (arg));
}

struct fdb_flush_arg {
  const struct mac_age_arg *aa;
  GT_BOOL ds;
};

enum status
mac_op_foreign_blck(unsigned n, const struct pti_fdbr *arg, void *ctl_sock) {
  return fdb_ctl2(FCC_MAC_OP_FOREIGN_BLCK, n, arg, sizeof(struct pti_fdbr) * n, ctl_sock);
}

enum status
mac_flush (const struct mac_age_arg *arg, GT_BOOL del_static)
{
  struct fdb_flush_arg fa = {
    .aa = arg,
    .ds = del_static
  };

  return fdb_ctl (FCC_FLUSH, &fa, sizeof (fa));
}

enum status
mac_set_aging_time (aging_time_t time)
{
  GT_STATUS rc;
  int d;

  for_each_dev (d) {
    rc = CRP (cpssDxChBrgFdbAgingTimeoutSet (d, time));
    ON_GT_ERROR (rc) break;
  }

  switch (rc) {
  case GT_OK:        return ST_OK;
  case GT_BAD_PARAM: return ST_BAD_VALUE;
  case GT_HW_ERROR:  return ST_HW_ERROR;
  default:           return ST_HEX;
  }
}

/*
 * FDB management.
 */

struct fdb_entry fdb[FDB_MAX_ADDRS];

enum fdb_entry_prio {
  FEP_UNUSED,
  FEP_FOREIGN,
  FEP_DYN,
  FEP_STATIC,
  FEP_OWN
};

CPSS_MAC_UPDATE_MSG_EXT_STC fdb_addrs[FDB_MAX_ADDRS];
GT_U32 fdb_naddrs = 0;

static enum status
fdb_flush (const struct fdb_flush_arg *arg)
{
  CPSS_FDB_ACTION_MODE_ENT s_act_mode;
  CPSS_MAC_ACTION_MODE_ENT s_mac_mode;
  GT_U32 s_act_dev, s_act_dev_mask, act_dev, act_dev_mask;
  GT_U16 s_act_vid, s_act_vid_mask, act_vid, act_vid_mask;
  GT_U32 s_is_trunk, s_is_trunk_mask;
  GT_U32 s_port, s_port_mask, port, port_mask;
  GT_BOOL done[NDEVS];
  int d, all_done, i;

//DEBUG("fdb_flush(.vid==%03hX, .port==%02hX, del_static==%d)\n", arg->vid, arg->port, del_static); //TODO remove

  if (arg->aa->vid == ALL_VLANS) {
    act_vid = 0;
    act_vid_mask = 0;
  } else {
    if (!vlan_valid (arg->aa->vid))
      return ST_BAD_VALUE;
    act_vid = arg->aa->vid;
    act_vid_mask = 0x0FFF;
  }

  if (arg->aa->port == ALL_PORTS) {
    act_dev = 0;
    act_dev_mask = 0;
    port = 0;
    port_mask = 0;
  } else {
    struct port *p = port_ptr (arg->aa->port);

    if (!p)
      return ST_BAD_VALUE;

    act_dev = phys_dev (p->ldev);
    act_dev_mask = 31;
    port = p->lport;
    port_mask = 0x0000007F;
  }

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbAAandTAToCpuSet (d, GT_FALSE));

    CRP (cpssDxChBrgFdbActionModeGet (d, &s_act_mode));
    CRP (cpssDxChBrgFdbMacTriggerModeGet (d, &s_mac_mode));
    CRP (cpssDxChBrgFdbActionActiveDevGet (d, &s_act_dev, &s_act_dev_mask));
    CRP (cpssDxChBrgFdbActionActiveVlanGet (d, &s_act_vid, &s_act_vid_mask));
    CRP (cpssDxChBrgFdbActionActiveInterfaceGet
         (d, &s_is_trunk, &s_is_trunk_mask, &s_port, &s_port_mask));
    CRP (cpssDxChBrgFdbActionsEnableSet (d, GT_FALSE));

    CRP (cpssDxChBrgFdbActionActiveDevSet (d, act_dev, act_dev_mask));
    CRP (cpssDxChBrgFdbActionActiveVlanSet (d, act_vid, act_vid_mask));
    CRP (cpssDxChBrgFdbActionActiveInterfaceSet (d, 0, 0, port, port_mask));
    CRP (cpssDxChBrgFdbStaticDelEnable (d, arg->ds));
    CRP (cpssDxChBrgFdbTrigActionStart (d, CPSS_FDB_ACTION_DELETING_E));

    done[d] = GT_FALSE;
  }

  unsigned fridx = 0;

  if (act_vid_mask && port_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.key.key.macVlan.vlanId == act_vid
          && fdb[i].me.dstInterface.devPort.devNum == act_dev
          && fdb[i].me.dstInterface.devPort.portNum == port
          && (arg->ds || !fdb[i].me.isStatic)) {
        psec_addr_del (&fdb[i].me);
        fdb[i].valid = 0;
        fdb[i].secure = 0;
        if (fdb[i].me.userDefined != FEP_FOREIGN)
          mac_form_fdbr(&fdb[i].me, PTI_FDB_OP_DEL, &fdbr[fridx++]);
        DEBUG("INVALIDATED: " MAC_FMT "\n", MAC_ARG(fdb[i].me.key.key.macVlan.macAddr.arEther));

      }
  } else if (act_vid_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.key.key.macVlan.vlanId == act_vid
          && (arg->ds || !fdb[i].me.isStatic)) {
        psec_addr_del (&fdb[i].me);
        fdb[i].valid = 0;
        fdb[i].secure = 0;
        if (fdb[i].me.userDefined != FEP_FOREIGN)
          mac_form_fdbr(&fdb[i].me, PTI_FDB_OP_DEL, &fdbr[fridx++]);
        DEBUG("INVALIDATED: " MAC_FMT "\n", MAC_ARG(fdb[i].me.key.key.macVlan.macAddr.arEther));

      }
  } else if (port_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.dstInterface.devPort.devNum == act_dev
          && fdb[i].me.dstInterface.devPort.portNum == port
          && (arg->ds || !fdb[i].me.isStatic)) {
        psec_addr_del (&fdb[i].me);
        fdb[i].valid = 0;
        fdb[i].secure = 0;
        if (fdb[i].me.userDefined != FEP_FOREIGN)
          mac_form_fdbr(&fdb[i].me, PTI_FDB_OP_DEL, &fdbr[fridx++]);
        DEBUG("INVALIDATED: " MAC_FMT "\n", MAC_ARG(fdb[i].me.key.key.macVlan.macAddr.arEther));

      }
  } else {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && (arg->ds || !fdb[i].me.isStatic)) {
        psec_addr_del (&fdb[i].me);
        fdb[i].valid = 0;
        fdb[i].secure = 0;
        if (fdb[i].me.userDefined != FEP_FOREIGN)
          mac_form_fdbr(&fdb[i].me, PTI_FDB_OP_DEL, &fdbr[fridx++]);
        DEBUG("INVALIDATED: " MAC_FMT "\n", MAC_ARG(fdb[i].me.key.key.macVlan.macAddr.arEther));

      }
  }

  psec_after_flush ();
  if (fridx)
    tipc_fdb_ctl(fridx, fdbr);


  do {
    all_done = 1;
    for_each_dev (d) {
      if (!done[d]) {
        CRP (cpssDxChBrgFdbTrigActionStatusGet (d, &done[d]));
        all_done &= !!done[d];
      }
    }
    usleep (100);
  } while (!all_done);

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbActionActiveInterfaceSet
         (d, s_is_trunk, s_is_trunk_mask, s_port, s_port_mask));
    CRP (cpssDxChBrgFdbActionActiveDevSet (d, s_act_dev, s_act_dev_mask));
    CRP (cpssDxChBrgFdbActionModeSet (d, s_act_mode));
    CRP (cpssDxChBrgFdbActionActiveVlanSet (d, s_act_vid, s_act_vid_mask));
    CRP (cpssDxChBrgFdbMacTriggerModeSet (d, s_mac_mode));
    CRP (cpssDxChBrgFdbActionsEnableSet (d, GT_TRUE));

    CRP (cpssDxChBrgFdbAAandTAToCpuSet (d, GT_TRUE));
  }

  return ST_OK;
}

static inline int
me_key_eq (const CPSS_MAC_ENTRY_EXT_KEY_STC *a,
           const CPSS_MAC_ENTRY_EXT_KEY_STC *b)
{
  if (a->entryType != b->entryType)
    return 0;

  switch (a->entryType) {
  case CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E:
    return (a->key.macVlan.vlanId == b->key.macVlan.vlanId
            && !memcmp (a->key.macVlan.macAddr.arEther,
                        b->key.macVlan.macAddr.arEther,
                        6));

  case CPSS_MAC_ENTRY_EXT_TYPE_IPV4_MCAST_E:
  case CPSS_MAC_ENTRY_EXT_TYPE_IPV6_MCAST_E:
    return (a->key.ipMcast.vlanId == b->key.ipMcast.vlanId
            && !memcmp (a->key.ipMcast.sip, b->key.ipMcast.sip, 4)
            && !memcmp (a->key.ipMcast.dip, b->key.ipMcast.dip, 4));
  }

  /* Should never happen. */
  return 0;
}

void
mac_count(uint16_t base, uint16_t bits ,uint16_t hash) { //TODO remove func

  uint32_t x,n;
  CPSS_MAC_ENTRY_EXT_KEY_STC k;
  memset(&k, 0, sizeof(k));
  k.key.macVlan.vlanId = 1;
  GT_U32 idx;

  n=1;
  for (x = 0; x < bits-3000 ; x++) {
    n |= 1<<x ;
  }

  DEBUG ("LOOKING HASH: %04x, HITS, BASE==%04x, TILL= %08x \n", hash, base, n);

//  n = 0xFFFFFFFE;
//  if (hash == 0) {
//    n = 0xFFFF;
//  }

  k.key.macVlan.vlanId = 1;
  *(uint16_t *)k.key.macVlan.macAddr.arEther = (uint16_t) htons(base);

  for (x = 0; x < n; x++) {
    *(uint32_t *)(&k.key.macVlan.macAddr.arEther[2]) = htonl(x);
    CRP (cpssDxChBrgFdbHashCalc (CPU_DEV, &k, &idx));
    if (k.key.macVlan.macAddr.arEther[5]==0 && k.key.macVlan.macAddr.arEther[4]==0 && k.key.macVlan.macAddr.arEther[3]==0)  {
      if (hash==0)
      DEBUG ("YES: " MAC_FMT " -> %04x\n", MAC_ARG(k.key.macVlan.macAddr.arEther), idx);

      fprintf(stderr, ".");
    }
//if (hash ==0)
//  DEBUG ("YES: " MAC_FMT " -> %04x\n", MAC_ARG(k.key.macVlan.macAddr.arEther), idx);
    if (idx == hash)
      DEBUG ("HASH: %04x, GOT: " MAC_FMT "\n", hash, MAC_ARG(k.key.macVlan.macAddr.arEther));
  }
  *(uint32_t *)(&k.key.macVlan.macAddr.arEther[2]) = htonl(x);
  CRP (cpssDxChBrgFdbHashCalc (CPU_DEV, &k, &idx));
  if (idx == hash)
    DEBUG ("HASH: %04x, GOT: " MAC_FMT "\n", hash, MAC_ARG(k.key.macVlan.macAddr.arEther));

}


#define INVALID_IDX 0xFFFFFFFF
static enum status
fdb_insert (CPSS_MAC_ENTRY_EXT_STC *e, int own, int secure)
{
  GT_U32 idx, best_idx = INVALID_IDX;
  int i, d, best_pri = e->userDefined;

  ON_GT_ERROR (CRP (cpssDxChBrgFdbHashCalc (CPU_DEV, &e->key, &idx)))
    return ST_HEX;

DEBUG ("INIT: idx==%04x,  best_idx==%04x, best_pri==%d FOUND\n", idx, best_idx, best_pri); //TODO remove
  for (i = 0; i < 4; i++, idx++) {
    if (me_key_eq (&e->key, &fdb[idx].me.key)) {
      if (!fdb[idx].valid
          || fdb[idx].me.userDefined <= e->userDefined
          || (e->userDefined == FEP_FOREIGN && fdb[idx].me.userDefined == FEP_DYN)) {
DEBUG ("idx==%04x,  best_idx==%04x, best_pri==%d FOUND\n", idx, best_idx, best_pri); //TODO remove
        best_idx = idx;
        break;
      } else {
        DEBUG ("won't overwrite FDB entry with higher priority\r\n");
        return ST_ALREADY_EXISTS;
      }
    }

    if (best_pri == FEP_UNUSED) {
DEBUG ("idx==%04x,  best_idx==%04x, best_pri == FEP_UNUSED\n", idx, best_idx); //TODO remove
      continue;
    }

    if (!fdb[idx].valid) {
DEBUG ("idx==%04x, best_idx==%04x, !fdb[idx].valid\n", idx, best_idx); //TODO remove
      best_pri = FEP_UNUSED;
      best_idx = idx;
      continue;
    }

    if (best_pri > fdb[idx].me.userDefined) {
DEBUG ("idx==%04x, best_idx==%04x, best_pri==%u > fdb[idx].me.userDefined==%u\n", idx, best_idx, best_pri, fdb[idx].me.userDefined); //TODO remove
      best_pri = fdb[idx].me.userDefined;
      best_idx = idx;
    }
  }

  if (best_idx == INVALID_IDX) {
DEBUG ("COLLISION: %04x-" MAC_FMT "-%4u\n", idx, MAC_ARG(e->key.key.macVlan.macAddr.arEther), e->key.key.macVlan.vlanId); // TODO remove
    return ST_DOES_NOT_EXIST;
  }

  /* Port Security. */
  if (psec_addr_check (&fdb[best_idx], e) != PAS_OK)
    return ST_BAD_STATE;
  /* END: Port Security. */

  memcpy (&fdb[best_idx].me, e, sizeof (*e));
  fdb[best_idx].valid = 1;
  fdb[best_idx].secure = !!secure;
  for_each_dev (d) {
    if (own)
      e->dstInterface.devPort.devNum = phys_dev (d);
    CRP (cpssDxChBrgFdbMacEntryWrite (d, best_idx, GT_FALSE, e));
  }

  return ST_OK;
}
#undef INVALID_IDX

static enum status
fdb_remove (CPSS_MAC_ENTRY_EXT_KEY_STC *k, int is_foreign)
{
  GT_U32 idx;
  int i, d;

  ON_GT_ERROR (CRP (cpssDxChBrgFdbHashCalc (CPU_DEV, k, &idx)))
    return ST_HEX;

  for (i = 0; i < 4; i++, idx++) {
    if (fdb[idx].valid && me_key_eq (k, &fdb[idx].me.key)) {
      /* DEBUG ("found entry at %u, removing\r\n", idx); */

      if (is_foreign && fdb[idx].me.userDefined > FEP_FOREIGN)
        continue;
      if (!is_foreign && fdb[idx].me.userDefined == FEP_FOREIGN)
        continue;

      psec_addr_del (&fdb[idx].me);

      for_each_dev (d)
        CRP (cpssDxChBrgFdbMacEntryInvalidate (d, idx));
      fdb[idx].valid = 0;

      return ST_OK;
    }
  }

  DEBUG ("Aged entry not found! %04x-" MAC_FMT "\r\n", idx, MAC_ARG(k->key.macVlan.macAddr.arEther));
  return ST_DOES_NOT_EXIST;
}

static enum status
fdb_mac_add (const struct mac_op_arg *arg, int own)
{
  CPSS_MAC_ENTRY_EXT_STC me;

  memset (&me, 0, sizeof (me));
  me.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (me.key.key.macVlan.macAddr.arEther, arg->mac, sizeof (arg->mac));
  me.key.key.macVlan.vlanId = arg->vid;
  me.isStatic = (arg->type != MET_DYNAMIC) || own;

  if (own) {
    me.userDefined = FEP_OWN;
    me.dstInterface.type = CPSS_INTERFACE_PORT_E;
    /* me.dstInterface.devPort.devNum will be set in fdb_insert() */
    me.dstInterface.devPort.portNum = CPSS_CPU_PORT_NUM_CNS;
    me.appSpecificCpuCode = GT_TRUE;
    me.daCommand = CPSS_MAC_TABLE_FRWRD_E;
    me.saCommand = CPSS_MAC_TABLE_FRWRD_E;
    me.daRoute = GT_TRUE;
  } else {
    me.userDefined = FEP_STATIC;

    if (arg->drop) {
      me.dstInterface.type = CPSS_INTERFACE_VID_E;
      me.dstInterface.vlanId = arg->vid;

      me.daCommand = CPSS_MAC_TABLE_DROP_E;
      me.saCommand = CPSS_MAC_TABLE_DROP_E;
    } else {
      struct port *port = port_ptr (arg->port);

      if (!port)
        return ST_BAD_VALUE;

      me.dstInterface.type = CPSS_INTERFACE_PORT_E;
      me.dstInterface.devPort.devNum = phys_dev (port->ldev);
      me.dstInterface.devPort.portNum = port->lport;

      me.daCommand = CPSS_MAC_TABLE_FRWRD_E;
      me.saCommand = CPSS_MAC_TABLE_FRWRD_E;
    }
  }

//DEBUG("fdb_mac_add (const struct mac_op_arg *arg, int own==%d)\n", own); //TODO remove
//PRINTHexDump(&me, sizeof(me));

  return fdb_insert (&me, own, arg->type == MET_SECURE);
}

static enum status
fdb_mac_delete (const struct mac_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_KEY_STC key;

  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (key.key.macVlan.macAddr.arEther, arg->mac, sizeof (arg->mac));
  key.key.macVlan.vlanId = arg->vid;

  return fdb_remove (&key, 0);
}

static enum status
fdb_mac_op (const struct mac_op_arg *arg, int own)
{
  if (arg->delete)
    return fdb_mac_delete (arg);
  else
    return fdb_mac_add (arg, own);
}

static enum status
fdb_mac_mc_ip_delete (const struct mc_ip_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_KEY_STC key;

  memset (&key, 0, sizeof (key));
  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_IPV4_MCAST_E;
  memcpy (key.key.ipMcast.sip, arg->src, sizeof (arg->src));
  memcpy (key.key.ipMcast.dip, arg->dst, sizeof (arg->dst));
  key.key.ipMcast.vlanId = arg->vid;

  return fdb_remove (&key, 0);
}

static enum status
fdb_mac_mc_ip_add (const struct mc_ip_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_STC me;

  memset (&me, 0, sizeof (me));
  me.key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_IPV4_MCAST_E;
  memcpy (me.key.key.ipMcast.sip, arg->src, sizeof (arg->src));
  memcpy (me.key.key.ipMcast.dip, arg->dst, sizeof (arg->dst));
  me.key.key.ipMcast.vlanId = arg->vid;
  me.isStatic           = GT_TRUE;
  me.userDefined        = FEP_STATIC;
  me.dstInterface.type  = CPSS_INTERFACE_VIDX_E;
  me.dstInterface.vidx  = arg->mcg;
  me.daCommand          = CPSS_MAC_TABLE_FRWRD_E;
  me.saCommand          = CPSS_MAC_TABLE_FRWRD_E;

  return fdb_insert (&me, 0, 0);
}

static enum status
fdb_mac_mc_ip_op (const struct mc_ip_op_arg *arg)
{
  if (arg->delete)
    return fdb_mac_mc_ip_delete (arg);
  else
    return fdb_mac_mc_ip_add (arg);
}

static enum status
fdb_mac_foreign_add(const struct pti_fdbr *fr) {
  CPSS_MAC_ENTRY_EXT_STC me;

  assert (fr->vid > 0 && fr->vid < 0xFFF
      && ((fr->type == IFTYPE_PORT && fr->port.hwdev <= 0x1f && fr->port.hwport < NPORTS)
        || (fr->type == IFTYPE_TRUNK && fr->trunk.trunkId <= TRUNK_MAX && fr->trunk.trunkId > 0))
    );
/*  if (fr->vid == 0 || fr->vid >= 0xFFF
      || (fr->type == IFTYPE_PORT && fr->port.hwdev > 0x1f)
      || (fr->type == IFTYPE_TRUNK && ((fr->trunk.trunkId > TRUNK_MAX) || (fr->trunk.trunkId == 0)))) {
    return ST_BAD_VALUE;
  } */
  memset (&me, 0, sizeof (me));
  switch (fr->type) {
    case IFTYPE_PORT:
      me.dstInterface.type = CPSS_INTERFACE_PORT_E;
      me.dstInterface.devPort.portNum = fr->port.hwport;
      me.dstInterface.devPort.devNum = fr->port.hwdev;
      break;
    case IFTYPE_TRUNK:
      me.dstInterface.type = CPSS_INTERFACE_TRUNK_E;
      me.dstInterface.trunkId = fr->trunk.trunkId;
      break;
    default:
      return ST_BAD_VALUE;
  }
  memcpy (me.key.key.macVlan.macAddr.arEther, fr->mac, sizeof (fr->mac));
  me.key.entryType          = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  me.key.key.macVlan.vlanId = fr->vid;
  me.sourceID               = stack_id;
  me.isStatic               = GT_FALSE;
  me.appSpecificCpuCode     = GT_FALSE;
  me.daRoute                = GT_FALSE;
  me.spUnknown              = GT_FALSE;
  me.userDefined            = FEP_FOREIGN;
  me.daCommand              = CPSS_MAC_TABLE_FRWRD_E;
  me.saCommand              = CPSS_MAC_TABLE_FRWRD_E;
  /* me.dstInterface.devPort.devNum will be set in fdb_insert() */

//DEBUG("fdb_mac_foreign_add(const struct pti_fdbr *fr)\n"); //TODO remove
//PRINTHexDump(&me, sizeof(me));

  return fdb_insert (&me, 0, 0);
}

static enum status
fdb_mac_foreign_del(const struct pti_fdbr *fr) {
  CPSS_MAC_ENTRY_EXT_KEY_STC key;

  if (fr->vid == 0 || fr->vid >= 0xFFF || fr->port.hwdev > 0x1f) {
    return ST_BAD_VALUE;
  }
  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (key.key.macVlan.macAddr.arEther, fr->mac, sizeof (fr->mac));
  key.key.macVlan.vlanId = fr->vid;

  return fdb_remove (&key, 1);
}

static enum status
fdb_mac_foreign_blck(unsigned n, const struct pti_fdbr *fa) {
  unsigned i;
  status_t status = ST_OK, s = ST_BAD_REQUEST;
  for (i = 0; i < n; i++) {
    switch (fa[i].operation) {
      case PTI_FDB_OP_ADD:
        s = fdb_mac_foreign_add(&fa[i]);
        break;
      case PTI_FDB_OP_DEL:
        s = fdb_mac_foreign_del(&fa[i]);
    }
    if (s != GT_OK)
      status = s;
  }
  return status;
}

static void
fdb_new_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u)
{
DEBUG("fdb_new_addr(): type==%hhu, %hhu:%hhu:%hhu, " MAC_FMT " \n",  // TODO remove
        u->macEntry.dstInterface.type, u->macEntry.dstInterface.devPort.devNum, u->macEntry.dstInterface.devPort.portNum, u->macEntry.dstInterface.trunkId, MAC_ARG(u->macEntry.key.key.macVlan.macAddr.arEther));

  u->macEntry.appSpecificCpuCode = GT_FALSE;
  u->macEntry.isStatic           = GT_FALSE;
  u->macEntry.daCommand          = CPSS_MAC_TABLE_FRWRD_E;
  u->macEntry.saCommand          = CPSS_MAC_TABLE_FRWRD_E;
  u->macEntry.daRoute            = GT_FALSE;
  u->macEntry.userDefined        = FEP_DYN;
  u->macEntry.spUnknown          = GT_FALSE;

//DEBUG("fdb_new_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u)\n");
//PRINTHexDump(&u->macEntry, sizeof(u->macEntry));

  fdb_insert (&u->macEntry, 0, 0);
}

static void
fdb_old_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u)
{
DEBUG("fdb_old_addr(): type==%hhu, %hhu:%hhu:%hhu, " MAC_FMT  " \n", // TODO remove
  u->macEntry.dstInterface.type, u->macEntry.dstInterface.devPort.devNum, u->macEntry.dstInterface.devPort.portNum, u->macEntry.dstInterface.trunkId, MAC_ARG(u->macEntry.key.key.macVlan.macAddr.arEther));
  /* DEBUG ("AA msg: " MAC_FMT ", VLAN %d\r\n", */
  /*        MAC_ARG (u->macEntry.key.key.macVlan.macAddr.arEther), */
  /*        u->macEntry.key.key.macVlan.vlanId); */

  fdb_remove (&u->macEntry.key, 0);
}

static void
fdb_upd_for_dev (int d)
{
  GT_U32 num, total;
  GT_STATUS rc;
  CPSS_MAC_UPDATE_MSG_EXT_STC *ptr = fdb_addrs;
  int i;

  total = 0;
  do {
    num = FDB_MAX_ADDRS - total;
    rc = cpssDxChBrgFdbAuMsgBlockGet (d, &num, ptr);
    total += num;
    ptr += num;
  } while (rc == GT_OK && total < FDB_MAX_ADDRS);

  switch (rc) {
  case GT_OK:
  case GT_NO_MORE:
    for (i = 0; i < total; i++) {
//DEBUG("fdb_upd_for_dev(): fdb_addrs[i].updType==%u, fp==%p, frp==%p \n", fdb_addrs[i].updType, fdb_addrs, fdbr); // TODO remove
      switch (fdb_addrs[i].updType) {
      case CPSS_NA_E:
      case CPSS_SA_E:
        fdb_new_addr (d, &fdb_addrs[i]);
        mac_form_fdbr(&fdb_addrs[i].macEntry, PTI_FDB_OP_ADD, &fdbr[i]);
        break;
      case CPSS_AA_E:
        fdb_old_addr (d, &fdb_addrs[i]);
        mac_form_fdbr(&fdb_addrs[i].macEntry, PTI_FDB_OP_DEL, &fdbr[i]);
        break;
      default:
        DEBUG("fdb_upd_for_dev(): UNRESOLVED!!!! fdb_addrs[i].updType==%u  \n", fdb_addrs[i].updType);
        break;
      }
    }
    if (total)
      tipc_fdb_ctl(total, fdbr);
    break;

  default:
    CRP (rc);
  }
}

static int
fdb_evt_handler (zloop_t *loop, zmq_pollitem_t *pi, void *not_sock)
{
  zmsg_t *msg = zmsg_recv (not_sock);
  zframe_t *frame = zmsg_first (msg);
  GT_U8 dev = *((GT_U8 *) zframe_data (frame));
  zmsg_destroy (&msg);

  fdb_upd_for_dev (dev);

  return 0;
}

static int
fdb_upd_timer (zloop_t *loop, zmq_pollitem_t *pi, void *not_sock)
{
  int d;

  for_each_dev (d)
    fdb_upd_for_dev (d);

  return 0;
}

static int
fdb_ctl_handler (zloop_t *loop, zmq_pollitem_t *pi, void *ctl_sock)
{
  zmsg_t *msg = zmsg_recv (ctl_sock);
  zframe_t *frame = zmsg_first (msg);
  int cmd = *((int *) zframe_data (frame));
  void *arg = zframe_data (zmsg_next (msg));
  status_t status = ST_BAD_REQUEST;
  unsigned n;

  switch (cmd) {
  case FCC_MAC_OP:
    DEBUG("FCC_MAP_OP\n"); // TODO remove
    status = fdb_mac_op (arg, 0);
    DEBUG("===FCC_MAP_OP\n"); // TODO remove
    break;
  case FCC_OWN_MAC_OP:
    DEBUG("FCC_OWN_MAP_OP\n"); // TODO remove
    status = fdb_mac_op (arg, 1);
    DEBUG("===FCC_OWN_MAP_OP\n"); // TODO remove
    break;
  case FCC_MC_IP_OP:
    DEBUG("FCC_MC_IP_OP\n"); // TODO remove
    status = fdb_mac_mc_ip_op (arg);
    DEBUG("===FCC_MC_IP_OP\n"); // TODO remove
    break;
  case FCC_MAC_OP_FOREIGN_BLCK:
    DEBUG("FCC_MAC_OP_FOREIGN_BLCK\n"); // TODO remove
    n = *((unsigned*) arg);
    arg = zframe_data (zmsg_next (msg));
    status = fdb_mac_foreign_blck(n, arg);
    DEBUG("===FCC_MAC_OP_FOREIGN_BLCK\n"); // TODO remove
    break;
  case FCC_FLUSH:
    status = fdb_flush (arg);
    break;
  default:
    status = ST_BAD_REQUEST;
  }
  zmsg_destroy (&msg);

  msg = zmsg_new ();
  zmsg_addmem (msg, &status, sizeof (status));
  zmsg_send (&msg, ctl_sock);

  return 0;
}

static volatile int fdb_thread_started = 0;

static void *
fdb_thread (void *_)
{
  void *not_sock, *tctl_sock;
  zloop_t *loop;

  DEBUG ("starting up FDB\r\n");

  loop = zloop_new ();
  assert (loop);

  not_sock = zsocket_new (zcontext, ZMQ_PULL);
  assert (not_sock);
  zsocket_bind (not_sock, FDB_NOTIFY_EP);

  zmq_pollitem_t not_pi = { not_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &not_pi, fdb_evt_handler, not_sock);
  zloop_timer (loop, 1000, 0, fdb_upd_timer, not_sock);

  tctl_sock = zsocket_new (zcontext, ZMQ_REP);
  assert (tctl_sock);
  zsocket_bind (tctl_sock, FDB_CONTROL_EP);

  zmq_pollitem_t ctl_pi = { tctl_sock, 0, ZMQ_POLLIN };
  zloop_poller (loop, &ctl_pi, fdb_ctl_handler, tctl_sock);

  prctl(PR_SET_NAME, "fdb", 0, 0, 0);

  DEBUG ("FDB startup done\r\n");
  fdb_thread_started = 1;
  zloop_start (loop);

  return NULL;
}

enum status
mac_start (void)
{
  pthread_t tid;
  struct mac_age_arg arg = {
    .vid = ALL_VLANS,
    .port = ALL_PORTS
  };
  struct fdb_flush_arg fa = {
    .aa = &arg,
    .ds = GT_TRUE
  };
  GT_U32 bmp = 0, n;
  GT_STATUS rc;
  int d;

  for_each_dev (d)
    bmp |= 1 << phys_dev (d);

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbActionsEnableSet (d, GT_FALSE));

    CRP (cpssDxChBrgFdbActionActiveDevSet (d, phys_dev(d), 0x1F));
    CRP (cpssDxChBrgFdbActionActiveVlanSet (d, 0, 0));
    CRP (cpssDxChBrgFdbActionActiveInterfaceSet (d, 0, 0, 0, 0));
    CRP (cpssDxChBrgFdbStaticDelEnable (d, GT_FALSE));
    CRP (cpssDxChBrgFdbActionModeSet (d, CPSS_FDB_ACTION_AGE_WITHOUT_REMOVAL_E));
    CRP (cpssDxChBrgFdbMacTriggerModeSet (d, CPSS_ACT_AUTO_E));
    CRP (cpssDxChBrgFdbActionsEnableSet (d, GT_TRUE));

//    CRP (cpssDxChBrgFdbDeviceTableSet (d, bmp));
    CRP (cpssDxChBrgFdbDeviceTableSet (d, 0x1F));

    /* Disable AA/TA messages. */
    CRP (cpssDxChBrgFdbAAandTAToCpuSet (d, GT_FALSE));
    CRP (cpssDxChBrgFdbSpAaMsgToCpuSet (d, GT_FALSE));

    /* Flush AU message queue. */
    do {
      n = FDB_MAX_ADDRS;
      rc = cpssDxChBrgFdbAuMsgBlockGet (d, &n, fdb_addrs);
    } while (rc == GT_OK);
    if (rc != GT_NO_MORE)
      CRP (rc);
  }


  memset (fdb, 0, sizeof (fdb));
  pthread_create (&tid, NULL, fdb_thread, NULL);
  DEBUG ("waiting for FDB startup\r\n");
  n = 0;
  while (!fdb_thread_started) {
    n++;
    usleep (10000);
  }
  DEBUG ("FDB startup finished after %d iteractions\r\n", n);

  gctl_sock = zsocket_new (zcontext, ZMQ_REQ);
  assert (gctl_sock);
  zsocket_connect (gctl_sock, FDB_CONTROL_EP);

  fdb_flush (&fa);

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbAuMsgRateLimitSet (d, 4000, GT_TRUE));
    CRP (cpssDxChBrgFdbAAandTAToCpuSet (d, GT_TRUE));
    CRP (cpssDxChBrgFdbSpAaMsgToCpuSet (d, GT_TRUE));
  }

  if (stack_active()) {
    struct mac_op_arg mo;
    memset(&mo, 0, sizeof(mo));
    mo.vid = 4095;
    mo.port = stack_pri_port->id;
    mo.type = MET_STATIC;
    memcpy(mo.mac, mac_pri, 6);
    fdb_ctl (FCC_MAC_OP, &mo, sizeof (mo));
    mo.port = stack_sec_port->id;
    memcpy(mo.mac, mac_sec, 6);
    fdb_ctl (FCC_MAC_OP, &mo, sizeof (mo));
  }
  return ST_OK;
}
