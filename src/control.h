#ifndef __CONTROL_H__
#define __CONTROL_H__

#include <cpss/generic/port/cpssPortCtrl.h>
#include <linux/pdsa-mgmt.h>
#include <control-proto.h>

#define INP_SOCK_EP "inproc://command"
#define INP_PUB_SOCK_EP "inproc://notify"

extern void control_pre_mac_init(void);
extern int control_init (void);
extern int control_start (void);

int event_forward (zloop_t *loop, zsock_t *event_pull_sock, void *arg);
int event_forward_sub (zloop_t *loop, zsock_t *sub_sock, void *arg);

enum control_int_command {
  CC_INT_ROUTE_ADD_PREFIX = CC_MAX,
  CC_INT_ROUTE_DEL_PREFIX,
  CC_INT_SPEC_FRAME_FORWARD,
  CC_INT_RET_SET_MAC_ADDR,
  SC_INT_RTBD_CMD,
  SC_INT_NA_CMD,
  SC_INT_OPNA_CMD,
  SC_INT_UDT_CMD,
  SC_INT_CLEAR_RT_CMD,
  CC_INT_GET_RT_CMD,
  CC_INT_GET_UDADDRS_CMD,
  SC_INT_CLEAR_RE_CMD,
  SC_INT_VIF_SET_STP_STATE,
  CC_INT_MAX
};

#define ALL_STP_IDS 0xFFFF

enum control_int_notification {
  CN_INT_PORT_VID_SET = CN_MAX,
  CN_INT_MAX
};

extern void cn_port_vid_set (port_id_t, vid_t);
extern void cn_mail (port_stack_role_t, uint8_t *, size_t);
extern enum status control_spec_frame (struct pdsa_spec_frame *);

#endif /* __CONTROL_H__ */
