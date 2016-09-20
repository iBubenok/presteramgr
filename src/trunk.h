#ifndef __TRUNK_H__
#define __TRUNK_H__

#include "vif.h"
#include <port.h>

#define TRUNK_MAX_MEMBERS 8
#define TRUNK_MAX 127

struct trunk_port {
  int valid;
  int enabled;
  uint8_t ldev;
  uint8_t lport;
  uint8_t hdev;
  uint8_t hport;
};

struct trunk {
  struct vif vif;
  trunk_id_t id;
  int nports;
  struct vif *designated;
  struct vif *vif_port[TRUNK_MAX_MEMBERS];
  uint8_t port_enabled[TRUNK_MAX_MEMBERS];
};

extern struct trunk trunks[];

static inline struct vif*
vif_by_trunkid(GT_U8 trunkid) {
  return (struct vif*) &trunks[trunkid];
}

extern void trunk_init (void);
extern enum status trunk_set_members (trunk_id_t, int, struct trunk_member *, void *);
extern enum status trunk_set_balance_mode(traffic_balance_mode_t mode);


#endif /* __TRUNK_H__ */
