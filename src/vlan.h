#ifndef __VLAN_H__
#define __VLAN_H__

#include <control-proto.h>

extern int vlan_init (void);
extern enum status vlan_dump (vid_t);
extern GT_STATUS vlan_set_mac_addr (GT_U16, const unsigned char *);

#endif /* __VLAN_H__ */
