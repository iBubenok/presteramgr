#ifndef __PORT_H__
#define __PORT_H__

#include <cpss/generic/port/cpssPortCtrl.h>
#include <control-proto.h>
#include <vif.h>
#include <stack.h>
#include <mac.h>

struct port_state {
  CPSS_PORT_ATTRIBUTES_STC attrs;
};

struct port_vlan_conf {
  uint32_t tallow : 1;
  uint32_t xlate  : 1;
  uint32_t map_to : 12;
  uint32_t refc   : 12;
};

struct port {
  struct vif vif;
  port_id_t id;
  port_type_t type;
  GT_U8 ldev;
  GT_U8 lport;
  enum port_mode mode;
  vid_t access_vid;
  vid_t native_vid;
  vid_t customer_vid;
  vid_t voice_vid;
  int trust_cos;
  int trust_dscp;
  enum port_speed c_speed;
  int c_speed_auto;
  enum port_duplex c_duplex;
  int c_shutdown;
  int c_protected;
  int c_prot_comm;
  CPSS_PORTS_BMP_STC iso_bmp;
  int iso_bmp_changed;
  int tdr_test_in_progress;
  struct port_state state;
  enum port_speed max_speed;
  struct port_vlan_conf vlan_conf[4094];
  int def_xlate;
  vid_t def_map_to;
  trunk_id_t trunk_id;
  int fdb_new_addr_notify_enabled;
  int fdb_addr_op_notify_enabled;
  int fdb_insertion_enabled;
  enum port_stack_role stack_role;
  CPSS_PORT_ATTRIBUTES_STC attrs;

  /* Port Security. */
  pthread_mutex_t psec_lock;
  int psec_enabled;
  int psec_action;
  int psec_trap_interval;
  enum psec_mode psec_mode;
  int psec_max_addrs;
  int psec_naddrs;
  /* END: Port Security. */

  enum status (*set_speed) (struct port *, const struct port_speed_arg *);
  enum status (*set_duplex) (struct port *, enum port_duplex);
  enum status (*update_sd) (struct port *);
  enum status (*shutdown) (struct port *, int);
  enum status (*set_mdix_auto) (struct port *, int);
  enum status (*setup) (struct port *);
};

extern struct port *ports;
extern int nports;
extern CPSS_PORTS_BMP_STC all_ports_bmp[];
extern CPSS_PORTS_BMP_STC nst_ports_bmp[];

static inline int
port_valid (port_id_t n)
{
  return n >= 1 && n <= nports;
}

static inline struct port *
port_ptr (port_id_t n)
{
  return port_valid (n) ? &ports[n - 1] : NULL;
}

static inline int
is_stack_port (const struct port *port)
{
  return stack_active () && (port->stack_role != PSR_NONE);
}

extern void port_psec_status_rlock(void);
extern void port_psec_status_wlock(void);
extern void port_psec_status_unlock(void);
extern int port_init (void);
extern void port_disable_all (void);
extern enum status port_start (void);
extern int port_exists (GT_U8, GT_U8);
extern int port_id (GT_U8, GT_U8);
extern int port_is_phyless (struct port *);
extern enum status port_handle_link_change (GT_U8, GT_U8, vif_id_t *, port_id_t *, CPSS_PORT_ATTRIBUTES_STC *);
extern enum status port_get_state (port_id_t, struct port_link_state *);
extern enum status port_get_type (port_id_t, port_type_t *);
extern enum status port_set_stp_state (port_id_t, stp_id_t, int, enum port_stp_state);
extern enum status port_get_stp_state (port_id_t, stp_id_t, enum port_stp_state*);
extern enum status port_set_mode (port_id_t, enum port_mode);
extern enum status port_set_access_vid (port_id_t, vid_t);
extern enum status port_set_voice_vid (port_id_t, vid_t);
extern enum status port_set_native_vid (port_id_t, vid_t);
extern enum status port_set_speed (port_id_t, const struct port_speed_arg *);
extern enum status port_set_duplex (port_id_t, port_duplex_t);
extern enum status port_shutdown (port_id_t, int);
extern enum status port_block (port_id_t, const struct port_block *);
extern enum status port_update_qos_trust (const struct port *);
extern enum status port_dump_phy_reg (port_id_t, uint16_t, uint16_t, uint16_t *);
extern enum status port_set_sfp_mode (port_id_t, enum port_sfp_mode mode);
extern bool_t port_is_xg_sfp_present (port_id_t pid);
extern uint8_t* port_read_xg_sfp_idprom (port_id_t pid, uint16_t addr);
extern enum status port_set_xg_sfp_mode (port_id_t, enum port_sfp_mode mode);
extern enum status port_set_phy_reg (port_id_t, uint16_t, uint16_t, uint16_t);
extern enum status port_set_mdix_auto (port_id_t, int);
extern enum status port_set_flow_control (port_id_t, flow_control_t);
extern enum status port_get_stats (port_id_t, void *);
extern enum status port_clear_stats (port_id_t);
extern enum status port_set_rate_limit (port_id_t, const struct rate_limit *);
extern enum status port_rate_limit_drop_enable (port_id_t, int enable);
extern enum status get_rate_limit_drop_counter (int, uint64_t*);
extern enum status set_rate_limit_drop_counter (int, uint64_t);
extern enum status port_set_traffic_shape (port_id_t, bool_t, bps_t, burst_t);
extern enum status port_set_traffic_shape_queue (port_id_t, bool_t, queueid_t, bps_t, burst_t);
extern enum status port_set_protected (port_id_t, bool_t);
extern enum status port_set_comm (port_id_t, port_comm_t);
extern enum status port_set_igmp_snoop (port_id_t, bool_t);
extern enum status port_set_mru (uint16_t);
extern enum status port_set_pve_dst (port_id_t, vif_id_t, int);
extern enum status port_set_combo_preferred_media (port_id_t pid, combo_pref_media_t media);
extern enum status port_tdr_test_start (port_id_t);
extern enum status port_tdr_test_get_result (port_id_t, struct vct_cable_status *);
extern enum status port_set_customer_vid (port_id_t, vid_t);
extern enum status port_vlan_translate (port_id_t, vid_t, vid_t, int);
extern enum status port_clear_translation (port_id_t);
extern enum status port_set_trunk_vlans (port_id_t, const uint8_t *);
extern void port_update_trunk_vlan_all_ports (vid_t);
extern enum status port_enable_queue (port_id_t, uint8_t, bool_t);
extern enum status port_enable_eapol (port_id_t, bool_t);
extern enum status port_enable_eapol_vif (vif_id_t, bool_t);
extern enum status port_eapol_auth (port_id_t, vid_t, mac_addr_t, bool_t);
extern enum status port_eapol_auth_vif (vif_id_t, vid_t, mac_addr_t, bool_t);
extern enum status port_fdb_new_addr_notify (port_id_t, bool_t);
extern enum status port_fdb_addr_op_notify (port_id_t, bool_t);
extern enum status port_get_serdes_cfg (port_id_t, struct port_serdes_cfg *);
extern enum status port_set_serdes_cfg (port_id_t, const struct port_serdes_cfg *);
extern enum status port_dgasp_op (port_id_t pid, int add);

/* Port Security. */

enum psec_addr_status {
  PAS_OK,
  PAS_FULL,
  PAS_LIMIT,
  PAS_PROHIBITED
};

extern enum status psec_set_mode (port_id_t, psec_mode_t);
extern enum status psec_set_max_addrs (port_id_t, psec_max_addrs_t);
extern enum status psec_enable (port_id_t, int, psec_action_t, uint32_t);
extern enum psec_addr_status psec_addr_check (struct fdb_entry *, CPSS_MAC_ENTRY_EXT_STC *, int);
extern void psec_addr_del (CPSS_MAC_ENTRY_EXT_STC *);
extern void psec_after_flush (void);
extern enum status psec_enable_na_sb (port_id_t, int);
extern enum status psec_enable_na_sb_all (int);

/* END: Port Security. */

#endif /* __PORT_H__ */
