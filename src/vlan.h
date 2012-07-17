#ifndef __VLAN_H__
#define __VLAN_H__

#include <control-proto.h>

extern int vlan_dot1q_tag_native;

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
  enum vlan_state state;
};

extern struct vlan vlans[4095];

static inline int
vlan_valid (vid_t vid)
{
  return vid > 0 && vid < 4096;
}

extern int vlan_init (void);
extern enum status vlan_dump (vid_t);
extern enum status vlan_add (vid_t);
extern enum status vlan_delete (vid_t);
extern enum status vlan_set_dot1q_tag_native (int);
extern GT_STATUS vlan_set_mac_addr (GT_U16, const unsigned char *);
extern enum status vlan_set_cpu (vid_t, bool_t);
extern enum status vlan_set_fdb_map (const stp_id_t *);
extern enum status vlan_get_mac_addr (vid_t, mac_addr_t);


#endif /* __VLAN_H__ */
