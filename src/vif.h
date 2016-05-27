#ifndef __VIF_H__
#define __VIF_H__

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdbHash.h>

#include <control-proto.h>
#include <sysdeps.h>

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

  enum port_mode mode;
  int trust_cos;
  int trust_dscp;
  enum port_speed c_speed;
  int c_speed_auto;
  enum port_duplex c_duplex;
  int c_shutdown;

  void (*fdb_fill_dest) (struct vif *, CPSS_MAC_ENTRY_EXT_STC *);
  enum status (*set_speed) (struct vif *, const struct port_speed_arg *);
  enum status (*set_duplex) (struct vif *, enum port_duplex);
  enum status (*update_sd) (struct vif *);
  enum status (*shutdown) (struct vif *, int);
  enum status (*set_mdix_auto) (struct vif *, int);
};

typedef struct vif *vifp_single_dev_t[CPSS_MAX_PORTS_NUM_CNS];

extern vifp_single_dev_t vifp_by_hw[];

static inline struct vif*
vif_by_hw(GT_U8 hdev, GT_U8 hport) {
  return vifp_by_hw[hdev][hport];
}

extern void vif_init (void);
extern void vif_post_port_init (void);
extern struct vif* vif_get (vif_type_t, uint8_t, uint8_t);
extern struct vif* vif_getn (vif_id_t);
extern enum status vif_get_hw_port (struct hw_port *, vif_type_t, uint8_t, uint8_t);
extern struct vif* vif_get_by_gif(uint8_t, uint8_t, uint8_t);
extern enum status vif_get_hw_port_by_index (struct hw_port *, uint8_t, uint8_t);
extern enum status vif_get_hw_ports (struct vif_def *);
extern enum status vif_set_hw_ports (uint8_t, uint8_t, const struct vif_def *);
extern void vif_set_trunk_members (trunk_id_t, int, struct trunk_member *);
extern enum status vif_tx (const struct vif_id *, const struct vif_tx_opts *, uint16_t, const void *);

extern enum status vif_set_speed (vif_id_t, const struct port_speed_arg *);
extern enum status vif_set_speed_remote (struct vif *, const struct port_speed_arg *);
extern enum status vif_set_speed_port (struct vif *, const struct port_speed_arg *);
extern enum status vif_set_speed_trunk (struct vif *, const struct port_speed_arg *);

#endif /* __VIF_H__ */
