#include "control-proto.h"

#define ARRAY_SIZE(a) (sizeof (a) / sizeof (*a))

#define IPSG_REP_EP "ipc:///var/tmp/ipsg_to_prest.rep"
#define IPSG_REQ_EP "ipc:///var/tmp/ipsg_to_prest.req"

enum pcl_frame_t {
  RULE_SET = 0,
  RULE_UNSET = 1,
  TRAP_ENABLE = 2,
  TRAP_DISABLE = 3,
  DROP_ENABLE = 4,
  DROP_DISABLE = 5
};

enum ntf_frame_t {
  TRAP_ENABLED = 0,
  CH_GR_SET = 1,
  CH_GR_RESET = 2
};

extern int ipsg_init (void);
extern int ipsg_start (void);

extern void notify_ch_gr_set (vif_id_t vifid, port_id_t pi);
extern void notify_ch_gr_reset (vif_id_t vifid);
extern void notify_trap_enabled (port_id_t, vid_t, mac_addr_t, ip_addr_t);

