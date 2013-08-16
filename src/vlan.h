#ifndef __VLAN_H__
#define __VLAN_H__

#include <control-proto.h>

extern int vlan_dot1q_tag_native;
extern int vlan_xlate_tunnel;

enum vlan_state {
  VS_DELETED,
  VS_SHUTDOWN,
  VS_ACTIVE
};

struct vlan {
  vid_t vid;
  stp_id_t stp_id;
  int c_cpu;
  int mac_addr_set;
  mac_addr_t c_mac_addr;
  uint32_t mac_idx;
  int ip_addr_set;
  ip_addr_t c_ip_addr;
  int vt_refc;
  enum vlan_state state;
};

#define NVLANS 4094
#define SVC_VID 4095

extern struct vlan vlans[NVLANS];

static inline int
vlan_valid (vid_t vid)
{
  return vid > 0 && vid < 4095;
}

#define VLAN_TPID 0x8100
#define VLAN_TPID_IDX 0
#define FAKE_TPID 0xFFFF
#define FAKE_TPID_IDX 7

extern int vlan_init (void);
extern enum status vlan_dump (vid_t);
extern enum status vlan_add (vid_t);
extern enum status vlan_delete (vid_t);
extern enum status vlan_set_dot1q_tag_native (int);
extern GT_STATUS vlan_set_mac_addr (GT_U16, const unsigned char *);
extern enum status vlan_set_cpu (vid_t, bool_t);
extern enum status vlan_set_fdb_map (const stp_id_t *);
extern enum status vlan_get_mac_addr (vid_t, mac_addr_t);
extern enum status vlan_get_ip_addr (vid_t, ip_addr_t);
extern enum status vlan_set_ip_addr (vid_t, ip_addr_t);
extern enum status vlan_del_ip_addr (vid_t);
extern int stg_is_active (stp_id_t);
extern void vlan_svc_enable_port (port_id_t, int);
extern enum status vlan_set_xlate_tunnel (int);

#endif /* __VLAN_H__ */
