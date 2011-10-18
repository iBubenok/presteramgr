#ifndef __PRESTERAMGR_H__
#define __PRESTERAMGR_H__

#include <cpssdefs.h>

#include <cpss/extServices/os/gtOs/gtGenTypes.h>


/*
 * init.c
 */
extern void cpss_start (void);

/*
 * event.c
 */
extern void event_enter_loop (void);

/*
 * port.c
 */

struct port {
  GT_U8 ldev;
  GT_U8 lport;
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

/*
 * mgmt.c
 */
extern int mgmt_init (void);

/*
 * vlan.c
 */
extern int vlan_init (void);
extern GT_STATUS vlan_set_mac_addr (GT_U16, const unsigned char *);

/*
 * extsvc.c
 */
extern GT_STATUS extsvc_bind (void);
extern int osPrintSync (const char *, ...);


#endif /* __PRESTERAMGR_H__ */
