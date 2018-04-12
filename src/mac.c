#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdbHash.h>

#include <mac.h>
#include <stack.h>
#include <stackd.h>
#include <control.h>
#include <vif.h>
#include <trunk.h>
#include <port.h>
#include <vlan.h>
#include <dev.h>
#include <presteramgr.h>
#include <mcg.h>
#include <utils.h>
#include <debug.h>
#include <log.h>

#include <linux/tipc.h>
#include <sys/prctl.h>

#define FDB_CONTROL_EP "inproc://fdb-control"

struct fdb_flush_arg {
  struct mac_age_arg aa;
  GT_BOOL ds;
};

struct fdb_flush_arg_vif {
  struct mac_age_arg_vif aav;
  GT_BOOL ds;
};

enum fdb_ctl_cmd {
  FCC_MAC_OP,
  FCC_MAC_OP_VIF,
  FCC_OWN_MAC_OP,
  FCC_OWN_MAC_OP_VIF,
  FCC_MC_IP_OP,
  FCC_MAC_OP_FOREIGN_BLCK,
  FCC_FLUSH,
  FCC_FLUSH_VIF,
  FCC_QUERY,
  FCC_SET_MASTER,
  FCC_FDBMAN_HANDLE_PKT,
  FCC_FDBMAN_SEND_RT,
  FCC_FDBMAN_SEND_NA,
  FCC_FDBMAN_SEND_OPNA,
  FCC_FDBMAN_SEND_UDT,
  FCC_FDBMAN_SEND_VIF_LS
};

#define PTI_FDB_VERSION (1)

enum pti_fdb_op {
  PTI_FDB_OP_ADD = 0,
  PTI_FDB_OP_DEL = 1
};

enum pti_iface_type {
  IFTYPE_PORT = 0,
  IFTYPE_TRUNK,
  IFTYPE_VIDX,
  IFTYPE_VLANID,
  IFTYPE_DEVICE,
  IFTYPE_FABRIC_VIDX,
  IFTYPE_INDEX
};

enum fdbman_cmd {
  FMC_MASTER_READY,
  FMC_UPDATE,
  FMC_MASTER_UPDATE,
  FMC_FLUSH,
  FMC_MASTER_FLUSH,
  FMC_FLUSH_VIF,
  FMC_MASTER_FLUSH_VIF,
  FMC_MACOP,
  FMC_MASTER_MACOP,
  FMC_MASTER_RT,
  FMC_MASTER_NA,
  FMC_OPNA,
  FMC_MASTER_UDT,
  FMC_MASTER_CLEAR_ROUTING,
  FMC_VIF_LS,
  FMC_VIFLS_GET
};

enum fdbman_state {
  FST_MASTER,
  FST_PRE_MEMBER,
  FST_MEMBER
};

struct fdbcomm_thrd {
  void *fdb_ctl_sock;
  void *tipc_ctl_sock;
  zloop_t *loop;
  int fdb_sock;
};

struct pti_fdbr {
  uint8_t operation;
  uint8_t type; ///< record type of enum iface_type
  union {
    struct {
      uint8_t hwdev;
      uint8_t hwport;
    } __attribute__ ((packed)) port;
    struct {
      uint8_t trunkId;
    } __attribute__ ((packed)) trunk;
  } __attribute__ ((packed)) ;
  uint16_t vid;
  uint8_t mac[6];
} __attribute__ ((packed));

struct pti_fdbr_msg {
  uint8_t version;
  uint8_t stack_id;
  uint8_t command;
  uint16_t nfdb;
  serial_t serial;
  devsbmp_t devsbmp;
  struct pti_fdbr data[];
} __attribute__ ((packed));

#define PTI_FDBR_MSG_SIZE(n) \
    (sizeof (struct pti_fdbr_msg) + sizeof (struct pti_fdbr) * (n))

enum fdbman_state fdbman_state;
uint8_t fdbman_master, fdbman_newmaster;
static serial_t fdbman_serial, fdbman_newserial;
static devsbmp_t fdbman_devsbmp;

static void fdbman_send_rtbd(const void *arg, int len, devsbmp_t bmp);
static void fdbman_send_na(const struct arpd_ip_addr_msg *arg);
static void fdbman_send_opna(void *arg);
static void fdbman_send_udt(uint32_t daddr, devsbmp_t bmp);
static void fdbman_send_clear_routing (devsbmp_t newdevs_bmp);
static void fdbman_send_vif_ls(const struct vif_link_state_header *arg);
static enum status fdbman_set_master(const void *arg);
static enum status fdbman_handle_pkt (const void *pkt, uint32_t len);
static void fdbman_send_msg_uni_flush(const struct fdb_flush_arg *arg, int master);
static void fdbman_send_msg_uni_flush_vif(const struct fdb_flush_arg_vif *arg, int master);
static void fdbman_send_msg_uni_macop(const struct mac_op_arg_vif *arg, int own, int master);
static void fdbman_send_msg_uni_update(struct pti_fdbr *pf, uint32_t n, int master);

static void *fdbcomm_ctl_sock;
static void *gctl_sock;           /* sync socket for control thread  */
static void *gctl_asock_control;  /* async socket for control thread */
static void *gctl_asock_evt_phy;  /* async socket for event/phy thread */
static void *gctl_sock_secbr;     /* sync socket for security breach thread */
static void *scmd_sock; /* control sync cmd to be used in fdbman thread only */
static void *acmd_sock; /* control async cmd to be used in fdbman thread only */
static void *evt_sock;  /* vif link notification */
/** FDB sync records array to form sync message from fdb thread */
static struct pti_fdbr fdbr[FDB_MAX_ADDRS];
/** FDB sync records array to form sync message from main control thread */
//static struct pti_fdbr fdbr_ctl[FDB_MAX_ADDRS];

enum status fdbcomm_ctl(unsigned n, const struct pti_fdbr *arg);

static void *pub_sock;   /* notifications to control.c -> manager */

static void fdb_new_addr_notification_send(port_id_t pid,
                                           uint8_t *mac,
                                           GT_U16 vlan)
{
  zmsg_t *msg = zmsg_new ();
  notification_t ntype = CN_NEW_ADDR;

  assert (msg);

  zmsg_addmem (msg, &ntype, sizeof (ntype));
  zmsg_addmem (msg, &pid, sizeof (pid));
  zmsg_addmem (msg, mac, 6);
  zmsg_addmem (msg, &vlan, sizeof (GT_U16));

  zmsg_send (&msg, pub_sock);

  zmsg_destroy (&msg);
}

static void fdb_mac_op_notification_send(mac_op_t type,
                                         port_id_t pid,
                                         uint8_t *mac,
                                         GT_U16 vlan)
{
  zmsg_t *msg = zmsg_new ();
  notification_t ntype = CN_MAC_OP;

  assert (msg);

  zmsg_addmem (msg, &ntype, sizeof (ntype));
  zmsg_addmem (msg, &type, sizeof (type));
  zmsg_addmem (msg, &pid, sizeof (pid));
  zmsg_addmem (msg, mac, 6);
  zmsg_addmem (msg, &vlan, sizeof (GT_U16));

  zmsg_send (&msg, pub_sock);

  zmsg_destroy (&msg);
}

static enum status __attribute__ ((unused))
fdb_ctl_control (int cmd, const void *arg, int size) {
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
fdb_ctl_secbr (int cmd, const void *arg, int size) {
  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, arg, size);
  zmsg_send (&msg, gctl_sock_secbr);

  msg = zmsg_recv (gctl_sock_secbr);
  status_t status = *((status_t *) zframe_data (zmsg_first (msg)));
  zmsg_destroy (&msg);

  return status;
}

static enum status __attribute__ ((unused))
fdb_actl_control (int cmd, const void *arg, int size) {
  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, arg, size);

  zmsg_send (&msg, gctl_asock_control);

  return ST_OK;
}

static enum status
fdb_actl_evt_phy (int cmd, const void *arg, int size) {
  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, arg, size);

  zmsg_send (&msg, gctl_asock_evt_phy);

  return ST_OK;
}

static enum status __attribute__ ((unused))
fdb_ctl2 (int cmd, const void *arg, size_t size, void *ctl_sock) {

  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, arg, size);

  zmsg_send (&msg, ctl_sock);

/*  msg = zmsg_recv (ctl_sock);
  status_t status = *((status_t *) zframe_data (zmsg_first (msg)));
  zmsg_destroy (&msg);

  return status;
  */
  return ST_OK;
}

static enum status __attribute__ ((unused))
fdbman_ctl (const void *arg, size_t size, void *ctl_sock) {

  zmsg_t *msg = zmsg_new ();
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

  return fdb_actl_control (FCC_MC_IP_OP, arg, sizeof (*arg));
}

enum status
mac_op (const struct mac_op_arg *arg)
{
  if (!vlan_valid (arg->vid))
    return ST_BAD_VALUE;

  return fdb_actl_control (FCC_MAC_OP, arg, sizeof (*arg));
}

enum status
mac_op_vif (const struct mac_op_arg_vif *arg)
{
  if (!vlan_valid (arg->vid))
    return ST_BAD_VALUE;

  return fdb_actl_control (FCC_MAC_OP_VIF, arg, sizeof (*arg));
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

  return fdb_actl_control (FCC_OWN_MAC_OP, &arg, sizeof (arg));
}

enum status
mac_op_own_vif (vid_t vid, mac_addr_t mac, int add)
{
  struct mac_op_arg_vif arg;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  arg.vid = vid;
  memcpy (arg.mac, mac, sizeof (arg.mac));
  arg.delete = !add;
  /* Everything else is irrelevant for own MAC addr. */

  return fdb_actl_control (FCC_OWN_MAC_OP, &arg, sizeof (arg));
}

enum status
mac_op_rt (rtbd_notif_t notif, void *msg, int len) {
  static uint8_t buf[sizeof(struct rtbd_route_msg) + sizeof(notif) + 16];
  memcpy (buf, &notif, sizeof (notif));
  memcpy (buf + sizeof(notif), msg, len);
  return fdb_actl_control (FCC_FDBMAN_SEND_RT, buf, sizeof (notif) + len);
}

enum status
mac_op_na (struct arpd_ip_addr_msg *msg) {
  return fdb_actl_control (FCC_FDBMAN_SEND_NA, msg, sizeof (*msg));
}

enum status
mac_op_opna (const struct gw *gw, arpd_command_t cmd) {
  uint8_t buf[sizeof(cmd) + sizeof(*gw)];
  *(arpd_command_t*)buf = cmd;
  memcpy(buf + sizeof(cmd), gw, sizeof(*gw));
  return fdb_actl_control (FCC_FDBMAN_SEND_OPNA, buf, sizeof(cmd) + sizeof(*gw));
}

enum status
mac_op_udt (uint32_t daddr) {
  return fdb_actl_control (FCC_FDBMAN_SEND_UDT, &daddr, sizeof (daddr));
}

enum status
mac_op_send_vif_ls(struct vif_link_state_header *s) {
  return fdb_actl_evt_phy (FCC_FDBMAN_SEND_VIF_LS, s,
//      sizeof (struct vif_link_state_header) + sizeof(struct vif_link_state) * ((struct vif_stgblk_header*)buf)->n);
      sizeof(*s) + s->n * sizeof(struct vif_link_state));
}

enum status
mac_op_handle_pkt(const void * pkt, uint32_t len, void *ctl_sock) {
  return fdb_ctl2(FCC_FDBMAN_HANDLE_PKT, pkt, len, ctl_sock);
}

enum status
mac_flush (const struct mac_age_arg *arg, GT_BOOL del_static)
{
  struct fdb_flush_arg fa;
  memcpy(&fa.aa, arg, sizeof(*arg));
  fa.ds = del_static;

  return fdb_actl_control (FCC_FLUSH, &fa, sizeof (fa));
}

enum status
mac_flush_vif (const struct mac_age_arg_vif *arg, GT_BOOL del_static)
{
  struct fdb_flush_arg_vif fa;
  memcpy(&fa.aav, arg, sizeof(*arg));
  fa.ds = del_static;

  return fdb_actl_control (FCC_FLUSH_VIF, &fa, sizeof (fa));
}

enum status
mac2_query (struct fdb_entry *arg)
{
  return fdb_ctl_secbr (FCC_QUERY, &arg, sizeof (arg));
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

enum status
mac_set_master (uint8_t stid, serial_t serial, devsbmp_t dbmp) {
  uint8_t buf[sizeof(stid) + sizeof(serial) + sizeof(dbmp)];
  buf[0] = stid;
  *((typeof(serial)*)(&buf[sizeof(stid)])) = serial;
  *((typeof(dbmp)*)(&buf[sizeof(stid) + sizeof(serial)])) = dbmp;

  return fdb_actl_control (FCC_SET_MASTER, buf, sizeof (stid) + sizeof(serial) + sizeof(dbmp));
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

static inline void fdb_flush_entry(int i) {
  psec_addr_del (&fdb[i].me);
  fdb[i].valid = 0;
  fdb[i].secure = 0;
  fdb[i].addr_flaps = 0;
  fdb[i].ts_addr_change = 0;
}

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

  if (arg->aa.vid == ALL_VLANS) {
    act_vid = 0;
    act_vid_mask = 0;
  } else {
    if (!vlan_valid (arg->aa.vid))
      return ST_BAD_VALUE;
    act_vid = arg->aa.vid;
    act_vid_mask = 0x0FFF;
  }

  if (arg->aa.port == ALL_PORTS) {
    act_dev = 0;
    act_dev_mask = 0;
    port = 0;
    port_mask = 0;
  } else {
    struct port *p = port_ptr (arg->aa.port);

    if (!p)
      return ST_BAD_VALUE;

    act_dev = phys_dev (p->ldev);
    act_dev_mask = 31;
    port = p->lport;
    port_mask = 0x0000007F;
  }

  port_psec_status_rlock ();
  psec_enable_na_sb_all (0);

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

  if (act_vid_mask && port_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.key.key.macVlan.vlanId == act_vid
          && fdb[i].me.dstInterface.devPort.devNum == act_dev
          && fdb[i].me.dstInterface.devPort.portNum == port
          && (arg->ds || !fdb[i].me.isStatic)) {
        fdb_flush_entry(i);
      }
  } else if (act_vid_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.key.key.macVlan.vlanId == act_vid
          && (arg->ds || !fdb[i].me.isStatic)) {
        fdb_flush_entry(i);
      }
  } else if (port_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.dstInterface.devPort.devNum == act_dev
          && fdb[i].me.dstInterface.devPort.portNum == port
          && (arg->ds || !fdb[i].me.isStatic)) {
        fdb_flush_entry(i);
      }
  } else {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && (arg->ds || !fdb[i].me.isStatic)) {
        fdb_flush_entry(i);
      }
  }

  psec_after_flush ();

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

  psec_enable_na_sb_all (1);
  port_psec_status_unlock ();

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

static enum status
fdb_flush_vif (const struct fdb_flush_arg_vif *arg)
{
  CPSS_FDB_ACTION_MODE_ENT s_act_mode;
  CPSS_MAC_ACTION_MODE_ENT s_mac_mode;
  GT_U32 s_act_dev, s_act_dev_mask, act_dev, act_dev_mask;
  GT_U16 s_act_vid, s_act_vid_mask, act_vid, act_vid_mask;
  GT_U32 s_is_trunk, s_is_trunk_mask, is_trunk, is_trunk_mask;
  GT_U32 s_port, s_port_mask, port, port_mask;
  GT_BOOL done[NDEVS];
  int d, all_done, i;

  if (arg->aav.vid == ALL_VLANS) {
    act_vid = 0;
    act_vid_mask = 0;
  } else {
    if (!vlan_valid (arg->aav.vid))
      return ST_BAD_VALUE;
    act_vid = arg->aav.vid;
    act_vid_mask = 0x0FFF;
  }

  if (arg->aav.vifid == ALL_VIFS) {
    act_dev = 0;
    act_dev_mask = 0;
    port = 0;
    port_mask = 0;
    is_trunk = 0;
    is_trunk_mask = 0;
  } else {
    CPSS_INTERFACE_INFO_STC iface;

    vif_rlock();
    struct vif *v = vif_getn (arg->aav.vifid);
    if (!v) {
      vif_unlock();
      return ST_BAD_VALUE;
    }
    v->fill_cpss_if(v, &iface);
    vif_unlock();

    if (iface.type == CPSS_INTERFACE_PORT_E) {
      act_dev = iface.devPort.devNum;
      act_dev_mask = 31;
      port = iface.devPort.portNum;
      port_mask = 0x0000007F;
      is_trunk = 0;
      is_trunk_mask = 0;
    }
    else {
      act_dev = 0;
      act_dev_mask = 0;
      port = iface.trunkId;
      port_mask = 0x0000007F;
      is_trunk = 1;
      is_trunk_mask = 1;
     }
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
    CRP (cpssDxChBrgFdbActionActiveInterfaceSet (d, is_trunk, is_trunk_mask, port, port_mask));
    CRP (cpssDxChBrgFdbStaticDelEnable (d, arg->ds));
    CRP (cpssDxChBrgFdbTrigActionStart (d, CPSS_FDB_ACTION_DELETING_E));

    done[d] = GT_FALSE;
  }

  if (act_vid_mask && port_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.key.key.macVlan.vlanId == act_vid
          && ((!is_trunk_mask && fdb[i].me.dstInterface.type == CPSS_INTERFACE_PORT_E
               && fdb[i].me.dstInterface.devPort.devNum == act_dev
               && fdb[i].me.dstInterface.devPort.portNum == port)
            || (is_trunk_mask && fdb[i].me.dstInterface.type == CPSS_INTERFACE_TRUNK_E
               && fdb[i].me.dstInterface.trunkId == port))
          && (arg->ds || !fdb[i].me.isStatic)) {
        fdb_flush_entry(i);
      }
  } else if (act_vid_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && fdb[i].me.key.key.macVlan.vlanId == act_vid
          && (arg->ds || !fdb[i].me.isStatic)) {
        fdb_flush_entry(i);
      }
  } else if (port_mask) {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && ((!is_trunk_mask && fdb[i].me.dstInterface.type == CPSS_INTERFACE_PORT_E
               && fdb[i].me.dstInterface.devPort.devNum == act_dev
               && fdb[i].me.dstInterface.devPort.portNum == port)
            || (is_trunk_mask && fdb[i].me.dstInterface.type == CPSS_INTERFACE_TRUNK_E
               && fdb[i].me.dstInterface.trunkId == port))
          && (arg->ds || !fdb[i].me.isStatic)) {
        fdb_flush_entry(i);
      }
  } else {
    for (i = 0; i < FDB_MAX_ADDRS; i++)
      if (fdb[i].valid
          && (arg->ds || !fdb[i].me.isStatic)) {
        fdb_flush_entry(i);
      }
  }

  psec_after_flush ();

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

#define INVALID_IDX 0xFFFFFFFF
#define ANTIFLAPING_INTERVAL (100)
#define FLAP_BLOCKING_TIMEOUT (10000)
#define MAX_FLAPS 5
static enum status
fdb_insert (CPSS_MAC_ENTRY_EXT_STC *e, int own, int secure, int fake) {
// DEBUG(">>>>fdb_insert(): type==%hhu, %hhu:%hhu:%hhu, " MAC_FMT " \n",  // TODO remove
//         e->dstInterface.type, e->dstInterface.devPort.devNum, e->dstInterface.devPort.portNum, e->dstInterface.trunkId, MAC_ARG(e->key.key.macVlan.macAddr.arEther));
  GT_U32 idx, best_idx = INVALID_IDX;
  int i, d, me_exists = 0, best_pri = e->userDefined;
  monotimemsec_t ts = time_monotonic();

  ON_GT_ERROR (CRP (cpssDxChBrgFdbHashCalc (CPU_DEV, &e->key, &idx)))
    return ST_HEX;

//DEBUG ("INIT: idx==%04x,  best_idx==%04x, best_pri==%d FOUND\n", idx, best_idx, best_pri); //TODO remove
  for (i = 0; i < 4; i++, idx++) {
    if (me_key_eq (&e->key, &fdb[idx].me.key)) {
      if (!fdb[idx].valid) {
        best_idx = idx;
        break;
      }
      if (e->dstInterface.type == CPSS_INTERFACE_PORT_E
          && fdb[idx].me.dstInterface.type == CPSS_INTERFACE_PORT_E
          && e->dstInterface.devPort.devNum == fdb[idx].me.dstInterface.devPort.devNum
          && e->dstInterface.devPort.portNum == fdb[idx].me.dstInterface.devPort.portNum)
        me_exists = 1;

      if (fdb[idx].addr_flaps && fdb[idx].ts_addr_change + FLAP_BLOCKING_TIMEOUT < ts)
        fdb[idx].addr_flaps = 0;
      if (fdb[idx].me.userDefined <= e->userDefined) {
//DEBUG ("idx==%04x,  best_idx==%04x, best_pri==%d FOUND\n", idx, best_idx, best_pri); //TODO remove
        if (e->userDefined == FEP_DYN && fdb[idx].ts_addr_change + ANTIFLAPING_INTERVAL >= ts)
          ++fdb[idx].addr_flaps;
        if (e->userDefined <= FEP_DYN && fdb[idx].addr_flaps >= MAX_FLAPS) {
//          fdb[idx].ts_addr_change = ts; // ?????
          DEBUG ("FDB: address flapping detected\r\n");
          return ST_NOT_READY;
        }

        best_idx = idx;
        break;
      } else {
        DEBUG ("won't overwrite FDB entry with higher priority\r\n");
        return ST_ALREADY_EXISTS;
      }
    }

    if (best_pri == FEP_UNUSED) {
//DEBUG ("idx==%04x,  best_idx==%04x, best_pri == FEP_UNUSED\n", idx, best_idx); //TODO remove
      continue;
    }

    if (!fdb[idx].valid) {
//DEBUG ("idx==%04x, best_idx==%04x, !fdb[idx].valid\n", idx, best_idx); //TODO remove
      best_pri = FEP_UNUSED;
      best_idx = idx;
      continue;
    }

    if (best_pri > fdb[idx].me.userDefined) {
//DEBUG ("idx==%04x, best_idx==%04x, best_pri==%u > fdb[idx].me.userDefined==%u\n", idx, best_idx, best_pri, fdb[idx].me.userDefined); //TODO remove
      best_pri = fdb[idx].me.userDefined;
      best_idx = idx;
    }
  } /* for (i = 0; i < 4; i++, idx++) */

  if (best_idx == INVALID_IDX) {
//DEBUG ("COLLISION: %04x-" MAC_FMT "-%4u\n", idx, MAC_ARG(e->key.key.macVlan.macAddr.arEther), e->key.key.macVlan.vlanId); // TODO remove
    return ST_DOES_NOT_EXIST;
  }

  /* Port Security. */
  if (psec_addr_check (&fdb[best_idx], e, fake || me_exists) != PAS_OK)
    return ST_BAD_STATE;
  /* END: Port Security. */

//DEBUG("====fdb_insert(): ST_OK\n");  // TODO remove
  if (fake) /* used in member units to check if macaddr+vid is liable to add to fdb before sending it to master unit */
    return ST_OK;

  memcpy (&fdb[best_idx].me, e, sizeof (*e));
  if (!fdb[best_idx].valid)
    fdb[best_idx].addr_flaps = 0;
  fdb[best_idx].valid = 1;
  fdb[best_idx].secure = !!secure;
  fdb[best_idx].pc_aging_status = fdbman_devsbmp;
  fdb[best_idx].ts_addr_change = ts;
  for_each_dev (d) {
    if (own)
      e->dstInterface.devPort.devNum = phys_dev (d);
    CRP (cpssDxChBrgFdbMacEntryWrite (d, best_idx, GT_FALSE, e));
  }

  return ST_OK;
}
#undef INVALID_IDX

static enum status
fdb_remove (CPSS_MAC_ENTRY_EXT_KEY_STC *k, int is_foreign, uint8_t sid)
{
  GT_U32 idx;
  int i, d;

  ON_GT_ERROR (CRP (cpssDxChBrgFdbHashCalc (CPU_DEV, k, &idx)))
    return ST_HEX;

  for (i = 0; i < 4; i++, idx++) {
    if (fdb[idx].valid && me_key_eq (k, &fdb[idx].me.key)) {
      /* DEBUG ("found entry at %u, removing\r\n", idx); */
/*
      if (is_foreign && fdb[idx].me.userDefined > FEP_FOREIGN)
        continue;
      if (!is_foreign && fdb[idx].me.userDefined == FEP_FOREIGN)
        continue;
*/
      if (fdbman_master == stack_id && fdb[idx].me.dstInterface.type == CPSS_INTERFACE_TRUNK_E && sid != 16) {
//DEBUG("====fdb_remove before " MAC_FMT " PC %d sid: %d \n",
//    MAC_ARG(fdb[idx].me.key.key.macVlan.macAddr.arEther), fdb[idx].me.dstInterface.trunkId, sid);
        if ((fdb[idx].pc_aging_status &= ~(1 << sid))) {
//DEBUG("====fdb_remove NOT AGING " MAC_FMT " PC %d sid: %d \n",
//    MAC_ARG(fdb[idx].me.key.key.macVlan.macAddr.arEther), fdb[idx].me.dstInterface.trunkId, sid);
          return ST_NOT_READY;
        }
//DEBUG("====fdb_remove after " MAC_FMT " PC %d sid: %d \n",
//    MAC_ARG(fdb[idx].me.key.key.macVlan.macAddr.arEther), fdb[idx].me.dstInterface.trunkId, sid);
      }

      psec_addr_del (&fdb[idx].me);

      for_each_dev (d)
        CRP (cpssDxChBrgFdbMacEntryInvalidate (d, idx));
      fdb[idx].valid = 0;
      fdb[idx].secure = 0;
      fdb[idx].ts_addr_change = 0;
      fdb[idx].addr_flaps = 0;

      return ST_OK;
    }
  }

  DEBUG ("Aged entry not found! %04x-" MAC_FMT "\r\n", idx, MAC_ARG(k->key.macVlan.macAddr.arEther));
  return ST_DOES_NOT_EXIST;
}

static enum status
fdb_query (struct fdb_entry **fe) {
  GT_U32 idx;
  int i;

  ON_GT_ERROR (CRP (cpssDxChBrgFdbHashCalc (CPU_DEV, &(*fe)->me.key, &idx)))
    return ST_HEX;

  for (i = 0; i < 4; i++, idx++) {
    if (fdb[idx].valid && me_key_eq (&(*fe)->me.key, &fdb[idx].me.key)) {
      memcpy(*fe, &fdb[idx], sizeof(**fe));
      return ST_OK;
    }
  }
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
    me.userDefined = (me.isStatic)? FEP_STATIC:FEP_DYN;

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

      if (port->fdb_addr_op_notify_enabled)
      fdb_mac_op_notification_send(
        MAC_LEARN,
        port->id,
        me.key.key.macVlan.macAddr.arEther,
        me.key.key.macVlan.vlanId
      );
    }
  }

//DEBUG("fdb_mac_add (const struct mac_op_arg *arg, int own==%d)\n", own); //TODO remove
//PRINTHexDump(&me, sizeof(me));

  return fdb_insert (&me, own, arg->type == MET_SECURE, 0);
}

static enum status
fdb_mac_add_vif (const struct mac_op_arg_vif *arg, int own)
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
    me.userDefined = (me.isStatic)? FEP_STATIC:FEP_DYN;

    if (arg->drop) {
      me.dstInterface.type = CPSS_INTERFACE_VID_E;
      me.dstInterface.vlanId = arg->vid;

      me.daCommand = CPSS_MAC_TABLE_DROP_E;
      me.saCommand = CPSS_MAC_TABLE_DROP_E;
    } else {

      vif_rlock();

      struct vif* vif = vif_getn(arg->vifid);
      if (!vif) {
        vif_unlock();
        return ST_BAD_VALUE;
      }
      vif->fill_cpss_if(vif, &me.dstInterface);

      vif_unlock();

      me.daCommand = CPSS_MAC_TABLE_FRWRD_E;
      me.saCommand = CPSS_MAC_TABLE_FRWRD_E;
    }
  }

//DEBUG("fdb_mac_add (const struct mac_op_arg *arg, int own==%d)\n", own); //TODO remove
//PRINTHexDump(&me, sizeof(me));

  return fdb_insert (&me, own, arg->type == MET_SECURE, 0);
}

static enum status
fdb_mac_delete (const struct mac_op_arg *arg)
{
  CPSS_MAC_ENTRY_EXT_KEY_STC key;

  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (key.key.macVlan.macAddr.arEther, arg->mac, sizeof (arg->mac));
  key.key.macVlan.vlanId = arg->vid;

  return fdb_remove (&key, 0, 16);
}

static enum status
fdb_mac_delete_vif (const struct mac_op_arg_vif *arg)
{
  CPSS_MAC_ENTRY_EXT_KEY_STC key;

  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (key.key.macVlan.macAddr.arEther, arg->mac, sizeof (arg->mac));
  key.key.macVlan.vlanId = arg->vid;

  return fdb_remove (&key, 0, 16);
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
fdb_mac_op_vif (const struct mac_op_arg_vif *arg, int own)
{
  if (arg->delete)
    return fdb_mac_delete_vif (arg);
  else {

    return fdb_mac_add_vif (arg, own);
  }
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

  return fdb_remove (&key, 0, 16);
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

  return fdb_insert (&me, 0, 0, 0);
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
      && ((fr->type == IFTYPE_PORT && fr->port.hwdev <= 0x1f && fr->port.hwport < CPSS_MAX_PORTS_NUM_CNS)
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
//      me.userDefined = (fr->port.hwdev == stack_id) ? FEP_DYN : FEP_FOREIGN;
      me.userDefined = FEP_DYN;
      break;
    case IFTYPE_TRUNK:
      me.dstInterface.type = CPSS_INTERFACE_TRUNK_E;
      me.dstInterface.trunkId = fr->trunk.trunkId;
//      me.userDefined = FEP_FOREIGN; // TODO ???
      me.userDefined = FEP_DYN;
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
  me.daCommand              = CPSS_MAC_TABLE_FRWRD_E;
  me.saCommand              = CPSS_MAC_TABLE_FRWRD_E;

//DEBUG("fdb_mac_foreign_add(const struct pti_fdbr *fr)\n"); //TODO remove
//PRINTHexDump(&me, sizeof(me));
//DEBUG("====fdb_mac_foreign_add() " MAC_FMT  "-%d, %d/%d:%d:%d\n",
//    MAC_ARG(me.key.key.macVlan.macAddr.arEther), me.key.key.macVlan.vlanId,
//    me.dstInterface.type, me.dstInterface.devPort.devNum, me.dstInterface.devPort.portNum,
//    me.dstInterface.trunkId);

  return fdb_insert (&me, 0, 0, 0);
}

static enum status
fdb_mac_foreign_del(const struct pti_fdbr *fr, uint8_t sid) {
  CPSS_MAC_ENTRY_EXT_KEY_STC key;

  if (fr->vid == 0 || fr->vid >= 0xFFF || fr->port.hwdev > 0x1f) {
    return ST_BAD_VALUE;
  }
  key.entryType = CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E;
  memcpy (key.key.macVlan.macAddr.arEther, fr->mac, sizeof (fr->mac));
  key.key.macVlan.vlanId = fr->vid;

//DEBUG("====fdb_mac_foreign_del() " MAC_FMT  "-%d\n",
//    MAC_ARG(key.key.macVlan.macAddr.arEther), key.key.macVlan.vlanId);

  return fdb_remove (&key, 0, sid);
}

static enum status
fdb_mac_foreign_blck(unsigned n, const struct pti_fdbr *fa, uint8_t sid) {
  unsigned i, idx = 0;
  status_t status = ST_OK, s = ST_BAD_REQUEST;
  for (i = 0; i < n; i++) {
    switch (fa[i].operation) {
      case PTI_FDB_OP_ADD:
        s = fdb_mac_foreign_add(&fa[i]);
        if (s == ST_BAD_STATE) {
          memcpy(&fdbr[idx], &fa[i], sizeof(struct pti_fdbr));
          fdbr[idx].operation = PTI_FDB_OP_DEL;
          idx++;
        }
        break;
      case PTI_FDB_OP_DEL:
        s = fdb_mac_foreign_del(&fa[i], sid);
    }
    if (s != GT_OK)
      status = s;
  }
  if (idx) {
    fdbman_send_msg_uni_update(fdbr, idx, 0);
  }
  return status;
}

static enum status
fdb_new_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u, int fake)
{
//DEBUG(">>>>fdb_new_addr(): type==%hhu, sp= %d, %hhu:%hhu:%hhu, " MAC_FMT " \n",  // TODO remove
//         u->macEntry.dstInterface.type, u->macEntry.spUnknown, u->macEntry.dstInterface.devPort.devNum, u->macEntry.dstInterface.devPort.portNum, u->macEntry.dstInterface.trunkId, MAC_ARG(u->macEntry.key.key.macVlan.macAddr.arEther));

  if (u->macEntry.dstInterface.type == CPSS_INTERFACE_PORT_E) {
    vif_rlock();
    struct vif *vif = vif_by_hw (u->macEntry.dstInterface.devPort.devNum,
                      u->macEntry.dstInterface.devPort.portNum);
    assert(vif); /* MUST be local port other way some HEX SHIT happened */
    if (vif->trunk) {
      vif_unlock();
      return ST_BAD_STATE;
    }
    vif_unlock();
  }

//DEBUG("fdb_new_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u)\n");
//PRINTHexDump(&u->macEntry, sizeof(u->macEntry));

  if (u->macEntry.dstInterface.type == CPSS_INTERFACE_PORT_E) {
    port_id_t lport = (port_id_t)port_id(u->macEntry.dstInterface.devPort.devNum,
                                         u->macEntry.dstInterface.devPort.portNum);
    struct port *port = port_ptr(lport);

    if (port) {
      if (port->fdb_new_addr_notify_enabled)
        fdb_new_addr_notification_send(
          port->id,
          u->macEntry.key.key.macVlan.macAddr.arEther,
          u->macEntry.key.key.macVlan.vlanId
        );
    }

    if (!port || !port->fdb_insertion_enabled)
      return ST_BAD_STATE;

    if (port->fdb_addr_op_notify_enabled)
      fdb_mac_op_notification_send(
        MAC_LEARN,
        port->id,
        u->macEntry.key.key.macVlan.macAddr.arEther,
        u->macEntry.key.key.macVlan.vlanId);
  }

  u->macEntry.appSpecificCpuCode = GT_FALSE;
  u->macEntry.isStatic           = GT_FALSE;
  u->macEntry.daCommand          = CPSS_MAC_TABLE_FRWRD_E;
  u->macEntry.saCommand          = CPSS_MAC_TABLE_FRWRD_E;
  u->macEntry.daRoute            = GT_FALSE;
  u->macEntry.userDefined        = FEP_DYN;
  u->macEntry.spUnknown          = GT_FALSE;

  return fdb_insert (&u->macEntry, 0, 0, fake);
}

static enum status
fdb_old_addr (GT_U8 d, CPSS_MAC_UPDATE_MSG_EXT_STC *u)
{
//DEBUG(">>>>fdb_old_addr(): type==%hhu, %hhu:%hhu:%hhu, " MAC_FMT  " \n", // TODO remove
//   u->macEntry.dstInterface.type, u->macEntry.dstInterface.devPort.devNum, u->macEntry.dstInterface.devPort.portNum, u->macEntry.dstInterface.trunkId, MAC_ARG(u->macEntry.key.key.macVlan.macAddr.arEther));
  /* DEBUG ("AA msg: " MAC_FMT ", VLAN %d\r\n", */
  /*        MAC_ARG (u->macEntry.key.key.macVlan.macAddr.arEther), */
  /*        u->macEntry.key.key.macVlan.vlanId); */

  return fdb_remove (&u->macEntry.key, 0, stack_id);
}

static void
fdb_upd_for_dev (int d)
{
  GT_U32 num, total;
  GT_STATUS rc;
  CPSS_MAC_UPDATE_MSG_EXT_STC *ptr = fdb_addrs;
  int i, idx = 0;

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
        if (fdbman_state == FST_MEMBER) {
          if (fdb_new_addr (d, &fdb_addrs[i], 1) == ST_OK)
            mac_form_fdbr(&fdb_addrs[i].macEntry, PTI_FDB_OP_ADD, &fdbr[idx++]);
        }
        else
          if (fdb_new_addr (d, &fdb_addrs[i], 0) == ST_OK)
            mac_form_fdbr(&fdb_addrs[i].macEntry, PTI_FDB_OP_ADD, &fdbr[idx++]);
        break;
      case CPSS_AA_E:
        if (fdbman_state == FST_MEMBER)
          mac_form_fdbr(&fdb_addrs[i].macEntry, PTI_FDB_OP_DEL, &fdbr[idx++]);
        else
          if (fdb_old_addr (d, &fdb_addrs[i]) == ST_OK)
            mac_form_fdbr(&fdb_addrs[i].macEntry, PTI_FDB_OP_DEL, &fdbr[idx++]);
        break;
      default:
        // DEBUG("fdb_upd_for_dev(): UNRESOLVED!!!! fdb_addrs[i].updType==%u  \n", fdb_addrs[i].updType);
        break;
      }
    }
    if (idx)
      fdbman_send_msg_uni_update(fdbr, idx, (fdbman_state == FST_MEMBER)? 0 : 1);
    break;

  default:
    CRP (rc);
  }
}

static int
fdb_evt_handler (zloop_t *loop, zsock_t *reader, void *not_sock)
{
  zmsg_t *msg = zmsg_recv (not_sock);
  zframe_t *frame = zmsg_first (msg);
  GT_U8 dev = *((GT_U8 *) zframe_data (frame));
  zmsg_destroy (&msg);

  fdb_upd_for_dev (dev);

  return 0;
}

static int
fdb_upd_timer (zloop_t *loop, int timer_id, void *not_sock)
{
  int d;

  for_each_dev (d)
    fdb_upd_for_dev (d);

  return 0;
}


static enum status
fdb_ctl_handler (zloop_t *loop, zsock_t *reader, void *ctl_sock)
{
  zmsg_t *msg = zmsg_recv (ctl_sock);
  zframe_t *frame = zmsg_first (msg);
  int cmd = *((int *) zframe_data (frame));
  frame = zmsg_next(msg);
  void *arg = zframe_data (frame);
  status_t status = ST_BAD_REQUEST;

  switch (cmd) {
  case FCC_MAC_OP:
    // DEBUG("FCC_MAP_OP\n"); // TODO remove
    status = fdb_mac_op (arg, 0);
    // DEBUG("===FCC_MAP_OP\n"); // TODO remove
    break;
   case FCC_MAC_OP_VIF:
    // DEBUG("FCC_MAP_OP_VIF\n"); // TODO remove
    if (fdbman_state == FST_MASTER || fdbman_state == FST_PRE_MEMBER) {
      fdbman_send_msg_uni_macop(arg, 0, 1);
      status = fdb_mac_op_vif (arg, 0);
    }
    else {
      status = ST_OK;
      fdbman_send_msg_uni_macop(arg, 0, 0);
    }
    // DEBUG("===FCC_MAP_OP_VIF\n"); // TODO remove
    break;
  case FCC_OWN_MAC_OP:
    // DEBUG("FCC_OWN_MAP_OP\n"); // TODO remove
    status = fdb_mac_op (arg, 1);
    // DEBUG("===FCC_OWN_MAP_OP\n"); // TODO remove
    break;
   case FCC_OWN_MAC_OP_VIF:
    // DEBUG("FCC_OWN_MAP_OP_VIF\n"); // TODO remove
    status = fdb_mac_op_vif (arg, 1);
    // DEBUG("===FCC_OWN_MAP_OP_VIF\n"); // TODO remove
    break;
  case FCC_MC_IP_OP:
    // DEBUG("FCC_MC_IP_OP\n"); // TODO remove
    status = fdb_mac_mc_ip_op (arg);
    // DEBUG("===FCC_MC_IP_OP\n"); // TODO remove
    break;
  case FCC_FLUSH:
    if (fdbman_state == FST_MASTER || fdbman_state == FST_PRE_MEMBER) {
      fdbman_send_msg_uni_flush(arg, 1);
      status = fdb_flush (arg);
    }
    else {
      status = ST_OK;
      fdbman_send_msg_uni_flush(arg, 0);
    }
    break;
  case FCC_FLUSH_VIF:
    if (fdbman_state == FST_MASTER || fdbman_state == FST_PRE_MEMBER) {
      fdbman_send_msg_uni_flush_vif(arg, 1);
      status = fdb_flush_vif (arg);
    }
    else {
      status = ST_OK;
      fdbman_send_msg_uni_flush_vif(arg, 0);
    }
    break;
  case FCC_QUERY:
    if (zframe_size(frame) != sizeof(struct fdb_entry*)) {
      status = ST_BAD_FORMAT;
      break;
    }
    status = fdb_query((struct fdb_entry**)arg);
    break;
  case FCC_SET_MASTER:
    // DEBUG("FCC_SET_MASTER\n"); // TODO remove
    status = fdbman_set_master(arg);
    // DEBUG("===FCC_SET_MASTER\n"); // TODO remove
    break;
  case FCC_FDBMAN_HANDLE_PKT:
    // DEBUG("FCC_FDBMAN_HANDLE_PKT\n"); // TODO remove
//    n = *((uint32_t*) arg);
//    arg = zframe_data (zmsg_next (msg));
    status = fdbman_handle_pkt(arg, zframe_size(frame));
    // DEBUG("===FCC_FDBMAN_HANDLE_PKT\n"); // TODO remove
    break;
  case FCC_FDBMAN_SEND_RT:
    fdbman_send_rtbd(arg, zframe_size (frame), ALL_DEVS);
    break;
  case FCC_FDBMAN_SEND_NA:
    fdbman_send_na((struct arpd_ip_addr_msg *) arg);
    break;
  case FCC_FDBMAN_SEND_OPNA:
    fdbman_send_opna(arg);
    break;
  case FCC_FDBMAN_SEND_UDT:
    fdbman_send_udt(*(uint32_t*)arg, ALL_DEVS);
    break;
  case FCC_FDBMAN_SEND_VIF_LS:
    fdbman_send_vif_ls((struct vif_link_state_header *) arg);
    break;
  default:
    status = ST_BAD_REQUEST;
  }
  zmsg_destroy (&msg);

  return status;
}

static int
fdb_ctl_shandler (zloop_t *loop, zsock_t *reader, void *ctl_sock) {

  enum status status = fdb_ctl_handler (loop, reader, ctl_sock);
  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, &status, sizeof (status));
  zmsg_send (&msg, ctl_sock);
  return 0;
}

static int
fdb_ctl_ahandler (zloop_t *loop, zsock_t *reader, void *ctl_sock) {

  fdb_ctl_handler (loop, reader, ctl_sock);
  return 0;
}

enum status
fdbman_send_pkt(const void *arg, uint32_t len) {

  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, arg, len);
  zmsg_send (&msg, fdbcomm_ctl_sock);

  return ST_OK;
}

static void
fdbman_send_rtbd(const void *arg, int len, devsbmp_t bmp) {
// DEBUG(">>>fdbman_send_rtbd(%d, len == %d)\n", *((rtbd_notif_t*)arg), len);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = FMC_MASTER_RT;
  fdb_msg->nfdb = 0;
  fdb_msg->devsbmp = bmp;
  fdb_msg->serial = fdbman_serial;
  memcpy(fdb_msg->data, arg, len);
  size_t msglen = sizeof(struct pti_fdbr_msg) + len;

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_na(const struct arpd_ip_addr_msg *arg) {
// DEBUG(">>>fdbman_send_na(ip: %x, vid==%d, vif=%x, " MAC_FMT "\n",
//    "mac==%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X)\n",
    // arg->ip_addr ,arg->vid, arg->vif_id, MAC_ARG(arg->mac_addr));
//    arg->mac[0],arg->mac[1],arg->mac[2],arg->mac[3],arg->mac[4],arg->mac[5],arg->mac[6],arg->mac[7]);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = FMC_MASTER_NA;
  fdb_msg->nfdb = 0;
  fdb_msg->devsbmp = ALL_DEVS;
  fdb_msg->serial = fdbman_serial;
  memcpy(fdb_msg->data, arg, sizeof(*arg));
  size_t msglen = sizeof(struct pti_fdbr_msg) + sizeof(*arg);

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_opna(void *arg) {
// DEBUG(">>>fdbman_send_opna()\n");
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = FMC_OPNA;
  fdb_msg->nfdb = 0;
  fdb_msg->devsbmp = ALL_DEVS;
  fdb_msg->serial = fdbman_serial;
  memcpy(fdb_msg->data, arg, sizeof(arpd_command_t) + sizeof(struct gw));
  size_t msglen = sizeof(struct pti_fdbr_msg) + sizeof(arpd_command_t) + sizeof(struct gw);

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_udt(uint32_t daddr, devsbmp_t bmp) {
// DEBUG(">>>fdbman_send_udt(%x)\n", daddr);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = FMC_MASTER_UDT;
  fdb_msg->nfdb = 0;
  fdb_msg->devsbmp = bmp;
  fdb_msg->serial = fdbman_serial;
  memcpy(fdb_msg->data, &daddr, sizeof(daddr));
  size_t msglen = sizeof(struct pti_fdbr_msg) + sizeof(daddr);

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_clear_routing (devsbmp_t newdevs_bmp) {
// DEBUG(">>>fdbman_send_clear_routing(%x)\n", newdevs_bmp);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = FMC_MASTER_CLEAR_ROUTING;
  fdb_msg->nfdb = 0;
  fdb_msg->devsbmp = newdevs_bmp;
  fdb_msg->serial = fdbman_serial;
  size_t msglen = sizeof(struct pti_fdbr_msg);

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_vifls_get (devsbmp_t bmp) {
DEBUG(">>>fdbman_send_vifls_get(%x)\n", bmp);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = FMC_VIFLS_GET;
  fdb_msg->nfdb = 0;
  fdb_msg->devsbmp = bmp;
  fdb_msg->serial = fdbman_serial;
  size_t msglen = sizeof(struct pti_fdbr_msg);

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_vif_ls(const struct vif_link_state_header *arg) {
DEBUG(">>>fdbman_send_vif_ls(vif=%x)\n", arg->data[0].vifid);

  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = FMC_VIF_LS;
  fdb_msg->nfdb = 0;
  fdb_msg->devsbmp = ALL_DEVS;
  fdb_msg->serial = fdbman_serial;
  memcpy(fdb_msg->data, arg, sizeof(*arg) + arg->n * sizeof(struct vif_link_state));
  size_t msglen = sizeof(struct pti_fdbr_msg) + sizeof(*arg) + arg->n * sizeof(struct vif_link_state);

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_vif_allls(void) {
DEBUG(">>>fdbman_send_vif_allls(void)\n");
#define MAX_VIF_LS_SIZE sizeof(struct vif_link_state_header) + NPORTS * sizeof(struct vif_link_state)
  uint8_t buf[MAX_VIF_LS_SIZE];
  struct vif_link_state_header *vif_lsh = vif_form_ls_sync_pkt(buf , MAX_VIF_LS_SIZE);
  assert(vif_lsh);
  fdbman_send_vif_ls(vif_lsh);
}

static void
fdbman_send_master_announce_pkt(serial_t serial, devsbmp_t bmp) {
// DEBUG(">>>fdbman_send_master_announce_pkt(%llu, %hx)\n", serial, bmp);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

    fdb_msg->version = PTI_FDB_VERSION;
    fdb_msg->stack_id = stack_id;
    fdb_msg->command = FMC_MASTER_READY;
    fdb_msg->nfdb = 0;
    fdb_msg->serial = serial;
    fdb_msg->devsbmp = bmp;

    fdbman_send_pkt(buf, sizeof(struct pti_fdbr_msg));
}

static void
fdbman_send_msg_uni_macop(const struct mac_op_arg_vif *arg, int own, int master) {
// DEBUG(">>>fdbman_send_msg_uni_macop(vid==%d, vif=%x, type==%d, drop=%d, delete=%d, bmp_devs=%x, "
//     "mac==%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X)\n",
//     arg->vid, arg->vifid, arg->type, arg->drop, arg->delete, arg->bmp_devs,
//     arg->mac[0],arg->mac[1],arg->mac[2],arg->mac[3],arg->mac[4],arg->mac[5],arg->mac[6],arg->mac[7]);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = (master) ? FMC_MASTER_MACOP : FMC_MACOP;
  fdb_msg->nfdb = 0;
  fdb_msg->serial = fdbman_serial;
  memcpy(fdb_msg->data, arg, sizeof(*arg));
  size_t msglen = sizeof(struct pti_fdbr_msg) + sizeof(*arg);

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_msg_uni_flush(const struct fdb_flush_arg *arg, int master) {
// DEBUG(">>>fdbman_send_msg_uni_flush(arg.aa.vid==%d, arg.aa.port=%d, arg.ds==%d)\n", arg->aa.vid, arg->aa.port, arg->ds);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = (master) ? FMC_MASTER_FLUSH : FMC_FLUSH;
  fdb_msg->nfdb = 0;
  fdb_msg->serial = fdbman_serial;
  memcpy(fdb_msg->data, arg, sizeof(*arg));
  size_t msglen = sizeof(struct pti_fdbr_msg) + sizeof(*arg);

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_msg_uni_flush_vif(const struct fdb_flush_arg_vif *arg, int master) {
// DEBUG(">>>fdbman_send_msg_uni_flush_vif(arg.aav.vid==%d, arg.aav.vifid=%x, arg.ds==%d)\n", arg->aav.vid, arg->aav.vifid, arg->ds);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  fdb_msg->version = PTI_FDB_VERSION;
  fdb_msg->stack_id = stack_id;
  fdb_msg->command = (master) ? FMC_MASTER_FLUSH_VIF : FMC_FLUSH_VIF;
  fdb_msg->nfdb = 0;
  fdb_msg->serial = fdbman_serial;
  memcpy(fdb_msg->data, arg, sizeof(*arg));
  size_t msglen = sizeof(struct pti_fdbr_msg) + sizeof(*arg);

  fdbman_send_pkt(buf, msglen);
}

static void
fdbman_send_msg_uni_update(struct pti_fdbr *pf, uint32_t n, int master) {
// DEBUG(">>>fdbman_send_msg_uni_update(%p, %d, %d)\n", pf, n, master);
  static uint8_t buf[TIPC_MSG_MAX_LEN];
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

#define TIPC_FDB_NREC ( (TIPC_MSG_MAX_LEN - sizeof(struct pti_fdbr_msg)) / sizeof(struct pti_fdbr) - 1 )

  int nf = 0;
  do { /* splitting up to TIPC message maximum size */
    fdb_msg->version = PTI_FDB_VERSION;
    fdb_msg->stack_id = stack_id;
    fdb_msg->command = (master)? FMC_MASTER_UPDATE : FMC_UPDATE ;
    uint16_t nr = (n - nf > TIPC_FDB_NREC)? TIPC_FDB_NREC : n - nf;
    fdb_msg->nfdb = htons(nr);
    fdb_msg->serial = fdbman_serial;
    memcpy(fdb_msg->data, pf+nf, sizeof(struct pti_fdbr) * nr);

    unsigned i;
    for (i = 0; i < nr; i++)
      fdb_msg->data[i].vid = htons(fdb_msg->data[i].vid);

    size_t msglen = sizeof (struct pti_fdbr_msg) + sizeof (struct pti_fdbr) * nr;

    fdbman_send_pkt(buf, msglen);

//DEBUG("tipc-fdb-msg sent, msglen==%d\n", msglen); //TODO remove
//DEBUG("tipc-fdb-msg nf==%d, fdb_msg->nfdb==%hu\n", nf, nr);
//PRINTHexDump(buf, msglen);

    nf += TIPC_FDB_NREC;
  } while (nf < n );
//DEBUG("tipc-fdb-msg OUT nf==%d, n==%u, TIPC_FDB_NREC==%u\n", nf, n, TIPC_FDB_NREC); // TODO remove
}

static void
fdbman_handle_msg_master_update(const struct pti_fdbr_msg *msg, uint32_t len) {

  fdb_mac_foreign_blck(msg->nfdb, msg->data, msg->stack_id);
}

static void
fdbman_handle_msg_update(struct pti_fdbr_msg *msg, uint32_t len) {
// DEBUG(">>>fdbman_handle_msg_update(%p, %d)\n", msg, len);
  unsigned i, idx = 0;
  struct pti_fdbr *fa = msg->data;
  status_t s = ST_DOES_NOT_EXIST;

  for (i = 0; i < msg->nfdb; i++) {
    switch (fa[i].operation) {
      case PTI_FDB_OP_ADD:
        s = fdb_mac_foreign_add(&fa[i]);
        break;
      case PTI_FDB_OP_DEL:
        s = fdb_mac_foreign_del(&fa[i], msg->stack_id);
    }
    if (s == ST_OK) {
      memcpy(&fdbr[idx], &fa[i], sizeof(struct pti_fdbr));
      idx++;
    }
  }
  if (idx) {
    fdbman_send_msg_uni_update(fdbr, idx, 1);
  }
}

static void
fdbman_handle_msg_macop(struct pti_fdbr_msg *msg, uint32_t len) {
// DEBUG(">>>fdbman_handle_msg_macop(%p, %d)\n", msg, len);
  struct mac_op_arg_vif *arg = (struct mac_op_arg_vif *) msg->data;
  fdbman_send_msg_uni_macop(arg, 0, 1);
  fdb_mac_op_vif (arg, 0);
}

static void
fdbman_handle_msg_master_macop(struct pti_fdbr_msg *msg, uint32_t len) {
// DEBUG(">>>fdbman_handle_msg_master_macop(%p, %d)\n", msg, len);
  struct mac_op_arg_vif *arg = (struct mac_op_arg_vif *) msg->data;
  fdb_mac_op_vif (arg, 0);
}

static void
fdbman_handle_msg_flush(struct pti_fdbr_msg *msg, uint32_t len) {
// DEBUG(">>>fdbman_handle_msg_flush(%p, %d)\n", msg, len);
  struct fdb_flush_arg *arg = (struct fdb_flush_arg *) msg->data;
  fdbman_send_msg_uni_flush(arg, 1);
  fdb_flush (arg);
}

static void
fdbman_handle_msg_master_flush(struct pti_fdbr_msg *msg, uint32_t len) {
// DEBUG(">>>fdbman_handle_msg_master_flush(%p, %d)\n", msg, len);
  struct fdb_flush_arg *arg = (struct fdb_flush_arg *) msg->data;
  fdb_flush (arg);
}

static void
fdbman_handle_msg_flush_vif(struct pti_fdbr_msg *msg, uint32_t len) {
// DEBUG(">>>fdbman_handle_msg_flush_vif(%p, %d)\n", msg, len);
  struct fdb_flush_arg_vif *arg = (struct fdb_flush_arg_vif *) msg->data;
  fdbman_send_msg_uni_flush_vif(arg, 1);
  fdb_flush_vif (arg);
}

static void
fdbman_handle_msg_master_flush_vif(struct pti_fdbr_msg *msg, uint32_t len) {
// DEBUG(">>>fdbman_handle_msg_master_flush_vif(%p, %d)\n", msg, len);
  struct fdb_flush_arg_vif *arg = (struct fdb_flush_arg_vif *) msg->data;
  fdb_flush_vif (arg);
}

static void
fdbman_send_control_cmd (command_t cmd, void *buf, size_t len) {
  zmsg_t *msg = zmsg_new ();
  assert (msg);

  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, buf, len);
  zmsg_send (&msg, acmd_sock);
}

__attribute__ ((unused))
static void *
fdbman_execute_control_cmd (command_t cmd, void *buf, size_t len) {
// DEBUG(">>>>fdbman_execute_control_cmd(%d, , %d) == \n", cmd, len);
  zmsg_t *msg = zmsg_new ();
  assert (msg);

  zmsg_addmem (msg, &cmd, sizeof (cmd));
  zmsg_addmem (msg, buf, len);
  zmsg_send (&msg, scmd_sock);

  msg = zmsg_recv(scmd_sock);
  if (!msg)
    return NULL;
  zframe_t *frame = zmsg_pop(msg);
  if (zframe_size(frame) != sizeof(status_t) || *(status_t*)zframe_data(frame) != ST_OK) {
    // DEBUG("====fdbman_execute_control_cmd OOPS1\n");
    return NULL;
  }
  zframe_destroy(&frame);
  frame = zmsg_next(msg);
  if (zframe_size(frame) != sizeof(void*)) {
    // DEBUG("====fdbman_execute_control_cmd OOPS2\n");
    return NULL;
  }
  void *r = *(void**)zframe_data(frame);
  zmsg_destroy(&msg);
// DEBUG("<<<<fdbman_execute_control_cmd == %p\n", r);
  return r;
}

static void
fdbman_handle_msg_rt(struct pti_fdbr_msg *msg, uint32_t len) {
  if (! (msg->devsbmp & (1 << stack_id)))
    return;
  void *arg = msg->data;
  size_t msglen = len - sizeof(struct pti_fdbr_msg);
// DEBUG(">>>fdbman_handle_msg_rt(%d, len == %d)\n", *((rtbd_notif_t*)arg), len);
  fdbman_send_control_cmd(SC_INT_RTBD_CMD, arg, msglen);
}

static void
fdbman_handle_msg_na(struct pti_fdbr_msg *msg, uint32_t len) {
  if (! (msg->devsbmp & (1 << stack_id)))
    return;
  struct arpd_ip_addr_msg *arg = (struct arpd_ip_addr_msg *) msg->data;
// DEBUG(">>>fdbman_handle_msg_na(ip: %x, vid==%d, vif=%x, " MAC_FMT "\n",
// arg->ip_addr ,arg->vid, arg->vif_id, MAC_ARG(arg->mac_addr));
  fdbman_send_control_cmd(SC_INT_NA_CMD, arg, sizeof(*arg));
}

static void
fdbman_handle_msg_opna(struct pti_fdbr_msg *msg, uint32_t len) {
  void *arg = msg->data;
  size_t msglen = len - sizeof(struct pti_fdbr_msg);
// DEBUG(">>>fdbman_handle_msg_opna(%d, len == %d)\n", *((arpd_command_t*)arg), len);
  fdbman_send_control_cmd(SC_INT_OPNA_CMD, arg, msglen);
}

static void
fdbman_handle_msg_udt(struct pti_fdbr_msg *msg, uint32_t len) {
  uint32_t daddr;
  memcpy(&daddr, msg->data, sizeof(daddr));
// DEBUG(">>>fdbman_handle_msg_udt(%x)\n", daddr);
  fdbman_send_control_cmd(SC_INT_UDT_CMD, &daddr, sizeof(daddr));
}

static void
fdbman_handle_msg_clear_routing(struct pti_fdbr_msg *msg, uint32_t len) {
  int dummy;
// DEBUG(">>>fdbman_handle_msg_clear_routing() %x\n", msg->devsbmp);
  if (! (msg->devsbmp & (1 << stack_id)))
    return;
  fdbman_send_control_cmd(SC_INT_CLEAR_RT_CMD, &dummy, sizeof(dummy));
}

static void
fdbman_handle_msg_vif_ls(struct pti_fdbr_msg *msg, uint32_t len) {
  struct vif_link_state_header *arg = (struct vif_link_state_header*) msg->data;
DEBUG(">>>fdbman_handle_msg_vif_ls() %x\n", msg->devsbmp);
  if (! (msg->devsbmp & (1 << stack_id)))
    return;

  vif_process_ls_pkt(arg, evt_sock);
}

static void
fdbman_handle_msg_vifls_get(struct pti_fdbr_msg *msg, uint32_t len) {
DEBUG(">>>fdbman_handle_msg_vifls_get() %x\n", msg->devsbmp);
  if (! (msg->devsbmp & (1 << stack_id)))
    return;

  fdbman_send_vif_allls();
}

static void
fdbman_sync_routing(devsbmp_t nbmp) {
DEBUG (">>>>fdbman_sync_routing(%hx) %hx\n", nbmp, nbmp);
  if (!nbmp) return;

  fdbman_send_clear_routing (nbmp);

  uint32_t dummy;
  void *p = fdbman_execute_control_cmd(CC_INT_GET_RT_CMD, &dummy, sizeof(dummy));
  if (!p) return;
  int nr = *(uint32_t*)p;
  assert(sizeof(rtbd_notif_t) <= sizeof(uint32_t));
  struct rtbd_route_msg *rt = (struct rtbd_route_msg *)((uint32_t*)p + 1);
  int i;
  for (i = 0; i < nr; i++) {
    *((rtbd_notif_t*)&rt[i] - 1) = RCN_ROUTE;
    size_t msglen = sizeof(struct rtbd_route_msg) + sizeof(rtbd_notif_t);
    fdbman_send_rtbd((rtbd_notif_t*)&rt[i] - 1, msglen, nbmp);
  }
  free(p);

  p = fdbman_execute_control_cmd(CC_INT_GET_UDADDRS_CMD, &dummy, sizeof(dummy));
  if (!p) return;
  nr = *(uint32_t*)p;
  uint32_t *udaddr = (uint32_t*)p + 1;
  for (i = 0; i < nr; i++) {
    fdbman_send_udt(udaddr[i], nbmp);
  }
  free(p);

DEBUG("<<<<fdbman_sync_routing");
}

static void
fdbman_sync_ls(devsbmp_t nbmp) {
DEBUG (">>>>fdbman_sync_ls(%hx) %hx\n", nbmp, nbmp);
  if (!nbmp)
    return;
  fdbman_send_vifls_get(nbmp);
}

static enum status
fdbman_set_master(const void *arg) {
  int dummy;
  uint8_t newmaster = *((uint8_t*) arg);
  serial_t serial = *(serial_t*)((uint8_t*) arg) + 1;
  devsbmp_t dbmp = *(devsbmp_t*)((uint8_t*) arg + 1 + sizeof(serial_t));
  devsbmp_t newdevs_bmp = ~(fdbman_devsbmp | ~dbmp);
  devsbmp_t deldevs_bmp = ~(~fdbman_devsbmp | dbmp);

  vif_ls_clear_serial(newdevs_bmp);

// DEBUG(">>>fdbman_set_master(%hhu, %llu, %hx)\n", newmaster, serial, dbmp);
// DEBUG("FDBMAN state: %d master: %d, newmaster: %d, fdbman_serial: %llu, fdbman_newserial: %llu\n", fdbman_state, fdbman_master, fdbman_newmaster, fdbman_serial, fdbman_newserial);
  if (serial < fdbman_serial) {
    // DEBUG("====fdbman_set_master() serial %llu < than the current fdbman_serial %llu", serial, fdbman_serial);
    return ST_OK;
  }
  if (serial <= fdbman_newserial)
    return ST_OK;

//  if (fdbman_devsbmp != dbmp)
//    fdbman_send_vif_allls();

  switch (fdbman_state) {
    case FST_MASTER:
      if (stack_id != newmaster) {
        fdbman_newmaster = newmaster;
        fdbman_newserial = serial;
        fdbman_state = FST_PRE_MEMBER;
      }
      else {
        fdbman_serial = fdbman_newserial = serial;
        fdbman_master = fdbman_newmaster = newmaster;
        fdbman_send_master_announce_pkt(serial, dbmp);
        fdbman_sync_ls(newdevs_bmp);
        fdbman_sync_routing(newdevs_bmp);
      }
      break;

    case FST_PRE_MEMBER:
      fdbman_newmaster = newmaster;
      fdbman_newserial = serial;
      if (stack_id == newmaster) {
        fdbman_master = newmaster;
        fdbman_serial = serial;
        fdbman_send_master_announce_pkt(serial, dbmp);
        fdbman_sync_ls(dbmp);
        fdbman_send_control_cmd(SC_INT_CLEAR_RT_CMD, &dummy, sizeof(dummy));
        fdbman_send_clear_routing(dbmp);
        fdbman_state = FST_MASTER;
      }
      break;
    case FST_MEMBER:
      fdbman_newmaster = newmaster;
      fdbman_newserial = serial;
      if (stack_id == newmaster) {
        fdbman_master = newmaster;
        fdbman_serial = serial;
        fdbman_send_master_announce_pkt(serial, dbmp);
        fdbman_sync_ls(dbmp);
        fdbman_send_control_cmd(SC_INT_CLEAR_RT_CMD, &dummy, sizeof(dummy));
        fdbman_send_clear_routing(dbmp);
        fdbman_state = FST_MASTER;
      }
      break;
  }
  fdbman_send_control_cmd(SC_INT_CLEAR_RE_CMD, &deldevs_bmp, sizeof(deldevs_bmp));
  fdbman_devsbmp = dbmp;
// DEBUG("2FDBMAN state: %d master: %d, newmaster: %d, fdbman_serial: %llu, fdbman_newserial: %llu\n", fdbman_state, fdbman_master, fdbman_newmaster, fdbman_serial, fdbman_newserial);
  return ST_OK;
}

static enum status
fdbman_handle_pkt (const void *pkt, uint32_t len) {

  struct pti_fdbr_msg *msg = (struct pti_fdbr_msg*) pkt;
//DEBUG(">>>fdbman_handle_pkt (%p, %d): %d\n", pkt, len, msg->command);
// DEBUG("msg: cmd %d, n %d, stid %d, serial: %llu, devsbmp: %hx\n",
    // msg->command, ntohs(msg->nfdb), msg->stack_id, msg->serial, msg->devsbmp);
// DEBUG("FDBMAN state: %d master: %d, newmaster: %d, fdbman_serial: %llu, fdbman_newserial: %llu\n",
    // fdbman_state, fdbman_master, fdbman_newmaster, fdbman_serial, fdbman_newserial);

  if (msg->command != FMC_MASTER_READY && msg->serial < fdbman_serial) {
// DEBUG("<<<<fdbman_handle_pkt() DROP;\n");
    return ST_OK;
  }

  if (msg->command == FMC_MASTER_UPDATE || msg->command == FMC_UPDATE) {
    msg->nfdb = ntohs(msg->nfdb);
    unsigned i;
    for (i = 0; i < msg->nfdb; i++)
      msg->data[i].vid = ntohs(msg->data[i].vid);
  }

  switch (msg->command) {
    case FMC_MASTER_READY:
      if (!(msg->devsbmp & (1 << stack_id))) {
// DEBUG("<<<<fdbman_handle_pkt(): DROP\n");
        return ST_OK;
      }
      if (msg->serial < fdbman_serial) {
        // DEBUG("ERROR: fdbman: with serial: %llu recieved outdated FMS_MASTER_READY with serial %llu ",
        //     fdbman_serial, msg->serial);
        break;
      }
      if (msg->serial < fdbman_newserial) {
        fdbman_master = msg->stack_id;
        fdbman_serial = msg->serial;
        break;
      }
      switch (fdbman_state) {
        case FST_MASTER:
          fdbman_newmaster = fdbman_master = msg->stack_id;
          fdbman_newserial = fdbman_serial = msg->serial;
          fdbman_devsbmp = msg->devsbmp;
          fdbman_state = FST_MEMBER;
          break;
        case FST_PRE_MEMBER:
          if (msg->stack_id == fdbman_newmaster) {
            fdbman_newmaster = fdbman_master = msg->stack_id;
            fdbman_newserial = fdbman_serial = msg->serial;
            fdbman_devsbmp = msg->devsbmp;
            fdbman_state = FST_MEMBER;
          }
          else {
            fdbman_newmaster = fdbman_master = msg->stack_id;
            fdbman_newserial = fdbman_serial = msg->serial;
            fdbman_devsbmp = msg->devsbmp;
            fdbman_state = FST_MEMBER;
            // DEBUG("ERROR: fdbman: FMS_MASTER recieved FMC_MASTER_READY from unit %hhu actually waiting it from unit %hhu\n",
            //     msg->stack_id, fdbman_newmaster);
          }
          break;
        case FST_MEMBER:
          if (msg->stack_id == fdbman_newmaster) {
            fdbman_newmaster = fdbman_master = msg->stack_id;
            fdbman_newserial = fdbman_serial = msg->serial;
            fdbman_devsbmp = msg->devsbmp;
            fdbman_state = FST_MEMBER;
          }
          else {
            fdbman_newmaster = fdbman_master = msg->stack_id;
            fdbman_newserial = fdbman_serial = msg->serial;
            fdbman_devsbmp = msg->devsbmp;
            fdbman_state = FST_MEMBER;
            // DEBUG("ERROR: fdbman: FMS_MASTER recieved FMC_MASTER_READY from unit %hhu actually waiting it from unit %hhu\n",
            //       msg->stack_id, fdbman_newmaster);
          }
          break;
      }
      break;

    case FMC_UPDATE:
      switch (fdbman_state) {
        case FST_MASTER:
        case FST_PRE_MEMBER:
          fdbman_handle_msg_update(msg, len);
          break;
        case FST_MEMBER:
          break;
      }
      break;

    case FMC_MASTER_UPDATE:
      if (msg->stack_id != fdbman_master) {
        // DEBUG("ERROR: fdbman:  recieved alien:%d FMC_MASTER_UPDATE data block\n", msg->stack_id);
        break;
      }
      switch (fdbman_state) {
        case FST_MASTER:
          // DEBUG("ERROR: fdbman: FMS_MASTER recieved FMC_MASTER_UPDATE\n");
          break;
        case FST_PRE_MEMBER:
          if (msg->stack_id == fdbman_newmaster) {
            fdbman_handle_msg_master_update(msg, len);
          }
          else {
            // DEBUG("ERROR: fdbman: FMS_PRE_MEMBER recieved alien FMC_MASTER_UPDATE data block\n");
          }
          break;
        case FST_MEMBER:
          fdbman_handle_msg_master_update(msg, len);
          break;
      }
      break;

    case FMC_FLUSH:
      switch (fdbman_state) {
        case FST_MASTER:
        case FST_PRE_MEMBER:
          fdbman_handle_msg_flush(msg, len);
          break;
        case FST_MEMBER:
          break;
      }
      break;

    case FMC_MASTER_FLUSH:
      if (msg->stack_id != fdbman_master) {
        // DEBUG("ERROR: fdbman:  recieved alien:%d FMC_MASTER_UPDATE data block\n", msg->stack_id);
        break;
      }
      switch (fdbman_state) {
        case FST_MASTER:
          // DEBUG("ERROR: fdbman: FMS_MASTER recieved FMC_MASTER_FLUSH\n");
          break;
        case FST_PRE_MEMBER:
          // DEBUG("ERROR: fdbman: FMS_PRE_MEMBER recieved FMC_MASTER_FLUSH data block\n");
          break;
        case FST_MEMBER:
          fdbman_handle_msg_master_flush(msg, len);
          break;
      }
      break;

    case FMC_FLUSH_VIF:
      switch (fdbman_state) {
        case FST_MASTER:
        case FST_PRE_MEMBER:
          fdbman_handle_msg_flush_vif(msg, len);
          break;
        case FST_MEMBER:
          break;
      }
      break;

    case FMC_MASTER_FLUSH_VIF:
      if (msg->stack_id != fdbman_master) {
        // DEBUG("ERROR: fdbman:  recieved alien:%d FMC_MASTER_FLUSH_VIF data block\n", msg->stack_id);
        break;
      }
      switch (fdbman_state) {
        case FST_MASTER:
          // DEBUG("ERROR: fdbman: FMS_MASTER recieved FMC_MASTER_FLUSH_VIF\n");
          break;
        case FST_PRE_MEMBER:
          // DEBUG("ERROR: fdbman: FMS_PRE_MEMBER recieved FMC_MASTER_FLUSH_VIF data block\n");
          break;
        case FST_MEMBER:
          fdbman_handle_msg_master_flush_vif(msg, len);
          break;
      }
      break;

     case FMC_MACOP:
      switch (fdbman_state) {
        case FST_MASTER:
        case FST_PRE_MEMBER:
          fdbman_handle_msg_macop(msg, len);
          break;
        case FST_MEMBER:
          break;
      }
      break;

    case FMC_MASTER_MACOP:
      if (msg->stack_id != fdbman_master) {
        // DEBUG("ERROR: fdbman:  recieved alien:%d FMC_MASTER_MACOP data block\n", msg->stack_id);
        break;
      }
      switch (fdbman_state) {
        case FST_MASTER:
          // DEBUG("ERROR: fdbman: FMS_MASTER recieved FMC_MASTER_MACOP\n");
          break;
        case FST_PRE_MEMBER:
          // DEBUG("ERROR: fdbman: FMS_PRE_MEMBER recieved alien FMC_MASTER_MACOP data block\n");
          break;
        case FST_MEMBER:
          fdbman_handle_msg_master_macop(msg, len);
          break;
      }
      break;
    case FMC_MASTER_RT:
//      if (msg->stack_id != fdbman_master) {
//        DEBUG("ERROR: fdbman:  recieved alien:%d FMC_MASTER_RT data block\n", msg->stack_id);
//        break;
//      }
      fdbman_handle_msg_rt(msg, len);
      break;
    case FMC_MASTER_NA:
      if (msg->stack_id != fdbman_master) {
        // DEBUG("ERROR: fdbman:  recieved alien:%d FMC_MASTER_NA data block\n", msg->stack_id);
        break;
      }
      fdbman_handle_msg_na(msg, len);
      break;
    case FMC_OPNA:
      fdbman_handle_msg_opna(msg, len);
      break;
    case FMC_MASTER_UDT:
      if (msg->stack_id != fdbman_master) {
        // DEBUG("ERROR: fdbman:  recieved alien:%d FMC_MASTER_UDT data block\n", msg->stack_id);
        break;
      }
      fdbman_handle_msg_udt(msg, len);
      break;
    case FMC_MASTER_CLEAR_ROUTING:
      fdbman_handle_msg_clear_routing(msg, len);
      break;
    case FMC_VIF_LS:
      fdbman_handle_msg_vif_ls(msg, len);
      break;
    case FMC_VIFLS_GET:
      fdbman_handle_msg_vifls_get(msg, len);
      break;
    }

// DEBUG("2FDBMAN state: %d master: %d, newmaster: %d, fdbman_serial: %llu, fdbman_newserial: %llu\n", fdbman_state, fdbman_master, fdbman_newmaster, fdbman_serial, fdbman_newserial);
  return ST_OK;
}

static int
fdbcomm_ctl_handler (zloop_t *loop, zsock_t *reader, void *arg) {
  struct fdbcomm_thrd *a = (struct fdbcomm_thrd *) arg;
  zmsg_t *msg = zmsg_recv (a->tipc_ctl_sock);
  zframe_t *aframe = zmsg_first (msg);
  if (!aframe)
    goto out;
  char *pkt = (char*) zframe_data (aframe);
  assert(pkt);
  size_t len = zframe_size (aframe);

  if (TEMP_FAILURE_RETRY
      (sendto (a->fdb_sock, pkt, len, 0,
               (struct sockaddr *) &fdb_dst, sizeof (fdb_dst)))
      != len)
    err ("TIPC fdb sendmsg() failed");

out:
  zmsg_destroy (&msg);
  return 1;
}

enum status
fdb_send_pkt(const void *arg, uint32_t len) {

  zmsg_t *msg = zmsg_new ();
  zmsg_addmem (msg, arg, len);
  zmsg_send (&msg, fdbcomm_ctl_sock);

  return ST_OK;
}

static inline zloop_fn *
zfn (int (fn) (zloop_t *loop, zmq_pollitem_t *item, struct fdbcomm_thrd *arg)) {
  return (zloop_fn *) fn;
}

static int
fdbcomm_tipc_handler (zloop_t *loop, zmq_pollitem_t *pi, struct fdbcomm_thrd *a) {
  static char *buf[TIPC_MSG_MAX_LEN];
  ssize_t mlen;
  struct pti_fdbr_msg *fdb_msg = (struct pti_fdbr_msg *) buf;

  mlen = TEMP_FAILURE_RETRY(recv(a->fdb_sock, buf, sizeof(buf), 0));
  if (mlen <= 0) {
    ERR("tipc recv() failed(%s)\r\n", strerror(errno));
    return 0;
  }

//DEBUG("tipc-fdb-msg recvd, len==%d\n", mlen); //TODO remove
//PRINTHexDump(buf, mlen);

  if (fdb_msg->version != PTI_FDB_VERSION){
    return 0;
  }
  if (fdb_msg->stack_id == stack_id)
    return 0;

  mac_op_handle_pkt (fdb_msg, mlen, a->fdb_ctl_sock);

  return 1;
}


static volatile int fdbcomm_thread_started = 0;

static void *
fdbcomm_thread (void *z)
{
  static struct fdbcomm_thrd ft;

  ft.loop = zloop_new();
  assert(ft.loop);

  ft.fdb_sock = tipc_fdbcomm_connect();
  int rc;

  DEBUG ("starting up FDB communicator\r\n");

  DEBUG("create FDB control PUSH socket\r\n");
  ft.fdb_ctl_sock = zsock_new (ZMQ_PUSH);
  DEBUG("zsock_new done\r\n");
  assert (ft.fdb_ctl_sock);

  DEBUG("connect to %s\r\n", FDB_ACONTROL_EP);
  rc=zsock_connect (ft.fdb_ctl_sock, FDB_ACONTROL_EP);
  DEBUG("connect done\r\n");

  DEBUG("create FDB tipc control SUB socket\r\n");
  ft.tipc_ctl_sock = zsock_new (ZMQ_SUB);
  DEBUG("create done\r\n");
  assert (ft.tipc_ctl_sock);

  DEBUG("set socket subscribe\r\n");
  zsock_set_subscribe(ft.tipc_ctl_sock, "");
  DEBUG("set socket subscribe done\r\n");

  DEBUG("connect to %s\r\n", TIPC_POST_EP);
  rc=zsock_connect (ft.tipc_ctl_sock, TIPC_POST_EP);
  DEBUG("connect done\r\n");

  zmq_pollitem_t pitp = {NULL, ft.fdb_sock, ZMQ_POLLIN};

  DEBUG("create poller for fdb_sock\r\n");
  zloop_poller(ft.loop, &pitp, zfn (fdbcomm_tipc_handler), &ft);
  DEBUG("create poller for fdb_sock done\r\n");

  DEBUG("create poller for tipc_ctl_sock\r\n");
  rc=zloop_reader(ft.loop, ft.tipc_ctl_sock, fdbcomm_ctl_handler, &ft);
  DEBUG("create poller for tipc_ctl_sock done\r\n");

  DEBUG("do prctl\r\n");
  prctl(PR_SET_NAME, "fdb-comm", 0, 0, 0);
  DEBUG("prctl done");

  DEBUG ("FDB communicator startup done\r\n");
  fdbcomm_thread_started = 1;

  zloop_start(ft.loop);

  return NULL;
}

static volatile int fdb_thread_started = 0;

static void *
fdb_thread (void *_)
{
  void *not_sock, *tctl_sock, *tctl_asock;
  zloop_t *loop;

  DEBUG ("starting up FDB\r\n");

  loop = zloop_new ();
  assert (loop);

  not_sock = zsock_new (ZMQ_PULL);
  assert (not_sock);
  zsock_bind (not_sock, FDB_NOTIFY_EP);

  zloop_reader (loop, not_sock, fdb_evt_handler, not_sock);
  zloop_timer (loop, 1000, 0, fdb_upd_timer, not_sock);

  tctl_sock = zsock_new (ZMQ_REP);
  assert (tctl_sock);
  zsock_bind (tctl_sock, FDB_CONTROL_EP);

  tctl_asock = zsock_new (ZMQ_PULL);
  assert (tctl_asock);
  zsock_bind (tctl_asock, FDB_ACONTROL_EP);

  zloop_reader (loop, tctl_sock, fdb_ctl_shandler, tctl_sock);

  zloop_reader (loop, tctl_asock, fdb_ctl_ahandler, tctl_asock);

  pub_sock = zsock_new (ZMQ_PUB);
  assert (pub_sock);
  zsock_bind (pub_sock, FDB_PUBSUB_EP);

  scmd_sock = zsock_new (ZMQ_REQ);
  assert (scmd_sock);
  int rc = zsock_connect (scmd_sock, INP_SOCK_EP);

  acmd_sock = zsock_new (ZMQ_PUSH);
  assert (acmd_sock);
  rc = zsock_connect (acmd_sock, STACK_CMD_SOCK_EP);

  evt_sock = zsock_new (ZMQ_PUSH);
  assert (evt_sock);
  rc = zsock_connect (evt_sock, NOTIFY_QUEUE_EP);

  prctl(PR_SET_NAME, "fdb", 0, 0, 0);

  DEBUG ("FDB startup done\r\n");
  fdb_thread_started = 1;
  zloop_start (loop);

  return NULL;
}

enum status
mac_start (void)
{
  pthread_t tid, tidcomm;
  struct fdb_flush_arg fa = {
    .aa = {
      .vid = ALL_VLANS,
      .port = ALL_PORTS,
      .bmp_devs = LOCAL_DEV
    },
    .ds = GT_TRUE
  };

  GT_U32 bmp = 0, n;
  GT_STATUS rc;
  int d;

  for_each_dev (d)
    bmp |= 1 << phys_dev (d);
  fdbman_devsbmp = 1 << stack_id;

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
    CRP (cpssDxChBrgFdbDeviceTableSet (d, 0xFFFFFFFF));

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

  fdbman_state = FST_MASTER;
  fdbman_newmaster = fdbman_master = stack_id;
  memset (fdb, 0, sizeof (fdb));
  pthread_create (&tid, NULL, fdb_thread, NULL);
  DEBUG ("waiting for FDB startup\r\n");
  n = 0;
  while (!fdb_thread_started) {
    n++;
    usleep (10000);
  }
  DEBUG ("FDB startup finished after %d iterations\r\n", n);

  gctl_sock = zsock_new (ZMQ_REQ);
  assert (gctl_sock);
  zsock_connect (gctl_sock, FDB_CONTROL_EP);

  gctl_asock_control = zsock_new (ZMQ_PUSH);
  assert (gctl_asock_control);
  zsock_connect (gctl_asock_control, FDB_ACONTROL_EP);

  gctl_asock_evt_phy = zsock_new (ZMQ_PUSH);
  assert (gctl_asock_evt_phy);
  zsock_connect (gctl_asock_evt_phy, FDB_ACONTROL_EP);

  gctl_sock_secbr = zsock_new (ZMQ_REQ);
  assert (gctl_sock_secbr);
  zsock_connect (gctl_sock_secbr, FDB_CONTROL_EP);

  fdbcomm_ctl_sock = zsock_new (ZMQ_PUB);
  assert (fdbcomm_ctl_sock);
  zsock_bind (fdbcomm_ctl_sock, TIPC_POST_EP);

  pthread_create (&tidcomm, NULL, fdbcomm_thread, NULL);
  DEBUG ("waiting for FDB communicator startup\r\n");
  n = 0;
  while (!fdbcomm_thread_started) {
    n++;
    usleep (10000);
  }
  DEBUG ("FDB communicator startup finished after %d iterations\r\n", n);

  gctl_sock = zsock_new (ZMQ_REQ);
  assert (gctl_sock);
  zsock_connect (gctl_sock, FDB_CONTROL_EP);

  fdb_flush (&fa);

  for_each_dev (d) {
    CRP (cpssDxChBrgFdbAuMsgRateLimitSet (d, 400, GT_TRUE));
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
    fdb_ctl_control (FCC_MAC_OP, &mo, sizeof (mo));
    mo.port = stack_sec_port->id;
    memcpy(mo.mac, mac_sec, 6);
    fdb_ctl_control (FCC_MAC_OP, &mo, sizeof (mo));
  }
  return ST_OK;
}
