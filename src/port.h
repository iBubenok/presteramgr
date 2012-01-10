#ifndef __PORT_H__
#define __PORT_H__

#include <cpss/generic/port/cpssPortCtrl.h>
#include <control-proto.h>


struct port_state {
  CPSS_PORT_ATTRIBUTES_STC attrs;
};

struct port {
  GT_U8 ldev;
  GT_U8 lport;
  enum port_mode mode;
  vid_t access_vid;
  vid_t native_vid;
  int trust_cos;
  int trust_dscp;
  enum port_speed c_speed;
  int c_speed_auto;
  struct port_state state;
  enum status (*set_speed) (struct port *, const struct port_speed_arg *);
};

extern struct port *ports;
extern int nports;

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

extern int port_init (void);
extern GT_STATUS port_set_sgmii_mode (port_id_t);
extern int port_exists (GT_U8, GT_U8);
extern int port_id (GT_U8, GT_U8);
extern void port_handle_link_change (GT_U8, GT_U8);
extern enum status port_get_state (port_id_t, struct port_link_state *);
extern enum status port_set_stp_state (port_id_t, stp_id_t, enum port_stp_state);
extern enum status port_set_mode (port_id_t, enum port_mode);
extern enum status port_set_access_vid (port_id_t, vid_t);
extern enum status port_set_native_vid (port_id_t, vid_t);
extern enum status port_set_speed (port_id_t, const struct port_speed_arg *);
extern enum status port_shutdown (port_id_t, int);
extern enum status port_block (port_id_t, const struct port_block *);
extern enum status port_update_qos_trust (const struct port *);


#endif /* __PORT_H__ */
