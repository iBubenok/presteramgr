#ifndef __PDSA_H__
#define __PDSA_H__

#include <control-proto.h>

extern enum status pdsa_init (void);
extern enum status pdsa_vlan_if_op (vid_t, int);

#endif /* __PDSA_H__ */
