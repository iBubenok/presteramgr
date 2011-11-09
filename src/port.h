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
  struct port_state state;
};

extern struct port *ports;
extern int nports;

static inline int
port_valid (int n)
{
  return n >= 0 && n < nports;
}

extern int port_init (void);
extern GT_STATUS port_set_sgmii_mode (int);
extern int port_exists (GT_U8, GT_U8);
extern int port_num (GT_U8, GT_U8);
extern void port_handle_link_change (GT_U8, GT_U8);
extern enum status port_get_state (port_num_t, struct port_link_state *);
extern enum status port_set_stp_state (port_num_t, stp_id_t, enum port_stp_state);


#endif /* __PORT_H__ */
