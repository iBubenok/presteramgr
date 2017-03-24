#ifndef __VIF_H__
#define __VIF_H__

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdbHash.h>

#include <control-proto.h>
#include <sysdeps.h>
#include <vlan.h>

#define PORT_DEF(name) struct port_def name[NPORTS];

struct vif {
  int valid;
  union {
    vif_id_t id;
    struct vif_id vifid;
  } __attribute__ ((packed));
  int islocal;
  struct vif *trunk;
  union {
    struct port *local;
    struct hw_port remote;
  };
  struct port_link_state state;
  uint8_t stg_state[256];

  enum port_mode mode;
  vid_t native_vid;
  vid_t voice_vid;
  int trust_cos;
  int trust_dscp;
  enum port_speed c_speed;
  int c_speed_auto;
  enum port_duplex c_duplex;
  int c_shutdown;

  void (*fill_cpss_if) (struct vif *, CPSS_INTERFACE_INFO_STC *);
  enum status (*mcg_add_vif) (struct vif *, mcg_t mcg);
  enum status (*mcg_del_vif) (struct vif *, mcg_t mcg);
  enum status (*set_speed) (struct vif *, const struct port_speed_arg *);
  enum status (*set_duplex) (struct vif *, enum port_duplex);
  enum status (*update_sd) (struct vif *);
  enum status (*shutdown) (struct vif *, int);
  enum status (*block) (struct vif *, const struct port_block *);
  enum status (*set_access_vid) (struct vif *, vid_t);
  enum status (*set_comm) (struct vif *, port_comm_t);
  enum status (*set_customer_vid) (struct vif *, vid_t);
  enum status (*set_mode) (struct vif *, enum port_mode);
  enum status (*set_pve_dst) (struct vif *, port_id_t, int);
  enum status (*set_protected) (struct vif *, bool_t);
  enum status (*set_native_vid) (struct vif *, vid_t);
  enum status (*set_trunk_vlans) (struct vif *, const uint8_t *);
  enum status (*set_stp_state) (struct vif *, stp_id_t, int, enum port_stp_state);
  enum status (*set_mdix_auto) (struct vif *, int);
  enum status (*vlan_translate) (struct vif *, vid_t, vid_t, int);
  enum status (*dgasp_op) (struct vif *, int add);
  enum status (*set_flow_control) (struct vif *, flow_control_t fc);
  enum status (*set_voice_vid) (struct vif *, vid_t);
};

struct vif_stgblk_header {
  uint8_t n;
  uint8_t st_id;
  serial_t serial;
  uint8_t data[];
};

struct vif_link_state {
  vif_id_t vifid;
  struct port_link_state state;
} __attribute__ ((packed));

struct vif_link_state_header {
  uint16_t n;
  uint8_t stack_id;
  serial_t serial;
  struct vif_link_state data[];
} __attribute__ ((packed));

typedef struct vif *vifp_single_dev_t[CPSS_MAX_PORTS_NUM_CNS];

extern vifp_single_dev_t vifp_by_hw[];

static inline struct vif*
vif_by_hw(GT_U8 hdev, GT_U8 hport) {
  return vifp_by_hw[hdev][hport];
}

static inline int
vif_is_forwarding_on_vlan(struct vif *vif, vid_t vid) {
  switch(vif->stg_state[vlans[vid - 1].stp_id]) {
    case STP_STATE_FORWARDING:
    case STP_STATE_DISABLED:
      return 1;
    default:
      return 0;
  };
}

#define STP_STATES_PER_BYTE (8 / STP_STATE_BITS_WIDTH)

struct vif_stg {
  vif_id_t id;
  uint8_t stgs[32 * STP_STATE_BITS_WIDTH + !!(8 % STP_STATE_BITS_WIDTH)];
};

extern void vif_rlock(void);
extern void vif_wlock(void);
extern void vif_unlock(void);
extern void vif_init (void);
extern void vif_post_port_init (void);
extern void vif_remote_proc_init(struct vif*);
extern void vif_port_proc_init(struct vif*);
extern void vif_trunk_proc_init(struct vif*);
extern struct vif* vif_get (vif_type_t, uint8_t, uint8_t);
extern struct vif* vif_getn (vif_id_t);
extern struct vif* vif_get_by_pid (uint8_t, port_id_t);
extern enum status vif_get_hw_port (struct hw_port *, vif_type_t, uint8_t, uint8_t);
extern struct vif* vif_get_by_gif(uint8_t, uint8_t, uint8_t);
extern enum status vif_get_hw_port_by_index (struct hw_port *, uint8_t, uint8_t);
extern enum status vif_get_hw_ports (struct vif_def *);
extern enum status vif_set_hw_ports (uint8_t, uint8_t, const struct vif_def *);
extern enum status vif_stg_get (void *);
extern enum status vif_stg_set (void *);
extern enum status vif_stg_get_single (struct vif*, uint8_t *, int);
extern void vif_set_trunk_members (trunk_id_t, int, struct trunk_member *, void *);
extern enum status vif_set_link_status(vif_id_t, struct port_link_state *, void *);
extern enum status vif_process_ls_pkt(struct vif_link_state_header *, void *);
extern struct vif_link_state_header *vif_form_ls_sync_pkt(void *, size_t);
extern enum status vif_tx (const struct vif_id *, const struct vif_tx_opts *, uint16_t, const void *);
extern enum status vif_enable_eapol (vif_id_t, bool_t);
extern enum status vif_eapol_auth (vif_id_t, vid_t, mac_addr_t, bool_t);

/*
extern enum status vif_set_speed (vif_id_t, const struct port_speed_arg *);
extern enum status vif_set_speed_remote (struct vif *, const struct port_speed_arg *);
extern enum status vif_set_speed_port (struct vif *, const struct port_speed_arg *);
extern enum status vif_set_speed_trunk (struct vif *, const struct port_speed_arg *);
*/

#define VIF_DEF_PROC(proc, arg...) \
extern enum status vif_##proc (vif_id_t, ##arg); \
extern enum status vif_##proc##_remote (struct vif *, ##arg); \
extern enum status vif_##proc##_port (struct vif *, ##arg); \
extern enum status vif_##proc##_trunk (struct vif *, ##arg)

VIF_DEF_PROC(mcg_add_vif, mcg_t mcg);
VIF_DEF_PROC(mcg_del_vif, mcg_t mcg);
VIF_DEF_PROC(set_speed, const struct port_speed_arg *);
VIF_DEF_PROC(set_duplex, enum port_duplex);
VIF_DEF_PROC(shutdown, int shutdown);
VIF_DEF_PROC(block, const struct port_block *);
VIF_DEF_PROC(set_access_vid, vid_t vid);
VIF_DEF_PROC(set_comm, port_comm_t comm);
VIF_DEF_PROC(set_customer_vid, vid_t vid);
VIF_DEF_PROC(set_mode, enum port_mode mode);
VIF_DEF_PROC(set_pve_dst, port_id_t dpid, int enable);
VIF_DEF_PROC(set_protected, bool_t protected);
VIF_DEF_PROC(set_native_vid, vid_t vid);
VIF_DEF_PROC(set_trunk_vlans, const uint8_t *bmp);
VIF_DEF_PROC(vlan_translate, vid_t from, vid_t to, int add);
VIF_DEF_PROC(set_stp_state, stp_id_t stp_id,
  int all, enum port_stp_state state);
VIF_DEF_PROC(dgasp_op, int add);
VIF_DEF_PROC(set_flow_control, flow_control_t fc);
VIF_DEF_PROC(set_voice_vid, vid_t vid);

#endif /* __VIF_H__ */
