#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <presteramgr.h>
#include <debug.h>
#include <sysdeps.h>
#include <variant.h>
#include <port.h>
#include <control.h>
#include <data.h>
#include <vlan.h>
#include <qos.h>
#include <utils.h>
#include <env.h>
#include <pcl.h>
#include <dev.h>
#include <mac.h>
#include <sec.h>
#include <zcontext.h>

#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortStat.h>
#include <cpss/dxCh/dxChxGen/port/cpssDxChPortTx.h>
#include <cpss/dxCh/dxChxGen/phy/cpssDxChPhySmi.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgStp.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgVlan.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgEgrFlt.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgGen.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgPrvEdgeVlan.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgSecurityBreach.h>
#include <cpss/dxCh/dxChxGen/cos/cpssDxChCos.h>
#include <cpss/dxCh/dxChxGen/cscd/cpssDxChCscd.h>
#include <cpss/dxCh/dxChxGen/nst/cpssDxChNstPortIsolation.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIpCtrl.h>
#include <cpss/generic/cscd/cpssGenCscd.h>
#include <cpss/generic/port/cpssPortTx.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>
#include <cpss/generic/smi/cpssGenSmi.h>
#include <cpss/generic/phy/cpssGenPhyVct.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgFdb.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgNestVlan.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgSrcId.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>
#include <cpss/generic/cpssHwInit/cpssLedCtrl.h>
#include <cpss/dxCh/dxChxGen/cpssHwInit/cpssDxChHwInitLedCtrl.h>

#include <cpss/generic/phy/private/prvCpssGenPhySmi.h>

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <sys/prctl.h>


DECLSHOW (CPSS_PORT_SPEED_ENT);
DECLSHOW (CPSS_PORT_DUPLEX_ENT);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t port_phy_lock = PTHREAD_MUTEX_INITIALIZER;

struct port *ports = NULL;
int nports = 0;

typedef port_id_t dev_ports_t[CPSS_MAX_PORTS_NUM_CNS];
static dev_ports_t dev_ports[32];

CPSS_PORTS_BMP_STC all_ports_bmp[NDEVS];
/// non stack ports bmp but including interconnect ports
CPSS_PORTS_BMP_STC nst_ports_bmp[NDEVS];

static enum status port_set_speed_fe (struct port *, const struct port_speed_arg *);
static enum status port_set_duplex_fe (struct port *, enum port_duplex);
static enum status port_update_sd_fe (struct port *);
static enum status __port_shutdown_fe (GT_U8, GT_U8, int);
static enum status port_shutdown_fe (struct port *, int);
static enum status port_set_mdix_auto_fe (struct port *, int);
static enum status __port_setup_fe (GT_U8, GT_U8);
static enum status port_setup_fe (struct port *);

static enum status port_set_speed_ge (struct port *, const struct port_speed_arg *);
static enum status port_set_duplex_ge (struct port *, enum port_duplex);
static enum status port_update_sd_ge (struct port *);
static enum status port_shutdown_ge (struct port *, int);
static enum status port_set_mdix_auto_ge (struct port *, int);
static enum status port_setup_ge (struct port *);
static enum status __attribute__ ((unused)) port_setup_phyless_ge (struct port *);

static enum status port_set_speed_xg (struct port *, const struct port_speed_arg *);
static enum status port_set_duplex_xg (struct port *, enum port_duplex);
static enum status port_update_sd_xg (struct port *);
static enum status port_shutdown_xg (struct port *, int);
static enum status port_set_mdix_auto_xg (struct port *, int);
static enum status port_setup_xg (struct port *);

static inline void
port_lock (void)
{
  pthread_mutex_lock (&lock);
}

static inline void
port_unlock (void)
{
  pthread_mutex_unlock (&lock);
}

int
port_id (GT_U8 hdev, GT_U8 hport)
{
  /* FIXME: that's not really the way to do it. */
  if (hdev == 0)
    hdev = stack_id;

  if (hdev > 31 || hport >= CPSS_MAX_PORTS_NUM_CNS)
    return 0;

  return dev_ports[hdev][hport];
}

int
port_is_phyless (struct port *port) {
  switch (port->type) {
    case PTYPE_COPPER_PHYLESS:
    case PTYPE_FIBER_PHYLESS:
      return 1;
    default:
      return 0;
  }
}

static void
port_set_vid (struct port *port)
{
  vid_t vid = 0; /* Make the compiler happy. */

  switch (port->mode) {
  case PM_ACCESS:   vid = port->access_vid;   break;
  case PM_TRUNK:    vid = port->native_vid;   break;
  case PM_CUSTOMER: vid = port->customer_vid; break;
  }
  CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
}

enum status
port_update_qos_trust (const struct port *port)
{
  GT_STATUS rc;
  CPSS_QOS_PORT_TRUST_MODE_ENT trust;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (mls_qos_trust) {
    if (port->trust_cos && port->trust_dscp)
      trust = CPSS_QOS_PORT_TRUST_L2_L3_E;
    else if (port->trust_cos)
      trust = CPSS_QOS_PORT_TRUST_L2_E;
    else if (port->trust_dscp)
      trust = CPSS_QOS_PORT_TRUST_L3_E;
    else
      trust = CPSS_QOS_PORT_NO_TRUST_E;
  } else
    trust = CPSS_QOS_PORT_NO_TRUST_E;

  rc = CRP (cpssDxChCosPortQosTrustModeSet (port->ldev, port->lport, trust));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}

static void
port_setup_stats (GT_U8 ldev, GT_U8 lport)
{
  CPSS_PORT_MAC_COUNTER_SET_STC stats;

  CRP (cpssDxChPortMacCountersEnable (ldev, lport, GT_TRUE));
  CRP (cpssDxChPortMacCountersClearOnReadSet (ldev, lport, GT_TRUE));
  CRP (cpssDxChPortMacCountersOnPortGet (ldev, lport, &stats));
  CRP (cpssDxChPortMacCountersClearOnReadSet (ldev, lport, GT_FALSE));
  CRP (cpssDxChPortMacCountersEnable (ldev, lport, GT_TRUE));
}

int
port_init (void)
{
  DECLARE_PORT_MAP (pmap);
  int i, d, p;

  memset (dev_ports, 0, sizeof (dev_ports));

  ports = calloc (NPORTS, sizeof (struct port));
  assert (ports);

  memset (all_ports_bmp, 0, sizeof (all_ports_bmp));
  memset (nst_ports_bmp, 0, sizeof (nst_ports_bmp));
  for_each_dev (d) {
    DEBUG ("add dev %d ic ports\r\n", d);
    for (p = 0; p < dev_info[d].n_ic_ports; p++) {
      CPSS_PORTS_BMP_PORT_SET_MAC (&all_ports_bmp[d], dev_info[d].ic_ports[p]);
      CPSS_PORTS_BMP_PORT_SET_MAC (&nst_ports_bmp[d], dev_info[d].ic_ports[p]);
    }
  }

  for (i = 0; i < NPORTS; i++) {
    ports[i].id = i + 1;

#if defined (VARIANT_ARLAN_3448PGE) || defined (VARIANT_ARLAN_3448GE)
    ports[i].type = (ports[i].id > 48) ? PTYPE_FIBER : PTYPE_COPPER;
#elif defined (VARIANT_ARLAN_3050PGE) || defined (VARIANT_ARLAN_3050GE)
    if (ports[i].id == 49 || ports[i].id == 50) {
      ports[i].type = PTYPE_FIBER;
    } else {
      ports[i].type = PTYPE_COPPER;
    }
#elif defined (VARIANT_FE) /* also implying PFE and SM-12F (see variant.h) */
    ports[i].type = (ports[i].id < 25) ? PTYPE_COPPER : PTYPE_COMBO;
#elif defined (VARIANT_ARLAN_3226PGE) || defined (VARIANT_ARLAN_3226GE)
    ports[i].type = ((ports[i].id > 24) && ((ports[i].id < 27)))
                    ? PTYPE_FIBER : PTYPE_COPPER;
#else /* GE-C[-S], GE-U, GE-F[-S] */
    switch (env_hw_subtype()) {
      case HWST_ARLAN_3424GE_F :
      case HWST_ARLAN_3424GE_F_S :
        ports[i].type = PTYPE_FIBER;
        break;
      case HWST_ARLAN_3424GE_U :
        ports[i].type = (ports[i].id > 12) ? PTYPE_FIBER : PTYPE_COPPER;
        break;
      default :
        if (ports[i].id < 23) {
          ports[i].type = PTYPE_COPPER;
        }
        else if (ports[i].id == 23 || ports[i].id == 24) {
          ports[i].type = PTYPE_COMBO;
        }
        else {
          ports[i].type = PTYPE_FIBER;
        }
    }
#endif /* VARIANT_* */

    if (IS_PORT_PHYLESS (i)) {
      assert(ports[i].type != PTYPE_COMBO);
      if (ports[i].type == PTYPE_COPPER)
        ports[i].type = PTYPE_COPPER_PHYLESS;
      else
        ports[i].type = PTYPE_FIBER_PHYLESS;
    }

    ports[i].ldev = pmap[i].dev;
    ports[i].lport = pmap[i].port;
    ports[i].mode = PM_ACCESS;
    ports[i].access_vid = 1;
    ports[i].native_vid = 1;
    ports[i].voice_vid = 0;
    ports[i].trust_cos = 0;
    ports[i].trust_dscp = 0;
    ports[i].c_speed = PORT_SPEED_AUTO;
    ports[i].c_speed_auto = 1;
    ports[i].c_duplex = PORT_DUPLEX_AUTO;
    ports[i].c_shutdown = 0;
    ports[i].c_protected = 0;
    ports[i].c_prot_comm = 0;
    ports[i].tdr_test_in_progress = 0;
    ports[i].stack_role = PORT_STACK_ROLE (i);
    if (ports[i].stack_role == PSR_NONE)
      CPSS_PORTS_BMP_PORT_SET_MAC (&nst_ports_bmp[pmap[i].dev], pmap[i].port);
    CPSS_PORTS_BMP_PORT_SET_MAC (&all_ports_bmp[pmap[i].dev], pmap[i].port);
    if (IS_FE_PORT (i)) {
      ports[i].max_speed = PORT_SPEED_100;
      ports[i].set_speed = port_set_speed_fe;
      ports[i].set_duplex = port_set_duplex_fe;
      ports[i].update_sd = port_update_sd_fe;
      ports[i].shutdown = port_shutdown_fe;
      ports[i].set_mdix_auto = port_set_mdix_auto_fe;
      ports[i].setup = port_setup_fe;
    } else if (IS_GE_PORT (i)) {
      ports[i].max_speed = PORT_SPEED_1000;
      ports[i].set_speed = port_set_speed_ge;
      ports[i].set_duplex = port_set_duplex_ge;
      ports[i].update_sd = port_update_sd_ge;
      ports[i].shutdown = port_shutdown_ge;
      ports[i].set_mdix_auto = port_set_mdix_auto_ge;
      ports[i].setup = port_setup_ge;
    } else if (IS_XG_PORT (i)) {
      ports[i].max_speed = PORT_SPEED_10000;
      ports[i].set_speed = port_set_speed_xg;
      ports[i].set_duplex = port_set_duplex_xg;
      ports[i].update_sd = port_update_sd_xg;
      ports[i].shutdown = port_shutdown_xg;
      ports[i].set_mdix_auto = port_set_mdix_auto_xg;
      ports[i].setup = port_setup_xg;
    } else {
      /* We should never get here. */
      EMERG ("Port specification error at %d, aborting", i);
      abort ();
    }
    dev_ports[phys_dev (ports[i].ldev)][ports[i].lport] = ports[i].id;

    /* Port Security. */
    pthread_mutex_init (&ports[i].psec_lock, NULL);
    ports[i].psec_enabled = 0;
    ports[i].psec_action = PSECA_RESTRICT;
    ports[i].psec_trap_interval = 30;
    ports[i].psec_mode = PSECM_LOCK;
    ports[i].psec_max_addrs = 0;
    ports[i].psec_naddrs = 0;
    /* END: Port Security. */

    switch (ports[i].stack_role) {
    case PSR_PRIMARY:
      if (stack_pri_port) {
        /* We should never get here. */
        EMERG ("Port specification error at %d, aborting", i);
        abort ();
      }
      stack_pri_port = &ports[i];
      break;
    case PSR_SECONDARY:
      if (stack_sec_port) {
        /* We should never get here. */
        EMERG ("Port specification error at %d, aborting", i);
        abort ();
      }
      stack_sec_port = &ports[i];
      break;
    default:
      break;
    }
  }

  nports = NPORTS;

  return 0;
}

static inline void
phy_lock (void)
{
  pthread_mutex_lock (&port_phy_lock);
}

static inline void
phy_unlock (void)
{
  pthread_mutex_unlock (&port_phy_lock);
}

#ifdef VARIANT_FE
void *not_sock;

static void
notify_port_state (port_id_t pid, const CPSS_PORT_ATTRIBUTES_STC *attrs) {
  zmsg_t *msg = zmsg_new ();
  assert (msg);
  zmsg_addmem (msg, &pid, sizeof (pid));
  zmsg_addmem (msg, attrs, sizeof (*attrs));
  zmsg_send (&msg, not_sock);
}

/* CPSS cpssDxChPortAttributesOnPortGet functionemulation for sofware polling
 * mode for 88e308[23], 88e1340, 88e1322 PHY's */
static enum status
phy_get_attibutes(GT_U8 ldev, GT_U8 lport, port_id_t pid, CPSS_PORT_ATTRIBUTES_STC *attrs, int link_up, int fiber_used) {
  GT_U16 reg;

  phy_lock();
  if (fiber_used) {
    GT_U16 preg;
    CRP (cpssDxChPhyPortSmiRegisterRead
         (ldev, lport, 0x16, &preg));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (ldev, lport, 0x16, 0x01));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (ldev, lport, 0x11, &reg));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (ldev, lport, 0x16, preg));
  }else {
    CRP (cpssDxChPhyPortSmiRegisterRead
         (ldev, lport, 0x11, &reg));
  }
  phy_unlock();

  attrs->portLinkUp = (link_up) ? GT_TRUE : GT_FALSE;
  if (! (reg & 0x0800) && attrs->portLinkUp)
    DEBUG("port %d speed/duplexity are not resolved\n", pid);

  attrs->portDuplexity = (reg & 0x2000) ? CPSS_PORT_FULL_DUPLEX_E : CPSS_PORT_HALF_DUPLEX_E;

  if (IS_GE_PORT(pid - 1)) {
    switch ( (reg & 0xC000)) {
      case 0x8000:
        attrs->portSpeed = CPSS_PORT_SPEED_1000_E;
        break;
      case 0x4000:
        attrs->portSpeed = CPSS_PORT_SPEED_100_E;
        break;
      case 0x0000:
        attrs->portSpeed = CPSS_PORT_SPEED_10_E;
        break;
      case 0xC000:
        attrs->portSpeed = CPSS_PORT_SPEED_10_E;
        return ST_HEX;
    }
  }
  else
    if (IS_FE_PORT(pid - 1)) {
      if (reg & 0x4000)
        attrs->portSpeed = CPSS_PORT_SPEED_100_E;
      else
        attrs->portSpeed = CPSS_PORT_SPEED_10_E;
    }
    else {
      attrs->portSpeed = CPSS_PORT_SPEED_10_E;
      return ST_HEX;
    }

  return ST_OK;
}

enum status
phy_handle_link_change (struct port *port, int link_up, int fiber_used)
{
  GT_STATUS rc;

  CPSS_PORT_ATTRIBUTES_STC attrs;
  rc = phy_get_attibutes(port->ldev, port->lport, port->id, &attrs, link_up, fiber_used);
  if (rc != ST_OK) {
    return rc;
  }
  notify_port_state (port->id, &attrs);

  port_lock ();

  if (attrs.portLinkUp    != port->state.attrs.portLinkUp ||
      attrs.portSpeed     != port->state.attrs.portSpeed  ||
      attrs.portDuplexity != port->state.attrs.portDuplexity) {
    port->state.attrs = attrs;
//#define DEBUG_STATE //TODO remove
#ifdef DEBUG_STATE
    if (attrs.portLinkUp)
      osPrintSync ("port %2d link up at %s, %s\n", port->id,
                   SHOW (CPSS_PORT_SPEED_ENT, attrs.portSpeed),
                   SHOW (CPSS_PORT_DUPLEX_ENT, attrs.portDuplexity));
    else
      osPrintSync ("port %2d link down\n", port->id);
#endif /* DEBUG_STATE */
  }

  port_unlock ();

  return ST_OK;
}

static volatile int phy_thread_started = 0;

#define PHY_POLLING_INTERVAL 100  /* millisecs */

enum phy_status {
  PHS_NOLINK,
  PHS_COPPER,
  PHS_FIBER
};

static void*
phy_polling_thread(void *numports) {
  GT_STATUS rc;
  GT_U16 reg;
  struct timespec ts = {0, PHY_POLLING_INTERVAL * 1000000};
  enum phy_status port_status[NPORTS+1];
  unsigned np = *(unsigned *)numports;

  unsigned i;
  for (i = 1; i <= np; i++) {
    port_status[i] = PHS_NOLINK;
  }

  prctl(PR_SET_NAME, "PHY-poller", 0, 0, 0);

  not_sock = zsocket_new (zcontext, ZMQ_PUSH);
  assert (not_sock);
  zsocket_connect (not_sock, NOTIFY_QUEUE_EP);

  DEBUG ("PHY polling thread startup done\r\n");
  phy_thread_started = 1;

  while (1) {
    nanosleep(&ts, NULL);
    phy_lock();
    for (i = 1; i <= np; i++) {
      struct port* port = port_ptr (i);
      int fiber_used = 0;
      int link_up = 0;

      switch (port_status[i]) {
        case PHS_NOLINK:
          if (IS_GE_PORT(i-1)) {
            GT_U16 preg;
            rc = CRP (cpssDxChPhyPortSmiRegisterRead
                      (port->ldev, port->lport, 0x16, &preg));
            rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                      (port->ldev, port->lport, 0x16, 0x01));
            rc = CRP (cpssDxChPhyPortSmiRegisterRead
                      (port->ldev, port->lport, 0x01, &reg));
            rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                      (port->ldev, port->lport, 0x16, preg));
            link_up = reg & 4;
            if (link_up)
              fiber_used = 1;
          }
          if (!link_up) {
            rc = CRP (cpssDxChPhyPortSmiRegisterRead
                      (port->ldev, port->lport, 0x01, &reg));
            link_up = reg & 4;
          }
          break;
        case PHS_COPPER:
          if (IS_GE_PORT(i-1)) {
            GT_U16 preg;
            rc = CRP (cpssDxChPhyPortSmiRegisterRead
                      (port->ldev, port->lport, 0x16, &preg));
            rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                      (port->ldev, port->lport, 0x16, 0x01));
            rc = CRP (cpssDxChPhyPortSmiRegisterRead
                      (port->ldev, port->lport, 0x01, &reg));
            rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                      (port->ldev, port->lport, 0x16, preg));
            link_up = reg & 4;
            if (link_up)
              {
              fiber_used = 1;
              }
          }
          if (!link_up) {
            rc = CRP (cpssDxChPhyPortSmiRegisterRead
                      (port->ldev, port->lport, 0x01, &reg));
            link_up = reg & 4;
          }
          break;
        case PHS_FIBER:
          if (IS_GE_PORT(i-1)) {
            GT_U16 preg;
            rc = CRP (cpssDxChPhyPortSmiRegisterRead
                      (port->ldev, port->lport, 0x16, &preg));
            rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                      (port->ldev, port->lport, 0x16, 0x01));
            rc = CRP (cpssDxChPhyPortSmiRegisterRead
                      (port->ldev, port->lport, 0x01, &reg));
            rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                      (port->ldev, port->lport, 0x16, preg));
            link_up = reg & 4;
            if (link_up)
              fiber_used = 1;
          } else
            assert(0);
          break;
      }

      if ((port_status[i] && !link_up) || ((port_status[i] == PHS_COPPER) && link_up && fiber_used)) {
        CRP(cpssDxChPortForceLinkPassEnableSet
            (port->ldev, port->lport, GT_FALSE));
  /* workaround for activated speed LED without active link status in no
     autoneg port configuration, disable LED */
        if (IS_FE_PORT(i-1)) {
          GT_U16 reg;
          CRP (cpssDxChPhyPortSmiRegisterRead
               (port->ldev, port->lport, 0x19, &reg));
          reg |= 0x0002;
          reg &= ~0x0001;
          CRP (cpssDxChPhyPortSmiRegisterWrite
               (port->ldev, port->lport, 0x19, reg));
        }
        phy_unlock();
        phy_handle_link_change(port, 0, (port_status[i] == PHS_COPPER)? 0 : 1 );
        phy_lock();
        port_status[i] = PHS_NOLINK;
      } else
      if (!port_status[i] && link_up) {
        CRP(cpssDxChPortForceLinkPassEnableSet
            (port->ldev, port->lport, GT_TRUE));
        if (IS_FE_PORT(i-1)) {
          GT_U16 reg;
          CRP (cpssDxChPhyPortSmiRegisterRead
               (port->ldev, port->lport, 0x19, &reg));
  /* workaround for activated speed LED without active link status in no
     autoneg port configuration, enable normal LED operation*/
          reg &= ~0x0003;
          CRP (cpssDxChPhyPortSmiRegisterWrite
               (port->ldev, port->lport, 0x19, reg));
        }
        phy_unlock();
        phy_handle_link_change(port, 1, fiber_used);
        phy_lock();
        port_status[i] = (fiber_used)? PHS_FIBER : PHS_COPPER;
      }
    }
    phy_unlock();
  }
  return NULL;
}
#endif /* VARIANT_FE */

static void
port_set_iso_bmp (struct port *port)
{
  if (port->iso_bmp_changed) {
    CPSS_INTERFACE_INFO_STC iface = {
      .type = CPSS_INTERFACE_PORT_E,
      .devPort = {
        .devNum = phys_dev (port->ldev),
        .portNum = port->lport
      }
    };

    CRP (cpssDxChNstPortIsolationTableEntrySet
         (port->ldev,
          CPSS_DXCH_NST_PORT_ISOLATION_TRAFFIC_TYPE_L2_E,
          iface,
          GT_TRUE,
          &port->iso_bmp));
    CRP (cpssDxChNstPortIsolationTableEntrySet
         (port->ldev,
          CPSS_DXCH_NST_PORT_ISOLATION_TRAFFIC_TYPE_L3_E,
          iface,
          GT_TRUE,
          &port->iso_bmp));
    port->iso_bmp_changed = 0;
  }
}

static int
__port_set_comm (struct port *t, uint8_t comm)
{
  int i;

  if (t->c_prot_comm == comm)
    return 0;

  if (t->c_protected) {
    for (i = 0; i < nports; i++) {
      struct port *o = &ports[i];

      if (!o->c_protected)
        continue;

      if (o->id == t->id)
        continue;

      if (t->c_prot_comm && t->c_prot_comm == o->c_prot_comm) {
        CPSS_PORTS_BMP_PORT_CLEAR_MAC (&t->iso_bmp, o->lport);
        t->iso_bmp_changed = 1;
        CPSS_PORTS_BMP_PORT_CLEAR_MAC (&o->iso_bmp, t->lport);
        o->iso_bmp_changed = 1;
      }

      if (comm && comm == o->c_prot_comm) {
        CPSS_PORTS_BMP_PORT_SET_MAC (&t->iso_bmp, o->lport);
        t->iso_bmp_changed = 1;
        CPSS_PORTS_BMP_PORT_SET_MAC (&o->iso_bmp, t->lport);
        o->iso_bmp_changed = 1;
      }
    }
  }

  t->c_prot_comm = comm;
  return t->c_protected;
}

static int
__port_set_prot (struct port *t, int prot)
{
  int i;

  if (t->c_protected == prot)
    return 0;

  for (i = 0; i < nports; i++) {
    struct port *o = &ports[i];

    if (o->id == t->id)
      continue;

    if (prot) {
      if (o->c_protected) {
        if (t->c_prot_comm && o->c_prot_comm == t->c_prot_comm) {
          CPSS_PORTS_BMP_PORT_SET_MAC (&t->iso_bmp, o->lport);
          t->iso_bmp_changed = 1;
          CPSS_PORTS_BMP_PORT_SET_MAC (&o->iso_bmp, t->lport);
          o->iso_bmp_changed = 1;
        } else {
          CPSS_PORTS_BMP_PORT_CLEAR_MAC (&t->iso_bmp, o->lport);
          t->iso_bmp_changed = 1;
          CPSS_PORTS_BMP_PORT_CLEAR_MAC (&o->iso_bmp, t->lport);
          o->iso_bmp_changed = 1;
        }
      } else {
        CPSS_PORTS_BMP_PORT_SET_MAC (&t->iso_bmp, o->lport);
        t->iso_bmp_changed = 1;
        CPSS_PORTS_BMP_PORT_SET_MAC (&o->iso_bmp, t->lport);
        o->iso_bmp_changed = 1;
      }
    } else {
      CPSS_PORTS_BMP_PORT_SET_MAC (&t->iso_bmp, o->lport);
      t->iso_bmp_changed = 1;
      CPSS_PORTS_BMP_PORT_SET_MAC (&o->iso_bmp, t->lport);
      o->iso_bmp_changed = 1;
    }
  }

  t->c_protected = prot;
  return 1;
}

static void
port_setup_stack (struct port *port)
{
  port->setup (port);
  CRP (cpssDxChCscdPortTypeSet
       (port->ldev, port->lport,
        CPSS_CSCD_PORT_DSA_MODE_EXTEND_E));
  CRP (cpssDxChPortMruSet (port->ldev, port->lport, 12000));
  CRP (cpssDxChCscdQosPortTcRemapEnableSet
       (port->ldev, port->lport, GT_TRUE));
  CRP (cpssDxChTxPortShapersDisable (port->ldev, port->lport));

  CRP (cpssDxChPortTxBindPortToDpSet
       (port->ldev, port->lport, CPSS_PORT_TX_DROP_PROFILE_3_E));
  CRP (cpssDxChPortTxBindPortToSchedulerProfileSet
       (port->ldev, port->lport, CPSS_PORT_TX_SCHEDULER_PROFILE_2_E));

  CRP (cpssDxChCosTrustDsaTagQosModeSet
       (port->ldev, port->lport, GT_TRUE));
}

static void
setup_tc_remap (void)
{
  CPSS_DXCH_CSCD_QOS_TC_REMAP_STC tr = {
    .toCpuLocalTc   = 7,
    .toCpuStackTc   = 7,
    .fromCpuLocalTc = 7,
    .fromCpuStackTc = 7
  };
  int i, d;

  for_each_dev (d) {
    for (i = 0; i < 7; i++) {
      tr.forwardLocalTc    = i;
      tr.forwardStackTc    = i;
      tr.toAnalyzerLocalTc = i;
      tr.toAnalyzerStackTc = i;
      CRP (cpssDxChCscdQosTcRemapTableSet (d, i, &tr));
    }
    CRP (cpssDxChCscdQosTcRemapTableSet (d, 7, &tr));
  }
}

static void
__port_disable (uint8_t d, uint8_t p)
{
  CRP (cpssDxChPortEnableSet (d, p, GT_FALSE));
  CRP (cpssDxChBrgFdbNaToCpuPerPortSet (d, p, GT_FALSE));
}

void
port_disable_all (void)
{
  int d, i;

  for_each_dev (d) {
    for (i = 0; i < PRV_CPSS_PP_MAC (d)->numOfPorts; i++) {
      if (PRV_CPSS_PP_MAC (d)->phyPortInfoArray[i].portType !=
          PRV_CPSS_PORT_NOT_EXISTS_E) {
        __port_disable (d, i);
      }
    }
    __port_disable (d, CPSS_CPU_PORT_NUM_CNS);
  }
}

enum status
port_start (void)
{
/*   GT_U32 rate = 5000; */
  int i, d;

#if defined (VARIANT_FE)
//  CRP (cpssDxChPhyAutoPollNumOfPortsSet
//       (0, CPSS_DXCH_PHY_SMI_AUTO_POLL_NUM_OF_PORTS_16_E,
//        CPSS_DXCH_PHY_SMI_AUTO_POLL_NUM_OF_PORTS_8_E));
#endif /* VARIANT_FE */

#if defined (VARIANT_SM_12F)
  /* Shut down unused PHYs. */
  for (i = 8; i < 12; i++) {
    __port_setup_fe (0, i);
    __port_shutdown_fe (0, i, 1);
  }
#endif /* VARIANT_SM_12F */

  for_each_dev (d) {
    CRP (cpssDxChNstPortIsolationEnableSet (d, GT_TRUE));
    CRP (cpssDxChBrgVlanEgressFilteringEnable (d, GT_TRUE));
    CRP (cpssDxChBrgRoutedSpanEgressFilteringEnable (d, GT_TRUE));
    CRP (cpssDxChBrgSrcIdGlobalSrcIdAssignModeSet
         (d, CPSS_BRG_SRC_ID_ASSIGN_MODE_PORT_DEFAULT_E));

    CRP (cpssDxChBrgSrcIdGroupEntrySet
         (d, stack_id, GT_TRUE, &all_ports_bmp[d]));
  }

  setup_tc_remap ();

  for (i = 0; i < nports; i++) {
    struct port *port = &ports[i];

    CRP (cpssDxChBrgSrcIdPortUcastEgressFilterSet
         (port->ldev, port->lport, GT_TRUE));
    CRP (cpssDxChBrgSrcIdPortDefaultSrcIdSet
         (port->ldev, port->lport, stack_id));

    pcl_port_setup (port->id);

    if (is_stack_port (port)) {
      port_setup_stack (port);
      continue;
    }

    CRP (cpssDxChCscdPortTypeSet
         (port->ldev, port->lport, CPSS_CSCD_PORT_NETWORK_E));

    port->setup (port);

    CRP (cpssDxChBrgFdbPortLearnStatusSet
         (port->ldev, port->lport, GT_FALSE, CPSS_LOCK_FRWRD_E));
    CRP (cpssDxChBrgSecurBreachNaPerPortSet
         (port->ldev, port->lport, GT_FALSE));
    CRP (cpssDxChBrgFdbNaStormPreventSet (port->ldev, port->lport, GT_TRUE));
    CRP (cpssDxChBrgFdbNaToCpuPerPortSet (port->ldev, port->lport, GT_TRUE));

    CRP (cpssDxChBrgVlanPortIngFltEnable (port->ldev, port->lport, GT_TRUE));
    port_set_vid (port);
    port_update_qos_trust (port);
    port_setup_stats (port->ldev, port->lport);

    CRP (cpssDxChBrgGenPortIeeeReservedMcastProfileIndexSet
         (port->ldev, port->lport, 0));
    CRP (cpssDxChIpPortRoutingEnable
         (port->ldev, port->lport,
          CPSS_IP_UNICAST_E, CPSS_IP_PROTOCOL_IPV4_E,
          GT_TRUE));
    CRP (cpssDxChIpPortRoutingEnable
         (port->ldev, port->lport,
          CPSS_IP_MULTICAST_E, CPSS_IP_PROTOCOL_IPV4_E,
          GT_TRUE));

    /* QoS initial setup. */
    CRP (cpssDxChCosPortReMapDSCPSet (port->ldev, port->lport, GT_FALSE));
    CRP (cpssDxChPortDefaultUPSet (port->ldev, port->lport, 0));
    CPSS_QOS_ENTRY_STC qe = {
      .qosProfileId = 0,
      .assignPrecedence = CPSS_PACKET_ATTRIBUTE_ASSIGN_PRECEDENCE_HARD_E,
      .enableModifyUp = CPSS_PACKET_ATTRIBUTE_MODIFY_DISABLE_E,
      .enableModifyDscp = CPSS_PACKET_ATTRIBUTE_MODIFY_DISABLE_E
    };
    CRP (cpssDxChCosPortQosConfigSet (port->ldev, port->lport, &qe));
    CRP (cpssDxChCosL2TrustModeVlanTagSelectSet
         (port->ldev, port->lport, CPSS_VLAN_TAG0_E));

    port->iso_bmp = all_ports_bmp[port->ldev];
    port->iso_bmp_changed = 1;
    port_set_iso_bmp (port);

    CRP (cpssDxChBrgVlanPortEgressTpidSet
         (port->ldev, port->lport,
          CPSS_VLAN_ETHERTYPE0_E, VLAN_TPID_IDX));
    CRP (cpssDxChBrgVlanPortEgressTpidSet
         (port->ldev, port->lport,
          CPSS_VLAN_ETHERTYPE1_E, VLAN_TPID_IDX));

#ifdef PRESTERAMGR_FUTURE_LION
    CRP (cpssDxChPortTxShaperModeSet
         (ports->ldev, port->lport,
          CPSS_PORT_TX_DROP_SHAPER_BYTE_MODE_E));
#endif /* PRESTERAMGR_FUTURE_LION */

    pcl_port_setup (port->id);
    pcl_enable_port (port->id, 1);
    /* pcl_enable_lbd_trap (port->id, 1); */
  }

  port_set_mru (1526);

  port_setup_stats (CPU_DEV, CPSS_CPU_PORT_NUM_CNS);
  CRP (cpssDxChCscdPortTypeSet
       (CPU_DEV, CPSS_CPU_PORT_NUM_CNS,
        CPSS_CSCD_PORT_DSA_MODE_EXTEND_E));
  CRP (cpssDxChPortMruSet (CPU_DEV, CPSS_CPU_PORT_NUM_CNS, 12000));
  CRP (cpssDxChBrgFdbNaToCpuPerPortSet
       (CPU_DEV, CPSS_CPU_PORT_NUM_CNS, GT_FALSE));

  for_each_dev (d) {
    int p;

    CRP (cpssDxChBrgPrvEdgeVlanEnable (d, GT_TRUE));

    CRP (cpssDxChPortTxShaperGlobalParamsSet (d, 15, 15, 1));
    CRP (cpssDxChPortTxToCpuShaperModeSet
         (d, CPSS_PORT_TX_DROP_SHAPER_PACKET_MODE_E));
    CRP (cpssDxChPortTxByteCountChangeEnableSet
         (d, CPSS_DXCH_PORT_TX_BC_CHANGE_ENABLE_SHAPER_ONLY_E));

    for (p = 0; p < dev_info[d].n_ic_ports; p++) {
      DEBUG ("*** enable dev %d trunk port %d\r\n", d, dev_info[d].ic_ports[p]);
      CRP (cpssDxChPortEnableSet (d, dev_info[d].ic_ports[p], GT_TRUE));
    }
  }

  for (i = 0; i < nports; i++) {
    struct port *port = &ports[i];

    CRP (cpssDxChBrgStpStateSet
         (port->ldev, port->lport, 0,
          is_stack_port (port) ? CPSS_STP_FRWRD_E : CPSS_STP_BLCK_LSTN_E));
    CRP (cpssDxChPortEnableSet (port->ldev, port->lport, GT_TRUE));
    port->shutdown (port, 0);
    port->update_sd (port);
  };

/*  CRP (cpssDxChPortTxShaperProfileSet
       (CPU_DEV, CPSS_CPU_PORT_NUM_CNS, 1, &rate));
  CRP (cpssDxChPortTxShaperEnableSet
       (CPU_DEV, CPSS_CPU_PORT_NUM_CNS, GT_TRUE)); */

  for_each_dev (d)
    CRP (cpssDxChNetIfFromCpuDpSet (d, CPSS_DP_GREEN_E));

  /* CRP (cpssDxChPortTxToCpuShaperModeSet */
  /*      (CPU_DEV, CPSS_PORT_TX_DROP_SHAPER_PACKET_MODE_E)); */
  /* CRP (cpssDxChTxPortShapersDisable (CPU_DEV, CPSS_CPU_PORT_NUM_CNS)); */

  CRP (cpssDxChCscdQosPortTcRemapEnableSet
       (CPU_DEV, CPSS_CPU_PORT_NUM_CNS, GT_FALSE));
  CRP (cpssDxChPortTxBindPortToDpSet
       (CPU_DEV, CPSS_CPU_PORT_NUM_CNS, CPSS_PORT_TX_DROP_PROFILE_1_E));
  CRP (cpssDxChPortTxBindPortToSchedulerProfileSet
       (CPU_DEV, CPSS_CPU_PORT_NUM_CNS, CPSS_PORT_TX_SCHEDULER_PROFILE_1_E));

  CRP (cpssDxChPortEnableSet (CPU_DEV, CPSS_CPU_PORT_NUM_CNS, GT_TRUE));

#ifdef VARIANT_FE
  /* start phy polling */
  pthread_t tid;
  pthread_create (&tid, NULL, phy_polling_thread, &nports);

  DEBUG ("waiting for PHY polling thread startup\r\n");
  unsigned n = 0;
  while (!phy_thread_started) {
    n++;
    usleep (10000);
  }
  DEBUG ("PHY polling thread startup finished after %u iteractions\r\n", n);
#endif

  return ST_OK;
}

#if defined (VARIANT_FE)
static GT_STATUS
port_set_sgmii_mode (const struct port *port)
{
  CRPR (cpssDxChPortInterfaceModeSet
        (port->ldev, port->lport, CPSS_PORT_INTERFACE_MODE_SGMII_E));
  CRPR (cpssDxChPortSpeedSet
        (port->ldev, port->lport, CPSS_PORT_SPEED_1000_E));
  CRPR (cpssDxChPortSerdesPowerStatusSet
        (port->ldev, port->lport, CPSS_PORT_DIRECTION_BOTH_E, 0x01, GT_TRUE));

  return GT_OK;
}
#endif /* VARIANT_FE */

int
port_exists (GT_U8 dev, GT_U8 port)
{
  return CPSS_PORTS_BMP_IS_PORT_SET_MAC
    (&(PRV_CPSS_PP_MAC (dev)->existingPorts), port);
}

enum status
port_handle_link_change (GT_U8 ldev, GT_U8 lport, port_id_t *pid, CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  struct port *port;
  GT_STATUS rc;

  *pid = port_id (phys_dev (ldev), lport);
  port = port_ptr (*pid);
  if (!port)
    return ST_DOES_NOT_EXIST;

  rc = CRP (cpssDxChPortAttributesOnPortGet (port->ldev, port->lport, attrs));
  if (rc != GT_OK)
    return ST_HEX;

  port_lock ();

  if (attrs->portLinkUp    != port->state.attrs.portLinkUp ||
      attrs->portSpeed     != port->state.attrs.portSpeed  ||
      attrs->portDuplexity != port->state.attrs.portDuplexity) {
    port->state.attrs = *attrs;

//#define DEBUG_STATE //TODO remove
#ifdef DEBUG_STATE
    if (attrs->portLinkUp)
      osPrintSync ("port %2d link up at %s, %s\n", port->id,
                   SHOW (CPSS_PORT_SPEED_ENT, attrs->portSpeed),
                   SHOW (CPSS_PORT_DUPLEX_ENT, attrs->portDuplexity));
    else
      osPrintSync ("port %2d link down\n", port->id);
#endif /* DEBUG_STATE */
  }

  port_unlock ();

  return ST_OK;
}

enum status
port_get_state (port_id_t pid, struct port_link_state *state)
{
  enum status result = ST_BAD_VALUE;
  struct port *port = port_ptr (pid);

  port_lock ();

  if (!port)
    goto out;

  data_encode_port_state (state, &port->state.attrs);
  result = ST_OK;

 out:
  port_unlock ();
  return result;
}

enum status
port_get_type (port_id_t pid, port_type_t *ptype)
{
  struct port *port = port_ptr (pid);
  if (!port) {
    return ST_BAD_VALUE;
  }

  *ptype = port->type;
  return ST_OK;
}

enum status
port_set_stp_state (port_id_t pid, stp_id_t stp_id,
                    int all, enum port_stp_state state)
{
  CPSS_STP_STATE_ENT cs;
  enum status result;
  struct port *port;

  if (!(port = port_ptr (pid)) || stp_id > 255)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;


  result = data_decode_stp_state (&cs, state);
  if (result != ST_OK)
    return result;

  if (all) {
    stp_id_t stg;
    /* FIXME: suboptimal code. */
    for (stg = 0; stg < 256; stg++)
      if (stg_is_active (stg)) {
        stg_state[pid - 1][stg] = state;
        CRP (cpssDxChBrgStpStateSet (port->ldev, port->lport, stg, cs));
    }
  } else {
      stg_state[pid - 1][stp_id] = state;
      CRP (cpssDxChBrgStpStateSet (port->ldev, port->lport, stp_id, cs));
  }

  return ST_OK;
}


enum status
port_set_access_vid (port_id_t pid, vid_t vid)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc = GT_OK;

  if (!(port && vlan_valid (vid)))
    return ST_BAD_VALUE;

  if (port->voice_vid == vid)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (port->mode == PM_ACCESS) {
    rc = CRP (cpssDxChBrgVlanPortDelete
              (port->ldev,
               port->access_vid,
               port->lport));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChBrgVlanMemberSet
              (port->ldev,
               vid,
               port->lport,
               GT_TRUE,
               GT_FALSE,
               CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
    if (rc != GT_OK)
      goto out;

    cn_port_vid_set (pid, vid);
  }

  port->access_vid = vid;

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
port_set_voice_vid (port_id_t pid, vid_t vid)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc = GT_OK;

  if (!(port && (vid == 0 || vlan_valid (vid))))
    return ST_BAD_VALUE;

  if (port->access_vid == vid)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (port->mode == PM_ACCESS) {
    if (port->voice_vid) {
      rc = CRP (cpssDxChBrgVlanPortDelete
                (port->ldev,
                 port->voice_vid,
                 port->lport));
      if (rc != GT_OK)
        goto out;
    }

    if (vid) {
      rc = CRP (cpssDxChBrgVlanMemberSet
                (port->ldev,
                 vid,
                 port->lport,
                 GT_TRUE,
                 GT_FALSE,
                 CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E));
      if (rc != GT_OK)
        goto out;
    }
  }

  port->voice_vid = vid;

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_update_trunk_vlan (struct port *port, vid_t vid)
{
  CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT cmd;
  GT_BOOL tag, mem;
  GT_STATUS rc;
  int vid_ix = vid - 1;

  if (vlans[vid_ix].state == VS_DELETED)
    return ST_OK;

  mem = GT_TRUE;
  tag = GT_TRUE;
  cmd = CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E;

  if (port->vlan_conf[vid_ix].refc) {
    /* VLAN translation source port. */
    cmd = vlan_xlate_tunnel
      ? CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E
      : CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
  } else if (vid == port->native_vid && !vlan_dot1q_tag_native) {
    /* Trunk native VLAN. */
    tag = GT_FALSE;
    cmd = CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E;
  } else if (!port->vlan_conf[vid_ix].tallow) {
    /* Not a member. */
    mem = GT_FALSE;
  } else if (vlans[vid_ix].vt_refc && vlan_xlate_tunnel) {
    cmd = CPSS_DXCH_BRG_VLAN_PORT_TAG0_CMD_E;
  }

  rc = CRP (cpssDxChBrgVlanMemberSet
            (port->ldev,
             vid,
             port->lport,
             mem,
             tag,
             cmd));

  switch (rc) {
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

void
port_update_trunk_vlan_all_ports (vid_t vid)
{
  int i;

  for (i = 0; i < nports; i++)
    if (ports[i].mode == PM_TRUNK)
      port_update_trunk_vlan (&ports[i], vid);
}

enum status
port_set_native_vid (port_id_t pid, vid_t vid)
{
  struct port *port = port_ptr (pid);
  vid_t old;

  if (!(port && vlan_valid (vid)))
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  old = port->native_vid;
  port->native_vid = vid;

  if (port->mode == PM_TRUNK) {
    port_update_trunk_vlan (port, old);
    port_update_trunk_vlan (port, vid);
    CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
    cn_port_vid_set (pid, vid);
  }

  port->native_vid = vid;

  return ST_OK;
}

enum status
port_set_customer_vid (port_id_t pid, vid_t vid)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc = GT_OK;

  if (!(port && vlan_valid (vid)))
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (port->mode == PM_CUSTOMER) {
    rc = CRP (cpssDxChBrgVlanPortDelete
              (port->ldev,
               port->customer_vid,
               port->lport));
    ON_GT_ERROR (rc) goto out;

    rc = CRP (cpssDxChBrgVlanMemberSet
              (port->ldev,
               vid,
               port->lport,
               GT_TRUE,
               GT_TRUE,
               CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E));
    ON_GT_ERROR (rc) goto out;

    rc = CRP (cpssDxChBrgVlanPortVidSet (port->ldev, port->lport, vid));
    ON_GT_ERROR (rc) goto out;

    cn_port_vid_set (pid, vid);
  }

  port->customer_vid = vid;

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_vlan_bulk_op (struct port *port,
                   vid_t vid,
                   GT_BOOL vid_tag,
                   CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT vid_tag_cmd,
                   GT_BOOL force_pvid,
                   int tpid_idx,
                   GT_BOOL rest_member,
                   GT_BOOL rest_tag,
                   CPSS_DXCH_BRG_VLAN_PORT_TAG_CMD_ENT rest_tag_cmd)
{
  GT_STATUS rc;
  int i;

  for (i = 0; i < NVLANS; i++) {
    if (vlans[i].state == VS_DELETED || vid == i + 1)
      continue;

    rc = CRP (cpssDxChBrgVlanMemberSet
              (port->ldev,
               i + 1,
               port->lport,
               rest_member,
               rest_tag,
               rest_tag_cmd));
    ON_GT_ERROR (rc) goto err;
  }

  rc = CRP (cpssDxChBrgVlanMemberSet
            (port->ldev,
             vid,
             port->lport,
             GT_TRUE,
             vid_tag,
             vid_tag_cmd));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanPortVidSet
            (port->ldev,
             port->lport,
             vid));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanForcePvidEnable
            (port->ldev,
             port->lport,
             force_pvid));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanPortIngressTpidSet
            (port->ldev, port->lport,
             CPSS_VLAN_ETHERTYPE0_E, 1 << tpid_idx));
  ON_GT_ERROR (rc) goto err;
  rc = CRP (cpssDxChBrgVlanPortIngressTpidSet
            (port->ldev, port->lport,
             CPSS_VLAN_ETHERTYPE1_E, 1 << tpid_idx));
  ON_GT_ERROR (rc) goto err;

  cn_port_vid_set (port->id, vid);

  return ST_OK;

 err:
  switch (rc) {
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}


static enum status
port_set_trunk_mode (struct port *port)
{
  GT_STATUS rc;
  int i;

  for (i = 0; i < NVLANS; i++)
    port_update_trunk_vlan (port, i + 1);

  rc = CRP (cpssDxChBrgVlanPortVidSet
            (port->ldev,
             port->lport,
             port->native_vid));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanForcePvidEnable
            (port->ldev,
             port->lport,
             GT_FALSE));
  ON_GT_ERROR (rc) goto err;

  rc = CRP (cpssDxChBrgVlanPortIngressTpidSet
            (port->ldev, port->lport,
             CPSS_VLAN_ETHERTYPE0_E, 1 << VLAN_TPID_IDX));
  ON_GT_ERROR (rc) goto err;
  rc = CRP (cpssDxChBrgVlanPortIngressTpidSet
            (port->ldev, port->lport,
             CPSS_VLAN_ETHERTYPE1_E, 1 << VLAN_TPID_IDX));
  ON_GT_ERROR (rc) goto err;

  cn_port_vid_set (port->id, port->native_vid);

  return ST_OK;

 err:
  switch (rc) {
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_set_access_mode (struct port *port)
{
  port_vlan_bulk_op (port,
                     port->access_vid,
                     GT_FALSE,
                     CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E,
                     GT_FALSE,
                     VLAN_TPID_IDX,
                     GT_FALSE,
                     GT_FALSE,
                     CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E);

  if (port->voice_vid)
    CRP (cpssDxChBrgVlanMemberSet
         (port->ldev,
          port->voice_vid,
          port->lport,
          GT_TRUE,
          GT_FALSE,
          CPSS_DXCH_BRG_VLAN_PORT_OUTER_TAG0_INNER_TAG1_CMD_E));

  return ST_OK;
}

static enum status
port_set_customer_mode (struct port *port)
{
  port_vlan_bulk_op (port,
                     port->customer_vid,
                     GT_TRUE,
                     CPSS_DXCH_BRG_VLAN_PORT_POP_OUTER_TAG_CMD_E,
                     GT_TRUE,
                     FAKE_TPID_IDX,
                     GT_FALSE,
                     GT_FALSE,
                     CPSS_DXCH_BRG_VLAN_PORT_UNTAGGED_CMD_E);

  return ST_OK;
}

enum status
port_set_mode (port_id_t pid, enum port_mode mode)
{
  struct port *port = port_ptr (pid);
  enum status result;

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (port->mode == mode)
    return ST_OK;

  switch (mode) {
  case PM_ACCESS:
    result = port_set_access_mode (port);
    break;
  case PM_TRUNK:
    result = port_set_trunk_mode (port);
    break;
  case PM_CUSTOMER:
    result = port_set_customer_mode (port);
    break;
  default:
    result = ST_BAD_VALUE;
  }

  if (result == ST_OK) {
    pcl_port_enable_vt (pid, mode == PM_TRUNK);
    port->mode = mode;
  }

  return result;
}

enum status
port_shutdown (port_id_t pid, int shutdown)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  shutdown = !!shutdown;

  if (port->c_shutdown == shutdown)
    return ST_OK;

  port->c_shutdown = shutdown;

  return port->shutdown (port, shutdown);
}

enum status
port_block (port_id_t pid, const struct port_block *what)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  switch (what->type) {
  case TT_UNICAST:
    rc = CRP (cpssDxChBrgPortEgrFltUnkEnable
              (port->ldev,
               port->lport,
               !!what->block));
    break;

  case TT_MULTICAST:
    rc = CRP (cpssDxChBrgPortEgrFltUregMcastEnable
              (port->ldev,
               port->lport,
               !!what->block));
    break;

  case TT_BROADCAST:
    rc = CRP (cpssDxChBrgPortEgrFltUregBcEnable
              (port->ldev,
               port->lport,
               !!what->block));
    break;

  default:
    return ST_BAD_VALUE;
  }

  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_update_sd_ge (struct port *port)
{
  GT_STATUS rc;
  GT_U16 reg, reg1;

  if (port->type == PTYPE_FIBER
      || port->type == PTYPE_FIBER_PHYLESS) {
    /* Fiber speed changed handled by sfp-utils */
    return ST_OK;
  }

  phy_lock();
  if (port->c_speed_auto || port->c_duplex == PORT_DUPLEX_AUTO) {
    /* Speed or duplex is AUTO. */

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x04, &reg));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x09, &reg1));
    if (rc != GT_OK)
      goto out;

    reg |= 0x01E0;

    switch (port->c_speed) {
    case PORT_SPEED_AUTO:
      reg1 |= 1 << 9;
      break;
    case PORT_SPEED_10:
      reg &= ~((1 << 7) | (1 << 8));
      reg1 &= ~(1 << 9);
      break;
    case PORT_SPEED_100:
      reg &= ~((1 << 5) | (1 << 6));
      reg1 &= ~(1 << 9);
      break;
    case PORT_SPEED_1000:
      reg &= ~0x01E0;
      reg1 |= 1 << 9;
      break;
    default:
      /* We should never get here. */
      phy_unlock();
      return ST_BAD_VALUE;
    }

    switch (port->c_duplex) {
    case PORT_DUPLEX_FULL:
#ifndef VARIANT_FE
      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_FULL_DUPLEX_E);
#endif
      reg &= ~((1 << 5) | (1 << 7));
      break;
    case PORT_DUPLEX_HALF:
#ifndef VARIANT_FE
      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_HALF_DUPLEX_E);
#endif
      reg &= ~((1 << 6) | (1 << 8));
      break;
    case PORT_DUPLEX_AUTO:
#ifndef VARIANT_FE
      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_FULL_DUPLEX_E);
      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_TRUE);
#endif
      break;
    default:
      break;
    }

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x04, reg));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x09, reg1));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x00, &reg));
    if (rc != GT_OK)
      goto out;

    reg |= (1 << 12) | (1 << 15);

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x00, reg));
  } else {
    /* Everything is set the hard way. */

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x00, &reg));
    if (rc != GT_OK)
      goto out;

    if (port->c_duplex == PORT_DUPLEX_FULL) {
#ifndef VARIANT_FE
      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_FULL_DUPLEX_E);
#endif
      reg |= 1 << 8;
    }else {
#ifndef VARIANT_FE
      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_HALF_DUPLEX_E);
#endif
      reg &= ~(1 << 8);
    }

    switch (port->c_speed) {
    case PORT_SPEED_10:
      reg &= ~((1 << 6) | (1 << 13));
      break;
    case PORT_SPEED_100:
      reg &= ~(1 << 6);
      reg |= 1 << 13;
      break;
    case PORT_SPEED_1000:
      reg |= (1 << 6) | (1 << 13);
      break;
    default:
      /* We should never get here. */
      phy_unlock();
      return ST_BAD_VALUE;
    }

    reg |= 1 << 15;
    reg &= ~(1 << 12);

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x00, reg));

  }

 out:
  phy_unlock();
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_update_sd_fe (struct port *port)
{
  GT_STATUS rc;
  GT_U16 reg;

  phy_lock();
  if (port->c_speed_auto || port->c_duplex == PORT_DUPLEX_AUTO) {
    /* Speed or duplex is AUTO. */

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x04, &reg));
    if (rc != GT_OK)
      goto out;

    reg |= 0x01E0;

    switch (port->c_speed) {
    case PORT_SPEED_10:
      reg &= ~((1 << 7) | (1 << 8));
      break;
    case PORT_SPEED_100:
      reg &= ~((1 << 5) | (1 << 6));
      break;
    default:
      break;
    }

    switch (port->c_duplex) {
    case PORT_DUPLEX_FULL:
//      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
//      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_FULL_DUPLEX_E);
      reg &= ~((1 << 5) | (1 << 7));
      break;
    case PORT_DUPLEX_HALF:
//      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
//      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_HALF_DUPLEX_E);
      reg &= ~((1 << 6) | (1 << 8));
      break;
    case PORT_DUPLEX_AUTO:
//      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_FULL_DUPLEX_E);
//      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_TRUE);
      break;
    default:
      break;
    }

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x04, reg));
    if (rc != GT_OK)
      goto out;

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x00, &reg));
    if (rc != GT_OK)
      goto out;

    reg |= (1 << 12) | (1 << 15);

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x00, reg));
  } else {
    /* Everything is set the hard way. */

    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x00, &reg));
    if (rc != GT_OK)
      goto out;

    if (port->c_duplex == PORT_DUPLEX_FULL) {
//      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
//      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_FULL_DUPLEX_E);
      reg |= 1 << 8;
    }else {
//      cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
//      cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_HALF_DUPLEX_E);
      reg &= ~(1 << 8);
    }

    if (port->c_speed == PORT_SPEED_100)
      reg |= 1 << 13;
    else
      reg &= ~(1 << 13);

    reg |= 1 << 15;
    reg &= ~(1 << 12);

    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x00, reg));

  }

 out:
  phy_unlock();
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_set_speed_fe (struct port *port, const struct port_speed_arg *psa)
{
  if (psa->speed > PORT_SPEED_100)
    return ST_BAD_VALUE;

  port->c_speed = psa->speed;
  port->c_speed_auto = psa->speed_auto;

  return port_update_sd_fe (port);
}

static enum status
port_set_duplex_fe (struct port *port, enum port_duplex duplex)
{
  port->c_duplex = duplex;
  return port_update_sd_fe (port);
}

static enum status
__port_shutdown_fe (GT_U8 dev, GT_U8 port, int shutdown)
{
  GT_STATUS rc;
  GT_U16 reg;

  phy_lock();
  rc = CRP (cpssDxChPhyPortSmiRegisterRead (dev, port, 0x00, &reg));
  if (rc != GT_OK)
    goto out;

  if (shutdown)
    reg |= (1 << 11);
  else
    reg &= ~(1 << 11);

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite (dev, port, 0x00, reg));

 out:
  phy_unlock();
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_shutdown_fe (struct port *port, int shutdown)
{
  return __port_shutdown_fe (port->ldev, port->lport, shutdown);
}

static enum status
__port_setup_fe (GT_U8 dev, GT_U8 port)
{
  CRP (cpssDxChPortTxBindPortToDpSet
       (dev, port, CPSS_PORT_TX_DROP_PROFILE_2_E));
  CRP (cpssDxChPortTxBindPortToSchedulerProfileSet
       (dev, port, CPSS_PORT_TX_SCHEDULER_PROFILE_2_E));

  CRP (cpssDxChPhyPortAddrSet (dev, port, (GT_U8) (port % 16)));
  /* workaround for activated speed LED without active link status in no
     autoneg portconfiguretion, disable LED */
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (dev, port, 0x19, 0x0002));

  return ST_OK;
}

static enum status
port_setup_fe (struct port *port)
{
  return __port_setup_fe (port->ldev, port->lport);
}

static enum status
port_set_speed_ge (struct port *port, const struct port_speed_arg *psa)
{
  if (psa->speed > PORT_SPEED_1000)
    return ST_BAD_VALUE;

  if (port->type == PTYPE_FIBER_PHYLESS
      && psa->speed != PORT_SPEED_1000
      && psa->speed != PORT_SPEED_AUTO) {
    return ST_BAD_VALUE;
  }

  port->c_speed = psa->speed;
  port->c_speed_auto = psa->speed_auto;

  return port_update_sd_ge (port);
}

static enum status
port_set_duplex_ge (struct port *port, enum port_duplex duplex)
{
  if (port->type == PTYPE_FIBER_PHYLESS
      && duplex != PORT_DUPLEX_AUTO
      && duplex != PORT_DUPLEX_FULL) {
    return ST_BAD_VALUE;
  }

  port->c_duplex = duplex;
  return port_update_sd_ge (port);
}

static enum status
port_shutdown_ge (struct port *port, int shutdown)
{
  GT_STATUS rc = GT_OK;
  GT_U16 reg, start_reg;

  if (port_is_phyless(port)) {
    rc = CRP (cpssDxChPortForceLinkDownEnableSet(
               port->ldev, port->lport, gt_bool(shutdown)));

    switch (rc) {
    case GT_OK:       return ST_OK;
    case GT_HW_ERROR: return ST_HW_ERROR;
    default:          return ST_HEX;
    }
  }

  CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 22, &start_reg));

  phy_lock();
  /* LEDs */
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 22, 0));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 29, 0));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 30, shutdown ? 0x20 : 0));
  /* END_LEDs */

#if defined (VARIANT_FE)
  CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x00, &reg));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00,
       (shutdown)?(reg | (1 << 11)):(reg & ~(1 << 11))));
  if (IS_GE_PORT (port->id -1)) { /* FIXME: maybe always true */
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 1));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 0x00, &reg));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00,
         (shutdown)?(reg | (1 << 11)):(reg & ~(1 << 11))));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0));
  }
#elif defined (VARIANT_GE)
  if (port->type == PTYPE_COPPER) {
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 0x00, &reg));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00,
         (shutdown)?(reg | (1 << 11)):(reg & ~(1 << 11))));
  }

  if (port->type == PTYPE_FIBER) {
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 1));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 0x00, &reg));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00,
         (shutdown)?(reg | (1 << 11)):(reg & ~(1 << 11))));
  }

  if (port->type == PTYPE_COMBO) {
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 0x00, &reg));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00,
         (shutdown)?(reg | (1 << 11)):(reg & ~(1 << 11))));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 1));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 0x00, &reg));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00,
         (shutdown)?(reg | (1 << 11)):(reg & ~(1 << 11))));
  }
#endif

  CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, start_reg));

  phy_unlock();
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

static enum status
port_set_speed_xg (struct port *port, const struct port_speed_arg *psa)
{
  DEBUG ("%s(): STUB!", __PRETTY_FUNCTION__);
  return ST_OK;
}

static enum status
port_set_duplex_xg (struct port *port, enum port_duplex duplex)
{
  DEBUG ("%s(): STUB!", __PRETTY_FUNCTION__);
  return ST_OK;
}

static enum status
port_update_sd_xg (struct port *port)
{
  DEBUG ("%s(): STUB!", __PRETTY_FUNCTION__);
  return ST_OK;
}

static enum status
port_shutdown_xg (struct port *port, int shutdown)
{
  if (port_is_phyless(port)) {
    GT_STATUS rc = CRP (cpssDxChPortForceLinkDownEnableSet(
                        port->ldev, port->lport, gt_bool(shutdown)));

    switch (rc) {
    case GT_OK:       return ST_OK;
    case GT_HW_ERROR: return ST_HW_ERROR;
    default:          return ST_HEX;
    }
  }

  uint16_t val;
  cpssXsmiPortGroupRegisterRead (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC319, 1, &val);

  val = shutdown ? (val | (1 << 1)) : (val & ~(1 << 1));
  cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC319, 1, val);

  return ST_OK;
}

enum status
port_set_speed (port_id_t pid, const struct port_speed_arg *psa)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (psa->speed == PORT_SPEED_AUTO && !psa->speed_auto)
    return ST_BAD_VALUE;

  return port->set_speed (port, psa);
}

enum status
port_set_duplex (port_id_t pid, port_duplex_t duplex)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (duplex >= __PORT_DUPLEX_MAX)
    return ST_BAD_VALUE;

  return port->set_duplex (port, duplex);
}

enum status
port_set_sfp_mode (port_id_t pid, enum port_sfp_mode mode)
{
  GT_STATUS rc;
  uint16_t mode_val, reg_val;
  struct port *port = port_ptr (pid);

  if (!port) {
    return ST_BAD_VALUE;
  }

  /*
    The value that we should put into register to switch to 100 Mbps fiber mode
    is the same for both fiber and combo ports. However, the value for 1000 Mbps
    on fiber ports should be 0x0002 which means QSGMII to 1000BASE-X; for combo
    ports it's 0x0007 which means QSGMII to auto media detect 1000BASE-X. It's
    done this way because we still care about the copper on combo ports, and if
    we dont set auto media detect mode on them, the copper won't work.
    If we're in 100BASE-FX mode, then we don't want copper to work, so it
    doesn't with the 0x0003 value.
  */

  uint16_t mode_100mbps = 0x0003;
  uint16_t mode_1000mbps = 0x0002;

#if defined (VARIANT_FE) || defined (VARIANT_ARLAN_3424PFE)
  if (pid > 24) {
    mode_1000mbps = 0x0007;
  }

  else {
    return GT_BAD_PARAM;
  }
#elif defined (VARIANT_GE)
  switch (env_hw_subtype()) {
    case HWST_ARLAN_3424GE_F:
    case HWST_ARLAN_3424GE_F_S:
      if (pid >= 25) {
        return GT_BAD_PARAM;
      }
    break;

    case HWST_ARLAN_3424GE_U:
      if (pid <= 12 || pid >= 25) {
        return GT_BAD_PARAM;
      }
    break;

    default:
      if (pid == 23 || pid == 24) {
        mode_1000mbps = 0x0007;
      }

      else {
        return GT_BAD_PARAM;
      }
    break;
  }
#endif

  switch (mode) {
    case PSM_100: mode_val = mode_100mbps; break;
    case PSM_1000: mode_val = mode_1000mbps; break;
    default: return ST_BAD_VALUE;
  }

  phy_lock();
#if defined (VARIANT_FE)
  rc = CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x10 + (port->lport - 24) * 2));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x6));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x14, &reg_val));

  reg_val &= ~(1 << 0);
  reg_val &= ~(1 << 1);
  reg_val &= ~(1 << 2);
  reg_val |= (1 << 15);
  reg_val += 0x0005;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x14, reg_val));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x4));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x1B, &reg_val));

  reg_val |= (1 << 0);
  reg_val |= (1 << 1);
  reg_val |= (1 << 14);

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x1B, reg_val));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x11 + (port->lport - 24) * 2));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x6));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x14, &reg_val));

  reg_val &= ~(1 << 0);
  reg_val &= ~(1 << 1);
  reg_val &= ~(1 << 2);
  reg_val |= (1 << 15);
  reg_val += mode_val;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x14, reg_val));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x4));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));

  if (rc != GT_OK)
    goto out;


#elif defined (VARIANT_GE)
  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x6));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x14, &reg_val));

  reg_val &= ~(1 << 0);
  reg_val &= ~(1 << 1);
  reg_val &= ~(1 << 2);
  reg_val |= (1 << 15);
  reg_val += mode_val;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x14, reg_val));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x4));

  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));

  if (rc != GT_OK)
    goto out;

  CRP (cpssDxChPortInbandAutoNegEnableSet
         (port->ldev, port->lport, GT_TRUE));

  /*
    If requested port is fiber, we should return on page 1
  */
  if (mode_1000mbps == 0x0002) {
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0001));
  }

  /* else if combo return to page 0 */
  else {
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
  }

  /*
    then reset either copper or fiber (page 0 is copper, page 1 is fiber).
    On 3424GE it's necessary to reset copper, not QSGMII like on 3424FE to make
    combo ports work.
  */
  CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00, 0x9140));
#endif

 out:
  phy_unlock();
  switch (rc) {
    case GT_OK:            return ST_OK;
    case GT_HW_ERROR:      return ST_HW_ERROR;
    case GT_BAD_PARAM:     return ST_BAD_VALUE;
    case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
    default:               return ST_HEX;
  }
}

bool_t
port_is_xg_sfp_present (port_id_t pid)
{
  struct port *port = port_ptr (pid); /* TODO fiber phyless */
  uint16_t val;
  cpssXsmiPortGroupRegisterRead (port->ldev, 1, 0x18 + port->lport - 24, 0xC200,
                                 1, &val);

  return !(val & 1);
}

uint8_t*
port_read_xg_sfp_idprom (port_id_t pid, uint16_t addr) /* TODO fiber phyless */
{
  const int sz = 128;
  const int phydev = addr == 0xD000 ? 3 : 1;
  uint8_t *ret = malloc (sz);
  uint8_t *cur = ret;

  struct port *port = port_ptr (pid);
  int i;

  uint16_t ready_val;
  bool_t ready;

  /*
    We should wait for bit 1 of 3.D100 register to be zero (see page 113 of
    QT2025 programmer's reference manual). The ready variable is true when the
    mentioned bit is zero (hence these NOT operations).
  */
  do {
    cpssXsmiPortGroupRegisterRead (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xD100, 3, &ready_val);

    ready = !((ready_val >> 1) & 1);
  } while (!ready);

  for (i = 0; i < sz; ++i) {
    cpssXsmiPortGroupRegisterRead (port->ldev, 1, 0x18 + port->lport - 24, addr,
                                   phydev, (uint16_t *) cur);
    cur++;
    addr++;
  }

  return ret;
}

enum status
port_set_xg_sfp_mode (port_id_t pid, enum port_sfp_mode mode)
{
  struct port *port = port_ptr (pid);

  /* PHY must be configured first */
  if (mode == PSM_1000) {
    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0000, 1, 0x8000);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC300, 1, 0x0000);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC302, 1, 0x0004);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC319, 1, 0x0088);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC31A, 1, 0x0098);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0026, 3, 0x0E00);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0027, 3, 0x1012);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0028, 3, 0xA528);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0029, 3, 0x0003);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC300, 1, 0x0002);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xE854, 3, 0x00C0);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xE854, 3, 0x0040);

    uint16_t asd;
    do {
      usleep (30);
      cpssXsmiPortGroupRegisterRead (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xD7FD, 3, &asd);

      DEBUG ("asd is %d\n", asd);

    } while (asd == 0x0 || asd == 0x10);

    /* Enable DOM periodic update (see p. 31 of QT2025 firmware release note) */
    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xD71A, 3, 0x0001);

    /* End of PHY configuration. Now configure Prestera and SERDES */

    cpssDxChPortInterfaceModeSet (port->ldev, port->lport, CPSS_PORT_INTERFACE_MODE_SGMII_E);
    cpssDxChPortSpeedAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
    cpssDxChPortSpeedSet(port->ldev, port->lport, CPSS_PORT_SPEED_1000_E);
    cpssDxChPortDuplexAutoNegEnableSet(port->ldev, port->lport, GT_FALSE);
    cpssDxChPortDuplexModeSet(port->ldev, port->lport, CPSS_PORT_FULL_DUPLEX_E);
    cpssDxChPortFlowCntrlAutoNegEnableSet(port->ldev, port->lport, GT_FALSE, GT_FALSE);
    cpssDxChPortFlowControlEnableSet(port->ldev, port->lport, CPSS_PORT_FLOW_CONTROL_DISABLE_E);
    cpssDxChPortSerdesPowerStatusSet(port->ldev, port->lport, CPSS_PORT_DIRECTION_BOTH_E, 0x1, GT_TRUE);
  }

  else if (mode == PSM_10G) {
    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0000, 1, 0x8000);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC300, 1, 0x0000);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC05F, 4, 0x0000);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC05F, 4, 0x3C00);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC302, 1, 0x0004);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC319, 1, 0x0038);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC31A, 1, 0x0098);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0026, 3, 0x0E00);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0027, 3, 0x0812);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0028, 3, 0xA528);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0x0029, 3, 0x0003);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xC300, 1, 0x0002);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xE854, 3, 0x00C0);

    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xE854, 3, 0x0050);

    uint16_t asd;
    do {
      usleep (30);
      cpssXsmiPortGroupRegisterRead (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xD7FD, 3, &asd);

      DEBUG ("asd is %d\n", asd);

    } while (asd == 0x0 || asd == 0x10);

    /* Enable DOM periodic update (see p. 31 of QT2025 firmware release note) */
    cpssXsmiPortGroupRegisterWrite (port->ldev, 1, 0x18 + port->lport - 24,
                                   0xD71A, 3, 0x0001);

    CRP (cpssDxChPortInterfaceModeSet
         (port->ldev, port->lport, CPSS_PORT_INTERFACE_MODE_XGMII_E));
    CRP (cpssDxChPortSpeedSet
         (port->ldev, port->lport, CPSS_PORT_SPEED_10000_E));
    CRP (cpssDxChPortSerdesPowerStatusSet
         (port->ldev, port->lport, CPSS_PORT_DIRECTION_BOTH_E, 0x0F, GT_TRUE));
    CRP (cpssXsmiPortGroupRegisterWrite
         (port->ldev, 1, 0x18 + port->lport - 24, 0xD70D, 3, 0x0020));
  }

  else return GT_BAD_PARAM;

  return ST_OK;
}

enum status
port_dump_phy_reg (port_id_t pid, uint16_t page, uint16_t reg, uint16_t *val)
{
  GT_STATUS rc;
  GT_U16 pg;
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  phy_lock();
  if (!IS_FE_PORT(pid - 1)) { /* if phy is of 88E1340 series */
#if defined (VARIANT_FE)
    if (page >= 1000)
      CRP (cpssDxChPhyPortAddrSet
           (port->ldev, port->lport, 0x10 + (port->lport - 24) * 2));
#endif
    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x16, &pg));
    if (rc != GT_OK)
      goto out;

#if defined (VARIANT_FE)
    if (page >= 1000)
      rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                (port->ldev, port->lport, 0x16, page - 1000));
    else
#endif /* XXX Achtung! Lebensgefahr! */
      rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                (port->ldev, port->lport, 0x16, page));
    if (rc != GT_OK)
      goto out;
  }

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, reg, val));
  if (rc != GT_OK)
    goto out;

  if (!IS_FE_PORT(pid - 1)) { /* if phy is of 88E1340 series */
    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x16, pg));
    if (rc != GT_OK)
      goto out;
#if defined (VARIANT_FE)
    if (page >= 1000)
      CRP (cpssDxChPhyPortAddrSet
           (port->ldev, port->lport, 0x11 + (port->lport - 24) * 2));
#endif
  }

 out:
  phy_unlock();
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

enum status
port_set_phy_reg (port_id_t pid, uint16_t page, uint16_t reg, uint16_t val)
{
  GT_STATUS rc;
  GT_U16 pg;
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  phy_lock();
  if (!IS_FE_PORT(pid - 1)) { /* if phy is of 88E1340 series */
#if defined (VARIANT_FE)
    if (page >= 1000)
      CRP (cpssDxChPhyPortAddrSet
           (port->ldev, port->lport, 0x10 + (port->lport - 24) * 2));
#endif
    rc = CRP (cpssDxChPhyPortSmiRegisterRead
              (port->ldev, port->lport, 0x16, &pg));
    if (rc != GT_OK)
      goto out;

#if defined (VARIANT_FE)
    if (page >= 1000)
      rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                (port->ldev, port->lport, 0x16, page - 1000));
    else
#endif /* XXX Achtung! Lebensgefahr! */
      rc = CRP (cpssDxChPhyPortSmiRegisterWrite
                (port->ldev, port->lport, 0x16, page));
    if (rc != GT_OK)
      goto out;
  }

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, reg, val));
  if (rc != GT_OK)
    goto out;

  if (!IS_FE_PORT(pid - 1)) { /* if phy is of 88E1340 series */
    rc = CRP (cpssDxChPhyPortSmiRegisterWrite
              (port->ldev, port->lport, 0x16, pg));
    if (rc != GT_OK)
      goto out;
#if defined (VARIANT_FE)
    if (page >= 1000)
      CRP (cpssDxChPhyPortAddrSet
           (port->ldev, port->lport, 0x11 + (port->lport - 24) * 2));
#endif
  }

 out:
  phy_unlock();
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

static enum status
port_set_mdix_auto_fe (struct port *port, int mdix_auto)
{
  GT_STATUS rc;
  GT_U16 val;

  phy_lock();
  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x10, &val));
  if (rc != GT_OK)
    goto out;

  if (mdix_auto)
    val |= 3 << 4;
  else {
    val &= ~(1 << 5);
    val |= 1 << 4;
  }

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x10, val));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x00, &val));
  if (rc != GT_OK)
    goto out;

  val |= 1 << 15;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x00, val));

 out:
  phy_unlock();
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

static enum status
port_set_mdix_auto_ge (struct port *port, int mdix_auto)
{
  GT_STATUS rc;
  GT_U16 val;

  if (port_is_phyless(port)) {
    return ST_BAD_VALUE;
  }

  phy_lock();
  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x10, &val));
  if (rc != GT_OK)
    goto out;

  if (mdix_auto)
    val |= 3 << 5;
  else {
    val &= ~(1 << 6);
    val |= 1 << 5;
  }

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x10, val));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x00, &val));
  if (rc != GT_OK)
    goto out;

  val |= 1 << 15;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x00, val));

 out:
  phy_unlock();
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

static enum status
port_set_mdix_auto_xg (struct port *port, int mdix_auto)
{
  DEBUG ("%s(): STUB!", __PRETTY_FUNCTION__);
  return ST_OK;
}

enum status
port_set_mdix_auto (port_id_t pid, int mdix_auto)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  return port->set_mdix_auto (port, mdix_auto);
}

enum status
port_set_flow_control (port_id_t pid, flow_control_t fc)
{
  struct port *port = port_ptr (pid);
  CPSS_PORT_FLOW_CONTROL_ENT type;
  GT_BOOL aneg;
  GT_U16 val;
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  switch (fc) {
  case FC_DESIRED:
    aneg = GT_TRUE;
    type = CPSS_PORT_FLOW_CONTROL_RX_TX_E;
    break;
  case FC_ON:
    aneg = GT_FALSE;
    type = CPSS_PORT_FLOW_CONTROL_RX_TX_E;
    break;
  case FC_OFF:
    aneg = GT_FALSE;
    type = CPSS_PORT_FLOW_CONTROL_DISABLE_E;
    break;
  default:
    return ST_BAD_VALUE;
  }

  phy_lock();
  rc = CRP (cpssDxChPortFlowCntrlAutoNegEnableSet
            (port->ldev, port->lport, aneg, GT_FALSE));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPortFlowControlEnableSet
            (port->ldev, port->lport, type));
  if (rc != GT_OK)
    goto out;

  if (port_is_phyless(port)) {
    goto out;
  }

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x04, &val));
  if (rc != GT_OK)
    goto out;

  val |= 1 << 10;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x04, val));
  if (rc != GT_OK)
    goto out;

  rc = CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0x00, &val));
  if (rc != GT_OK)
    goto out;

  val |= 1 << 15;

  rc = CRP (cpssDxChPhyPortSmiRegisterWrite
            (port->ldev, port->lport, 0x00, val));

 out:
  phy_unlock();
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

enum status
port_get_stats (port_id_t pid, void *stats)
{
  GT_U8 ldev, lport;
  GT_STATUS rc;

  if (pid == CPU_PORT) {
    ldev = 0;
    lport = CPSS_CPU_PORT_NUM_CNS;
  } else  {
    struct port *port = port_ptr (pid);

    if (!port)
      return ST_BAD_VALUE;

    ldev = port->ldev;
    lport = port->lport;
  }

  rc = CRP (cpssDxChPortMacCountersOnPortGet (ldev, lport, stats));
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
port_clear_stats (port_id_t pid)
{
  if (!port_valid (pid)) {
    return ST_BAD_VALUE;
  }

  struct port *port = port_ptr (pid);
  CPSS_PORT_MAC_COUNTER_SET_STC stats;
  CRP (cpssDxChPortMacCountersClearOnReadSet (port->ldev, port->lport, GT_TRUE));
  CRP (cpssDxChPortMacCountersOnPortGet (port->ldev, port->lport, &stats));
  CRP (cpssDxChPortMacCountersClearOnReadSet (port->ldev, port->lport, GT_FALSE));

  return ST_OK;
}

static uint64_t
max_bps (enum port_speed ps)
{
  switch (ps) {
  case PORT_SPEED_10:    return 10000000ULL;
  case PORT_SPEED_100:   return 100000000ULL;
  case PORT_SPEED_1000:  return 1000000000ULL;
  case PORT_SPEED_10000: return 10000000000ULL;
  case PORT_SPEED_12000: return 12000000000ULL;
  case PORT_SPEED_2500:  return 2500000000ULL;
  case PORT_SPEED_5000:  return 5000000000ULL;
  case PORT_SPEED_13600: return 13600000000ULL;
  case PORT_SPEED_20000: return 20000000000ULL;
  case PORT_SPEED_40000: return 40000000000ULL;
  case PORT_SPEED_16000: return 16000000000ULL;
  default:               return 0ULL;
  }
}

enum status
port_set_rate_limit (port_id_t pid, const struct rate_limit *rl)
{
  struct port *port = port_ptr (pid);
  uint32_t div;
  GT_STATUS rc;
  CPSS_DXCH_BRG_GEN_RATE_LIMIT_PORT_STC cfg = {
    .enableMcReg = GT_FALSE,
    .enableUcKnown = GT_FALSE
  };

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (rl->type >= __TT_MAX)
    return ST_BAD_VALUE;

  if (rl->limit) {
    switch (rl->type) {
    case TT_UNICAST:
      cfg.enableBc = GT_TRUE;
      cfg.enableMc = GT_TRUE;
      cfg.enableUcUnk = GT_TRUE;
      break;

    case TT_MULTICAST:
      cfg.enableBc = GT_TRUE;
      cfg.enableMc = GT_TRUE;
      cfg.enableUcUnk = GT_FALSE;
      break;

    case TT_BROADCAST:
      cfg.enableBc = GT_TRUE;
      cfg.enableMc = GT_FALSE;
      cfg.enableUcUnk = GT_FALSE;
    }

    /*
       For devices with FE ports (3424FE, 3424PFE, SM12F),
       divider must be 10 times higher, when current port is GE port.
    */
    #if defined (VARIANT_ARLAN_3424FE) || defined (VARIANT_ARLAN_3424PFE) || defined (VARIANT_SM_12F)

              if (IS_XG_PORT (pid - 1)) {
                div = 5120;
              } else {
                if (IS_GE_PORT (pid - 1)) {
                  div = 512000;
                } else {
                  div = 51200;
                }
              }

    #else
              div = IS_XG_PORT (pid - 1) ? 5120 : 51200;
    #endif

    cfg.rateLimit = (rl->limit / div) ? : 1;
  } else {
    cfg.enableBc = GT_FALSE;
    cfg.enableMc = GT_FALSE;
    cfg.enableUcUnk = GT_FALSE;
    cfg.rateLimit = 0;
  }

  rc = CRP (cpssDxChBrgGenPortRateLimitSet (port->ldev, port->lport, &cfg));
  switch (rc) {
  case GT_OK:           return ST_OK;
  case GT_HW_ERROR:     return ST_HW_ERROR;
  case GT_OUT_OF_RANGE: return ST_NOT_SUPPORTED;
  default:              return ST_HEX;
  }
}

enum status
port_set_traffic_shape (port_id_t pid, bool_t enable, bps_t rate, burst_t burst)
{
  struct port *port = port_ptr (pid);
  uint64_t max;
  GT_STATUS rc;
  GT_U32 rate_kbps, burst_units, zero = 0;

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  max = max_bps (port->max_speed);
  if (max == 0)
    return ST_HEX;

  if (rate > max)
    return ST_BAD_VALUE;

  rate_kbps = rate / 1000;
  burst_units = round ((float)burst/ 4096.0);

  if (!enable)
    rc = CRP (cpssDxChPortTxShaperEnableSet
              (port->ldev, port->lport, GT_FALSE));
  else {
    CRP (cpssDxChPortTxShaperProfileSet
         (port->ldev, port->lport, burst_units, &zero));
    rc = CRP (cpssDxChPortTxShaperProfileSet
              (port->ldev, port->lport, burst_units, &rate_kbps));
    ON_GT_ERROR (rc) goto out;

    rc = CRP (cpssDxChPortTxShaperEnableSet
              (port->ldev, port->lport, GT_TRUE));
  }

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
port_set_traffic_shape_queue (port_id_t pid, bool_t enable, queueid_t qid,
                               bps_t rate, burst_t burst)
{
  struct port *port = port_ptr (pid);
  uint64_t max;
  GT_STATUS rc;
  GT_U32 rate_kbps, burst_units, zero = 0;

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  max = max_bps (port->max_speed);
  if (max == 0)
    return ST_HEX;

  if (rate > max)
    return ST_BAD_VALUE;

  if ((qid < 1) || (qid > 8))
    return ST_BAD_VALUE;

  rate_kbps = rate / 1000;
  burst_units = round ((float)burst/ 4096.0);

  if (!enable)
    rc = CRP (cpssDxChPortTxQShaperEnableSet
              (port->ldev, port->lport, qid - 1, GT_FALSE));
  else {
    CRP (cpssDxChPortTxQShaperProfileSet
         (port->ldev, port->lport, qid - 1, burst_units, &zero));
    rc = CRP (cpssDxChPortTxQShaperProfileSet
              (port->ldev, port->lport, qid - 1, burst_units, &rate_kbps));
    ON_GT_ERROR (rc) goto out;

    rc = CRP (cpssDxChPortTxQShaperEnableSet
              (port->ldev, port->lport, qid - 1, GT_TRUE));
  }

 out:
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

#if defined (VARIANT_FE)
static enum status
port_setup_ge (struct port *port)
{
  GT_U16 val;

  CRP (cpssDxChPortTxBindPortToDpSet
       (port->ldev, port->lport, CPSS_PORT_TX_DROP_PROFILE_2_E));
  CRP (cpssDxChPortTxBindPortToSchedulerProfileSet
       (port->ldev, port->lport, CPSS_PORT_TX_SCHEDULER_PROFILE_2_E));

  CRP (cpssDxChPhyPortSmiInterfaceSet
       (port->ldev, port->lport, CPSS_PHY_SMI_INTERFACE_1_E));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x10 + (port->lport - 24) * 2));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x6));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x14, 0x8205));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x4));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x1A, 0xB002));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x1B, 0x7C03));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x1));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x11 + (port->lport - 24) * 2));

  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x09, 0x0800)); /* workaround to enable autonegotiation */

  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x6));
/*  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x14, 0x8207)); */
/* set Auto Media Detect Preffered Media to "Fiber Preferred" */
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x14, 0x8227));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x4));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0xFF));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x18, 0x2800));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x17, 0x2001));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x18, 0x1F70));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x17, 0x2004));

  /* Configure LEDs. */
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x3));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x10, 0x1777));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x11, 0x8845));
  /* END: Configure LEDs. */

  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x0));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x1A, 0x9040));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, 0x9140));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x10 + (port->lport - 24) * 2));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x2));
  CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x10, &val));
  val &= ~(1 << 3);
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x10, val));
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x0));
  CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x00, &val));
  val |= 1 << 15;
  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x00, val));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x11 + (port->lport - 24) * 2));

  port_set_sgmii_mode (port);

  return ST_OK;
}
#elif defined (VARIANT_GE)
static enum status
port_setup_ge (struct port *port)
{
  GT_U16 val;
  CRP (cpssDxChPortTxBindPortToDpSet
       (port->ldev, port->lport, CPSS_PORT_TX_DROP_PROFILE_2_E));
  CRP (cpssDxChPortTxBindPortToSchedulerProfileSet
       (port->ldev, port->lport, CPSS_PORT_TX_SCHEDULER_PROFILE_2_E));

  if (port_is_phyless(port)) {
    port_setup_phyless_ge (port);
    return ST_OK;
  }

  CRP (cpssDxChPhyPortSmiInterfaceSet
       (port->ldev, port->lport,
        (port->lport < 12)
        ? CPSS_PHY_SMI_INTERFACE_0_E
        : CPSS_PHY_SMI_INTERFACE_1_E));

  CRP (cpssDxChPhyPortAddrSet
       (port->ldev, port->lport, 0x04 + (port->lport % 12)));

  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x0000));
  CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x03, &val));
  /* DEBUG ("port %d reg 0:3 is 0x%04X\r\n", port->id, val); */
  if (val == 0x0DC0) {
    /* DEBUG ("Activating A0 revision workaround\r\n"); */

    /* A0 Revision errata. */
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0x00FF));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 24, 0x2800));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 23, 0x2001));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0x0000));

    /* A0 Revision errata for BGA. */
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 29, 0x0003));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 30, 0x0002));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 29, 0x0000));

    /* Check it. */
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0x00FF));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 23, 0x1001));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 25, &val));
    /* DEBUG ("check 1: 0x%04X, must be 0x2800, %s\r\n", */
    /*        val, (val == 0x2800) ? "good" : "bad"); */
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0x0000));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 29, 0x0003));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 30, &val));
    /* DEBUG ("check 2: 0x%04X, must be 0x0002, %s\r\n", */
    /*        val, (val == 0x0002) ? "good" : "bad"); */
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 29, 0x0000));
  }

  switch (port->type) {
  case PTYPE_FIBER:
    /* DEBUG ("port %d is fiber\n", port->id); */

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 0x03, &val));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0006));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x14, 0x8202));

    usleep (10000);

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0003));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x11, 0x8845));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x10, 0x1777));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x12, 0x4905));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x13, 0x0073));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0001));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00, 0x9140));

    break;

  case PTYPE_COPPER:
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x09, 0x0800)); /* workaround to enable autonegotiation */

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x3));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x11, 0x8845));

    /* DEBUG ("port %d is copper\n", port->id); */
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 6));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0));

    CRP (cpssDxChPortInbandAutoNegEnableSet
         (port->ldev, port->lport, GT_TRUE));
    break;

  case PTYPE_COMBO:
    /* DEBUG ("port %d is combo\n", port->id); */

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x09, 0x0800)); /* workaround to enable autonegotiation */

    /* Configure Auto-Media detect */
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x6));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x14, 0x0207));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x14, 0x8207));
    usleep (10000);
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x4));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00, 0x9140));

    /* Configure LEDs */
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x3));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x11, 0x8845));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x10, 0x1777));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x2));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x10, 0x4008));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));
    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 0x00, &val));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00, val | (1 << 15)));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0001));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x04, 0x0020));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x10, 0x4085));

    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 0x00, &val));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00, val | (1 << 15)));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0004));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x10, 0x6004));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x1A, 0x3000));

    CRP (cpssDxChPhyPortSmiRegisterRead
         (port->ldev, port->lport, 0x00, &val));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x00, val | (1 << 12)));

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x6));
    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x14, 0x8207));
    usleep (10000);

    CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 0x16, 0x0000));

    CRP (cpssDxChPortInbandAutoNegEnableSet
         (port->ldev, port->lport, GT_TRUE));
  }

  return ST_OK;
}
#endif /* VARIANT_* */

static enum status __attribute__ ((unused))
port_setup_phyless_ge (struct port *port) {

  CRP (cpssDxChPortInterfaceModeSet
        (port->ldev, port->lport, CPSS_PORT_INTERFACE_MODE_1000BASE_X_E));

  CRP (cpssDxChPortSpeedSet
        (port->ldev, port->lport, CPSS_PORT_SPEED_1000_E));

  CRP(cpssDxChPortDuplexModeSet(
        port->ldev, port->lport, CPSS_PORT_FULL_DUPLEX_E));

  CRP(cpssDxChPortDuplexAutoNegEnableSet(
        port->ldev, port->lport, GT_FALSE));

  CRP (cpssDxChPortSpeedAutoNegEnableSet
        (port->ldev, port->lport, GT_FALSE));

  CRP (cpssDxChPortSerdesPowerStatusSet
        (port->ldev, port->lport, CPSS_PORT_DIRECTION_BOTH_E, 0x01, GT_TRUE));

  CRP (cpssDxChPortInbandAutoNegEnableSet
        (port->ldev, port->lport, GT_TRUE));


  CPSS_LED_CONF_STC lconf = {
    .ledOrganize        = CPSS_LED_ORDER_MODE_BY_PORT_E,
    .disableOnLinkDown  = GT_TRUE,
    .blink0DutyCycle    = CPSS_LED_BLINK_DUTY_CYCLE_1_E,
    .blink0Duration     = CPSS_LED_BLINK_DURATION_3_E,
    .blink1DutyCycle    = CPSS_LED_BLINK_DUTY_CYCLE_1_E,
    .blink1Duration     = CPSS_LED_BLINK_DURATION_4_E,
    .pulseStretch       = CPSS_LED_PULSE_STRETCH_5_E,
    .ledStart           = 0,
    .ledEnd             = 15,
    .clkInvert          = GT_TRUE,
    .class5select       = CPSS_LED_CLASS_5_SELECT_FIBER_LINK_UP_E,
    .class13select       = CPSS_LED_CLASS_13_SELECT_COPPER_LINK_UP_E
  };

  CRP (cpssDxChLedStreamConfigSet
        (port->ldev, 0, &lconf));

  CPSS_LED_GROUP_CONF_STC ledGroupParams = {
    .classA = 0x9,
    .classB = 0xA,
    .classC = 0xB,
    .classD = 0xA
  };

  CRP(cpssDxChLedStreamGroupConfigSet(
        port->ldev, 0, CPSS_DXCH_LED_PORT_TYPE_XG_E, 0, &ledGroupParams));

  CRP(cpssDxChLedStreamGroupConfigSet(
        port->ldev, 0, CPSS_DXCH_LED_PORT_TYPE_XG_E, 1, &ledGroupParams));

  CRP(cpssDxChLedStreamClassIndicationSet(
        port->ldev, 0, 9, CPSS_DXCH_LED_INDICATION_RX_ACT_E));

  CRP(cpssDxChLedStreamClassIndicationSet(
        port->ldev, 0, 10, CPSS_DXCH_LED_INDICATION_LINK_E));

  CRP(cpssDxChLedStreamClassIndicationSet(
        port->ldev, 0, 11, CPSS_DXCH_LED_INDICATION_TX_ACT_E));

  CPSS_LED_CLASS_MANIPULATION_STC ledClassParams;
  memset(&ledClassParams, 0, sizeof(ledClassParams));
  ledClassParams.invertEnable = GT_TRUE;
  ledClassParams.blinkEnable = GT_TRUE;
  ledClassParams.blinkSelect = CPSS_LED_BLINK_SELECT_0_E;
  ledClassParams.forceEnable = GT_FALSE;

  CRP(cpssDxChLedStreamClassManipulationSet(
        port->ldev, 0, CPSS_DXCH_LED_PORT_TYPE_XG_E, 9, &ledClassParams));

  CRP(cpssDxChLedStreamClassManipulationSet(
        port->ldev, 0, CPSS_DXCH_LED_PORT_TYPE_XG_E, 11, &ledClassParams));

  CRP (cpssDxChLedStreamDirectModeEnableSet
        (port->ldev, 0, GT_TRUE));

  return ST_OK;
}

static void __attribute__ ((unused))
dump_xg_reg (const struct port *port, GT_U32 dev, GT_U32 reg)
{
  GT_U16 val;

  CRP (cpssXsmiPortGroupRegisterRead
       (port->ldev, 1, 0x18 + port->lport - 24, reg, dev, &val));
  DEBUG ("port %d (%d) reg %d.%04X: 0x%04X\n",
         port->id, port->lport, dev, reg, val);
}

static enum status
port_setup_xg (struct port *port)
{
  CRP (cpssDxChPortTxBindPortToDpSet
       (port->ldev, port->lport, CPSS_PORT_TX_DROP_PROFILE_2_E));
  CRP (cpssDxChPortTxBindPortToSchedulerProfileSet
       (port->ldev, port->lport, CPSS_PORT_TX_SCHEDULER_PROFILE_2_E));

  CRP (cpssDxChPortInterfaceModeSet
       (port->ldev, port->lport, CPSS_PORT_INTERFACE_MODE_XGMII_E));
  CRP (cpssDxChPortSpeedSet
       (port->ldev, port->lport, CPSS_PORT_SPEED_10000_E));
  CRP (cpssDxChPortSerdesPowerStatusSet
       (port->ldev, port->lport, CPSS_PORT_DIRECTION_BOTH_E, 0x0F, GT_TRUE));


  CRP (cpssXsmiPortGroupRegisterWrite
       (port->ldev, 1, 0x18 + port->lport - 24, 0xD70D, 3, 0x0020));

  return ST_OK;
}

static void
port_update_iso (void)
{
  int i;

  for (i = 0; i < nports; i++)
    port_set_iso_bmp (&ports[i]);
}

enum status
port_set_protected (port_id_t pid, bool_t protected)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (__port_set_prot (port, !!protected))
    port_update_iso ();

  return ST_OK;
}

enum status
port_set_comm (port_id_t pid, port_comm_t comm)
{
  struct port *port = port_ptr (pid);

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (__port_set_comm (port, comm))
    port_update_iso ();

  return ST_OK;
}

enum status
port_set_igmp_snoop (port_id_t pid, bool_t enable)
{
  struct port *port = port_ptr (pid);
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  rc = CRP (cpssDxChBrgGenIgmpSnoopEnable
            (port->ldev, port->lport, !!enable));
  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
port_set_mru (uint16_t mru)
{
  int i;

  if (mru > 12000 || mru % 2)
    return ST_BAD_VALUE;

  for (i = 0; i < NPORTS; i++)
    if (!is_stack_port (&ports[i]))
      CRP (cpssDxChPortMruSet (ports[i].ldev, ports[i].lport, mru));

  return ST_OK;
}

enum status
port_set_pve_dst (port_id_t spid, port_id_t dpid, int enable)
{
  struct port *src = port_ptr (spid);
  GT_STATUS rc;

  if (!src)
    return ST_BAD_VALUE;

  if (is_stack_port (src))
    return ST_BAD_STATE;

  if (enable) {
    struct port *dst = port_ptr (dpid);

    if (!dst)
      return ST_BAD_VALUE;

    if (is_stack_port (dst))
      return ST_BAD_STATE;

    rc = CRP (cpssDxChBrgPrvEdgeVlanPortEnable
              (src->ldev, src->lport, !!enable,
               dst->lport, phys_dev (dst->ldev), GT_FALSE));
  } else {
    rc = CRP (cpssDxChBrgPrvEdgeVlanPortEnable
              (src->ldev, src->lport, !!enable, 0, 0, GT_FALSE));
  }

  switch (rc) {
  case GT_OK:       return ST_OK;
  case GT_HW_ERROR: return ST_HW_ERROR;
  default:          return ST_HEX;
  }
}

enum status
port_set_combo_preferred_media (port_id_t pid, combo_pref_media_t media)
{
  struct port *port = port_ptr (pid);
  uint16_t reg_val, page;

  if (port->type != PTYPE_COMBO) {
    return ST_BAD_VALUE;
  }

  CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x16, &page));

  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, 0x6));

  CRP (cpssDxChPhyPortSmiRegisterRead
       (port->ldev, port->lport, 0x14, &reg_val));

  switch (media) {
    case PREF_MEDIA_NONE :
      reg_val &= ~(1 << 5);
      reg_val &= ~(1 << 4);
      break;

    case PREF_MEDIA_RJ45 :
      reg_val &= ~(1 << 5);
      reg_val |= (1 << 4);
      break;

    case PREF_MEDIA_SFP :
      reg_val |= (1 << 5);
      reg_val &= ~(1 << 4);
      break;
  }
  reg_val |= (1 << 15);

  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x14, reg_val));

  CRP (cpssDxChPhyPortSmiRegisterWrite
       (port->ldev, port->lport, 0x16, page));

  return ST_OK;
}

enum status
port_tdr_test_start (port_id_t pid)
{
  struct port *port = port_ptr (pid);
  CPSS_VCT_CABLE_STATUS_STC st;
  GT_STATUS rc;
  GT_U16 page, info, val;
  int autoneg;

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (port->tdr_test_in_progress)
    return ST_NOT_READY;

  if (port->c_shutdown)
    return ST_BAD_STATE;
    
  CRP (cpssDxChPhyPortSmiRegisterRead
    (port->ldev, port->lport, 22, &page));
  CRP (cpssDxChPhyPortSmiRegisterWrite
    (port->ldev, port->lport, 22, 0x0000));
  cpssOsTimerWkAfter(1);
  /* get model info */
  CRP (cpssDxChPhyPortSmiRegisterRead
    (port->ldev, port->lport, 3, &info));
  
  switch(info & PRV_CPSS_PHY_MODEL_MASK)
  {
    case PRV_CPSS_DEV_E1340:
    
      /* we are on page 0 now */
      CRP (cpssDxChPhyPortSmiRegisterRead
        (port->ldev, port->lport, 0, &val));
      val |= 0x8000; /* soft reset */
      CRP (cpssDxChPhyPortSmiRegisterWrite
        (port->ldev, port->lport, 0, val));
      do
      {
        CRP (cpssDxChPhyPortSmiRegisterRead
          (port->ldev, port->lport, 0, &val));
      } while (val & 0x8000);
      
      /* autoneg */
      autoneg = ( (val & 0x1000) ? 1 : 0 );
      if (autoneg) /* if autoneg - disable it */
      {
        val &= (~ 0x1000);
        CRP (cpssDxChPhyPortSmiRegisterWrite
          (port->ldev, port->lport, 0, val));
      }
    
      /* going to run TDR test */
      CRP (cpssDxChPhyPortSmiRegisterWrite
        (port->ldev, port->lport, 22, 0x0007));
      cpssOsTimerWkAfter(1);
        
      CRP (cpssDxChPhyPortSmiRegisterRead
        (port->ldev, port->lport, 21, &val));
      val |=  0x1000  /* run VCT after breaking link */
            + 0x2000  /* disable cross pair check */
            + 0x0400; /* measure in meters */
      CRP (cpssDxChPhyPortSmiRegisterWrite
        (port->ldev, port->lport, 21, val));
        
      /* restore autoneg if it was enable */
      if (autoneg)
      {
        CRP (cpssDxChPhyPortSmiRegisterWrite
          (port->ldev, port->lport, 22, 0x0000));
        cpssOsTimerWkAfter(1);
        
        CRP (cpssDxChPhyPortSmiRegisterRead
          (port->ldev, port->lport, 0, &val));
        val |= 0x1000;
        CRP (cpssDxChPhyPortSmiRegisterWrite
          (port->ldev, port->lport, 0, val));
      }
        
      /* restore initial page */
      CRP (cpssDxChPhyPortSmiRegisterWrite
        (port->ldev, port->lport, 22, page));
      cpssOsTimerWkAfter(1);
           
      rc = GT_OK;
    
      break;
    default:
    
      /* restore initial page */
      CRP (cpssDxChPhyPortSmiRegisterWrite
        (port->ldev, port->lport, 22, page));
      cpssOsTimerWkAfter(1);
  
      rc = cpssVctCableStatusGet
        (port->ldev, port->lport, CPSS_VCT_START_E, &st);
        
      break;
  }
  
  switch (rc) {
  case GT_OK:
    port->tdr_test_in_progress = 1;
    return ST_OK;
  case GT_HW_ERROR:
    return ST_HW_ERROR;
  default:
    CRP (rc);
    return ST_HEX;
  }
}

enum status
port_tdr_test_get_result (port_id_t pid, struct vct_cable_status *cs)
{
  struct port *port = port_ptr (pid);
  CPSS_VCT_CABLE_STATUS_STC st;
  GT_STATUS rc;
  GT_U16 page, info, val, len, sta;
  int i;
  int is_e1340 = 0, autoneg = -1;

  if (!port)
    return ST_BAD_VALUE;

  if (is_stack_port (port))
    return ST_BAD_STATE;

  if (!port->tdr_test_in_progress)
    return ST_BAD_STATE;
  else if (port->c_shutdown)
    return ST_NOT_READY;
    
  CRP (cpssDxChPhyPortSmiRegisterRead
    (port->ldev, port->lport, 22, &page));
  CRP (cpssDxChPhyPortSmiRegisterWrite
    (port->ldev, port->lport, 22, 0x0000));
  cpssOsTimerWkAfter(1);
  /* get model info */
  CRP (cpssDxChPhyPortSmiRegisterRead
    (port->ldev, port->lport, 3, &info));
  
  switch(info & PRV_CPSS_PHY_MODEL_MASK)
  {
    case PRV_CPSS_DEV_E1340:
    
      is_e1340 = 1;
      
      /* autoneg */
      CRP (cpssDxChPhyPortSmiRegisterRead
        (port->ldev, port->lport, 0, &val));
      autoneg = ( (val & 0x1000) ? 1 : 0 );
      if (autoneg) /* if autoneg - disable it */
      {
        val &= (~ 0x1000);
        CRP (cpssDxChPhyPortSmiRegisterWrite
          (port->ldev, port->lport, 0, val));
      }
      
      /* checking whether the test is performed */
      CRP (cpssDxChPhyPortSmiRegisterWrite
         (port->ldev, port->lport, 22, 0x0007));
      cpssOsTimerWkAfter(1);
      
      rc = GT_NOT_READY;
      CRP (cpssDxChPhyPortSmiRegisterRead
           (port->ldev, port->lport, 21, &val));
      if (!(val & 0x0800)) /* test complete */
      {
        rc = GT_OK;
        cs->ok = 1;
        cs->phy_type = PT_1000;
        cs->length = CL_UNKNOWN;
        cs->npairs = 4;
        CRP (cpssDxChPhyPortSmiRegisterRead
           (port->ldev, port->lport, 20, &sta));
        for (i=0; i<4; i++)
        {
          CRP (cpssDxChPhyPortSmiRegisterRead
           (port->ldev, port->lport, 16+i, &len));
          /* magic value 6 */
          if (len<6) cs->pair_status[i].length = 0;
          else cs->pair_status[i].length = len-6;
          
          switch( (sta >> (i*4)) & 0x000F )
          {
            case 1:
              cs->pair_status[i].status = VS_NORMAL_CABLE;
              break;
            case 2:
              cs->pair_status[i].status = VS_OPEN_CABLE;
              break;
            case 3:
              cs->pair_status[i].status = VS_SHORT_CABLE;
              break;
            default:
              cs->pair_status[i].status = VS_TEST_FAILED;
              break;
          }
          if (cs->pair_status[i].status != VS_NORMAL_CABLE)
            cs->ok = 0;
        }
        
        /* going to make soft reset */
        CRP (cpssDxChPhyPortSmiRegisterWrite
          (port->ldev, port->lport, 22, 0x0000));
        cpssOsTimerWkAfter(1);
        
        CRP (cpssDxChPhyPortSmiRegisterRead
          (port->ldev, port->lport, 0, &val));
        val |= 0x8000; /* soft reset */
        CRP (cpssDxChPhyPortSmiRegisterWrite
          (port->ldev, port->lport, 0, val));
        do
        {
          CRP (cpssDxChPhyPortSmiRegisterRead
            (port->ldev, port->lport, 0, &val));
        } while (val & 0x8000);
      }
      
      /* restore autoneg if it was enable */
      if (autoneg)
      {
        CRP (cpssDxChPhyPortSmiRegisterWrite
          (port->ldev, port->lport, 22, 0x0000));
        cpssOsTimerWkAfter(1);
        
        CRP (cpssDxChPhyPortSmiRegisterRead
          (port->ldev, port->lport, 0, &val));
        val |= 0x1000;
        CRP (cpssDxChPhyPortSmiRegisterWrite
          (port->ldev, port->lport, 0, val));
      }
      
      /* restore initial page */
      CRP (cpssDxChPhyPortSmiRegisterWrite
           (port->ldev, port->lport, 22, page));
      cpssOsTimerWkAfter(1);
    
      break;
    default:
    
      /* restore initial page */
      CRP (cpssDxChPhyPortSmiRegisterWrite
        (port->ldev, port->lport, 22, page));
      cpssOsTimerWkAfter(1);
    
      rc = cpssVctCableStatusGet (port->ldev, port->lport,
                                CPSS_VCT_GET_RES_E, &st);
                                
      break;
  }
  
  switch (rc) {
  case GT_OK:
    port->tdr_test_in_progress = 0;
    if (!is_e1340)
      data_encode_vct_cable_status (cs, &st, IS_FE_PORT (pid - 1));
    DEBUG("port=%d ok=%d len=%d phy=%d n=%d autoneg=%d", pid, cs->ok, cs->length, cs->phy_type, cs->npairs, autoneg);
    for (i=0; i<cs->npairs; i++)
      DEBUG(" [St=%d L=%d]", cs->pair_status[i].status, cs->pair_status[i].length);
    DEBUG("\n");
    return ST_OK;
  case GT_NOT_READY:
    return ST_NOT_READY;
  case GT_HW_ERROR:
    return ST_HW_ERROR;
  default:
    CRP (rc);
    return ST_HEX;
  }
}

static void
__port_clear_translation (struct port *port)
{
  int i, trunk = port->mode == PM_TRUNK;

  for (i = 0; i < 4094; i++) {
    if (trunk && port->vlan_conf[i].xlate) {
      vid_t to = port->vlan_conf[i].map_to;

      if (port->vlan_conf[to - 1].refc) {
        vlans[to - 1].vt_refc -= port->vlan_conf[to - 1].refc;
        port->vlan_conf[to - 1].refc = 0;
        port_update_trunk_vlan (port, to);
      }

      port->vlan_conf[i].xlate  = 0;
      port->vlan_conf[i].map_to = 0;
    }
  }

  port->def_xlate  = 0;
  port->def_map_to = 0;
}

enum status
port_clear_translation (port_id_t pid)
{
  if (pid == ALL_PORTS) {
    int i;

    for (i = 0; i < nports; i++)
      __port_clear_translation (&ports[i]);
  } else {
    struct port *port = port_ptr (pid);

    if (!port)
      return ST_BAD_VALUE;

    __port_clear_translation (port);
  }

  pcl_port_clear_vt (pid);

  return ST_OK;
}

enum status
port_vlan_translate (port_id_t pid, vid_t from, vid_t to, int add)
{
  struct port *port = port_ptr (pid);
  int f_ix, t_ix;
  vid_t vids_to_upd[2];
  int n_to_upd = 0, i;
  int trunk;

  if (!port)
    return ST_BAD_VALUE;

  if (from != ALL_VLANS && !vlan_valid (from))
    return ST_BAD_VALUE;

  if (to != ALL_VLANS && !vlan_valid (to))
    return ST_BAD_VALUE;

  /* Default translation rules are allowed only when tunneling. */
  if (from == ALL_VLANS && to != ALL_VLANS && !vlan_xlate_tunnel)
    return ST_BAD_VALUE;

  f_ix = from - 1;

  if (add) {
    t_ix = to - 1;

    if (from) {
      /* Specific VLAN. */
      int map_to = port->vlan_conf[f_ix].map_to;

      if (map_to == to)
        return ST_OK;

      if (vlan_valid (map_to)) {
        --vlans[map_to - 1].vt_refc;
        --port->vlan_conf[map_to - 1].refc;
        vids_to_upd[n_to_upd++] = map_to;
      }

      port->vlan_conf[f_ix].xlate = 1;
      port->vlan_conf[f_ix].map_to = to;
      port->vlan_conf[t_ix].refc++;
      vlans[t_ix].vt_refc++;
      vids_to_upd[n_to_upd++] = to;
    } else {
      /* Default. */
      if (port->def_xlate) {
        if (port->def_map_to == to)
          return ST_OK;

        if (vlan_valid (port->def_map_to)) {
          vlans[port->def_map_to - 1].vt_refc--;
          port->vlan_conf[port->def_map_to - 1].refc--;
          vids_to_upd[n_to_upd++] = port->def_map_to;
        }
      }

      port->def_xlate = 1;
      port->def_map_to = to;
      if (vlan_valid (to)) {
        port->vlan_conf[t_ix].refc++;
        vlans[t_ix].vt_refc++;
        vids_to_upd[n_to_upd++] = to;
      }
    }
  } else {
    /* Disable translation. */
    if (from && port->vlan_conf[f_ix].xlate) {
      /* Specific VLAN. */
      to = port->vlan_conf[f_ix].map_to;
      t_ix = to - 1;
      port->vlan_conf[f_ix].xlate = 0;
      port->vlan_conf[f_ix].map_to = 0;
      port->vlan_conf[t_ix].refc--;
      vlans[t_ix].vt_refc--;
      vids_to_upd[n_to_upd++] = to;
    } else if (port->def_xlate) {
      /* Default. */
      if (port->def_map_to) {
        port->vlan_conf[port->def_map_to - 1].refc--;
        vlans[port->def_map_to - 1].vt_refc--;
        vids_to_upd[n_to_upd++] = port->def_map_to;
      }
      port->def_xlate = 0;
      port->def_map_to = 0;
    } else {
      /* Nothing to do. */
      return ST_OK;
    }
  }

  DEBUG ("we're here\r\n");

  /* Now apply changes if needed. */
  trunk = port->mode == PM_TRUNK;

  if (add) {
    enum status r = pcl_setup_vt (pid, from, to, vlan_xlate_tunnel, trunk);
    DEBUG ("PCL returned %d\r\n", r);
  } else
    pcl_remove_vt (pid, from, vlan_xlate_tunnel);

  if (trunk)
    for (i = 0; i < n_to_upd; i++)
      port_update_trunk_vlan_all_ports (vids_to_upd[i]);

  return ST_OK;
}

enum status
port_set_trunk_vlans (port_id_t pid, const uint8_t *bmp)
{
  struct port *port = port_ptr (pid);
  int i, trunk, allow;

  if (!port)
    return ST_BAD_VALUE;

  trunk = port->mode == PM_TRUNK;
  for (i = 1; i < 4095; i++) {
    allow = !!(bmp[i / 8] & (1 << (7 - (i % 8))));
    if (port->vlan_conf[i - 1].tallow != allow) {
      port->vlan_conf[i - 1].tallow = allow;
      if (trunk)
        port_update_trunk_vlan (port, i);
    }
  }

  return ST_OK;
}

enum status
port_enable_queue (port_id_t pid, uint8_t q, bool_t enable)
{
  GT_U8 d, p;
  GT_STATUS rc;

  if (q > 7)
    return ST_BAD_VALUE;

  if (pid == 0) {
    d = 0;
    p = CPSS_CPU_PORT_NUM_CNS;
  } else {
    struct port *port = port_ptr (pid);

    if (!port)
      return ST_DOES_NOT_EXIST;

    d = port->ldev;
    p = port->lport;
  }

  DEBUG ("%s queue %d on port %d:%d\r\n",
         enable ? "enable" : "disable", q, d, p);

  rc = CRP (cpssDxChPortTxQueueingEnableSet (d, p, q, gt_bool (enable)));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}

static enum status
__port_enable_eapol (struct port *port, bool_t enable)
{
  if (enable) {
    struct mac_age_arg aa = {
      .vid  = 0,
      .port = port->id
    };

    CRP (cpssDxChBrgFdbNaToCpuPerPortSet (port->ldev, port->lport, GT_FALSE));
    CRP (cpssDxChBrgPortEgrFltUnkEnable (port->ldev, port->lport, GT_TRUE));
    CRP (cpssDxChBrgFdbPortLearnStatusSet
         (port->ldev, port->lport, GT_FALSE, CPSS_LOCK_SOFT_DROP_E));

    mac_flush (&aa, GT_FALSE);
  } else {
    CRP (cpssDxChBrgFdbNaToCpuPerPortSet (port->ldev, port->lport, GT_TRUE));
    CRP (cpssDxChBrgPortEgrFltUnkEnable (port->ldev, port->lport, GT_FALSE));
    CRP (cpssDxChBrgFdbPortLearnStatusSet
         (port->ldev, port->lport, GT_FALSE, CPSS_LOCK_FRWRD_E));
  }

  return ST_OK;
}

enum status
port_enable_eapol (port_id_t pid, bool_t enable)
{
  struct port *port;

  port = port_ptr (pid);
  if (!port)
    return ST_BAD_VALUE;

  return __port_enable_eapol (port, enable);
}

enum status
port_eapol_auth (port_id_t pid, vid_t vid, mac_addr_t mac, bool_t auth)
{
  static const mac_addr_t zm = {0, 0, 0, 0, 0, 0};
  struct port *port;
  struct mac_op_arg op;

  port = port_ptr (pid);
  if (!port)
    return ST_BAD_VALUE;

  if (!vlan_valid (vid))
    return ST_BAD_VALUE;

  if (!memcmp (mac, zm, sizeof (zm)))
    return __port_enable_eapol (port, !auth);

  op.vid = vid;
  op.port = pid;
  op.drop = 0;
  op.delete = !auth;
  memcpy (op.mac, mac, sizeof (op.mac));

  return mac_op (&op);
}

static void
psec_lock (struct port *port)
{
  pthread_mutex_lock (&port->psec_lock);
}

static void
psec_unlock (struct port *port)
{
  pthread_mutex_unlock (&port->psec_lock);
}

enum status
psec_set_mode (port_id_t pid, psec_mode_t mode)
{
  struct port *port;
  enum status result = ST_OK;

  port = port_ptr (pid);
  if (!port)
    return ST_BAD_VALUE;

  if (!in_range (mode, PSECM_MAX_ADDRS, PSECM_LOCK))
    return ST_BAD_VALUE;

  psec_lock (port);

  if (port->psec_enabled) {
    result = ST_BAD_STATE;
    goto out;
  }

  port->psec_mode = mode;

 out:
  psec_unlock (port);
  return result;
}

enum status
psec_set_max_addrs (port_id_t pid, psec_max_addrs_t max)
{
  struct port *port;
  enum status result = ST_OK;

  port = port_ptr (pid);
  if (!port)
    return ST_BAD_VALUE;

  if (!in_range (max, PSEC_MIN_ADDRS, PSEC_MAX_ADDRS))
    return ST_BAD_VALUE;

  psec_lock (port);

  if (port->psec_enabled) {
    result = ST_BAD_STATE;
    goto out;
  }

  port->psec_max_addrs = max;

 out:
  psec_unlock (port);
  return result;
}

static void
__psec_disable_learning (struct port *port)
{
  CRP (cpssDxChBrgFdbNaToCpuPerPortSet
       (port->ldev, port->lport, GT_FALSE));

  switch (port->psec_action) {
  case PSECA_FORWARD:
    CRP (cpssDxChBrgFdbPortLearnStatusSet
         (port->ldev, port->lport, GT_FALSE, CPSS_LOCK_FRWRD_E));
    break;
  case PSECA_RESTRICT:
    CRP (cpssDxChBrgFdbPortLearnStatusSet
         (port->ldev, port->lport, GT_FALSE, CPSS_LOCK_DROP_E));
    CRP (cpssDxChBrgSecurBreachNaPerPortSet
         (port->ldev, port->lport, GT_TRUE));
  }
}

static void
__psec_enable_learning (struct port *port)
{
  CRP (cpssDxChBrgSecurBreachNaPerPortSet
       (port->ldev, port->lport, GT_FALSE));
  CRP (cpssDxChBrgFdbNaToCpuPerPortSet
       (port->ldev, port->lport, GT_TRUE));
  CRP (cpssDxChBrgFdbPortLearnStatusSet
       (port->ldev, port->lport, GT_FALSE, CPSS_LOCK_FRWRD_E));
}

static void
__psec_limit_reached (struct port *port)
{
  if (port->psec_enabled) {
    __psec_disable_learning (port);
  }
}

static void
__psec_limit_left (struct port *port)
{
  if (port->psec_enabled && port->psec_mode == PSECM_MAX_ADDRS) {
    __psec_enable_learning (port);
  }
}

enum psec_addr_status
psec_addr_check (struct fdb_entry *o, CPSS_MAC_ENTRY_EXT_STC *n)
{
  struct port *op = NULL, *np = NULL;
  enum psec_addr_status st;

  if (o->valid
      && o->me.key.entryType == CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E
      && o->me.dstInterface.type == CPSS_INTERFACE_PORT_E) {
    op = port_ptr (port_id (o->me.dstInterface.devPort.devNum,
                            o->me.dstInterface.devPort.portNum));
  }

  if (n->key.entryType == CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E
      && n->dstInterface.type == CPSS_INTERFACE_PORT_E) {
    np = port_ptr (port_id (n->dstInterface.devPort.devNum,
                            n->dstInterface.devPort.portNum));
  }

  if (np == op) {
    st = PAS_OK;
    goto out;
  }

  if (np) {
    psec_lock (np);

    st = PAS_OK;
    if (!np->psec_enabled) {
      np->psec_naddrs += 1;
      goto upd_old;
    } else {
      if (np->psec_naddrs >= np->psec_max_addrs) {
        st = PAS_LIMIT;
        goto out_unlock_np;
      }
      np->psec_naddrs += 1;
      if (np->psec_naddrs == np->psec_max_addrs)
        __psec_limit_reached (np);
    }
  }

 upd_old:
  if (op) {
    psec_lock (op);

    if (op->psec_naddrs-- == op->psec_max_addrs)
      __psec_limit_left (op);

    if (op->psec_naddrs < 0)
      op->psec_naddrs = 0;

    psec_unlock (op);
  }

 out_unlock_np:
  if (np)
    psec_unlock (np);
 out:
  return st;
}

void
psec_addr_del (CPSS_MAC_ENTRY_EXT_STC *o)
{
  struct port *op;

  if (o->key.entryType == CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E
      && o->dstInterface.type == CPSS_INTERFACE_PORT_E) {
    op = port_ptr (port_id (o->dstInterface.devPort.devNum,
                            o->dstInterface.devPort.portNum));
    if (op) {
      op->psec_naddrs -= 1;
    }
  }
}

void
psec_after_flush (void)
{
  int p;

  for (p = 0; p < nports; p++) {
    struct port *port = &ports[p];

    psec_lock (port);

    if (port->psec_naddrs < 0)
      port->psec_naddrs = 0;

    if (port->psec_enabled) {
      if (port->psec_naddrs < port->psec_max_addrs) {
        __psec_limit_left (port);
      }
    }

    psec_unlock (port);
  }
}


static void
__psec_enable (struct port *port)
{
  if (port->psec_enabled) {
    switch (port->psec_mode) {
    case PSECM_LOCK:
      __psec_disable_learning (port);
      break;
    case PSECM_MAX_ADDRS:
      if (port->psec_naddrs >= port->psec_max_addrs)
        __psec_disable_learning (port);
      else
        __psec_enable_learning (port);
    }
  } else {
    __psec_enable_learning (port);
  }
}

enum status
psec_enable (port_id_t pid, int enable, psec_action_t act, uint32_t trap_interval)
{
  struct port *port;
  int do_enable;

  port = port_ptr (pid);
  if (!port)
    return ST_BAD_VALUE;

  enable = !!enable;

  if (!in_range (act, PSECA_FORWARD, PSECA_RESTRICT))
    return ST_BAD_VALUE;

  if (trap_interval == 0)
    trap_interval = 30;
  if (!in_range (trap_interval, 1, 1000000))
    return ST_BAD_VALUE;

  psec_lock (port);

  do_enable = port->psec_enabled != enable;

  port->psec_enabled = enable;
  port->psec_action = act;
  port->psec_trap_interval = trap_interval;

  sec_port_na_delay_set (pid, trap_interval);
  sec_moved_static_delay_set (pid, trap_interval);
  sec_port_na_enable (port, gt_bool (enable && act == PSECA_RESTRICT));

  if (do_enable)
    __psec_enable (port);

  psec_unlock (port);

  return ST_OK;
}

enum status
psec_enable_na_sb (port_id_t pid, int enable)
{
  struct port *port;

  port = port_ptr (pid);
  if (!port)
    return ST_BAD_VALUE;

  psec_lock (port);

  if (enable) {
    if (port->psec_action == PSECA_RESTRICT
        && (port->psec_mode == PSECM_LOCK
            || (port->psec_mode == PSECM_MAX_ADDRS
                && port->psec_naddrs >= port->psec_max_addrs))) {
      CRP (cpssDxChBrgSecurBreachNaPerPortSet
           (port->ldev, port->lport, GT_TRUE));
    }
  } else {
    CRP (cpssDxChBrgSecurBreachNaPerPortSet
         (port->ldev, port->lport, GT_FALSE));
  }

  psec_unlock (port);

  return ST_OK;
}

enum status
port_get_serdes_cfg (port_id_t pid, struct port_serdes_cfg *cfg)
{
  struct port *port = port_ptr (pid);
  CPSS_DXCH_PORT_SERDES_CONFIG_STC c;
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  rc = CRP (cpssDxChPortSerdesConfigGet (port->ldev, port->lport, &c));
  if (rc == GT_OK) {
    cfg->txAmp                 = c.txAmp;
    cfg->txEmphEn              = c.txEmphEn == GT_TRUE;
    cfg->txEmphAmp             = c.txEmphAmp;
    cfg->txAmpAdj              = c.txAmpAdj;
    cfg->txEmphLevelAdjEnable  = c.txEmphLevelAdjEnable == GT_TRUE;
    cfg->ffeSignalSwingControl = c.ffeSignalSwingControl;
    cfg->ffeResistorSelect     = c.ffeResistorSelect;
    cfg->ffeCapacitorSelect    = c.ffeCapacitorSelect;

    return ST_OK;
  }

  return ST_HEX;
}

enum status
port_set_serdes_cfg (port_id_t pid, const struct port_serdes_cfg *cfg)
{
  struct port *port = port_ptr (pid);
  CPSS_DXCH_PORT_SERDES_CONFIG_STC c;
  GT_STATUS rc;

  if (!port)
    return ST_BAD_VALUE;

  c.txAmp                 = cfg->txAmp;
  c.txEmphEn              = gt_bool (cfg->txEmphEn);
  c.txEmphAmp             = cfg->txEmphAmp;
  c.txAmpAdj              = cfg->txAmpAdj;
  c.txEmphLevelAdjEnable  = gt_bool (cfg->txEmphLevelAdjEnable);
  c.ffeSignalSwingControl = cfg->ffeSignalSwingControl;
  c.ffeResistorSelect     = cfg->ffeResistorSelect;
  c.ffeCapacitorSelect    = cfg->ffeCapacitorSelect;

  rc = CRP (cpssDxChPortSerdesConfigSet (port->ldev, port->lport, &c));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}
