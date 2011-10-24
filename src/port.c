#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <presteramgr.h>
#include <debug.h>
#include <sysdeps.h>
#include <port.h>


#include <cpss/dxCh/dxChxGen/port/cpssDxChPortCtrl.h>
#include <cpss/generic/config/private/prvCpssConfigTypes.h>

#include <stdlib.h>
#include <assert.h>


DECLSHOW (CPSS_PORT_SPEED_ENT);
DECLSHOW (CPSS_PORT_DUPLEX_ENT);

struct port *ports = NULL;
int nports = 0;

static int *port_nums;

static int
port_num (GT_U8 ldev, GT_U8 lport)
{
  assert (ldev < NDEVS);
  assert (lport < CPSS_MAX_PORTS_NUM_CNS);
  assert (port_nums);
  return port_nums[ldev * CPSS_MAX_PORTS_NUM_CNS + lport];
}

int
port_init (void)
{
  int i;

  port_nums = malloc (NDEVS * CPSS_MAX_PORTS_NUM_CNS * sizeof (int));
  assert (port_nums);
  for (i = 0; i < NDEVS * CPSS_MAX_PORTS_NUM_CNS; i++)
    port_nums[i] = -1;

  ports = calloc (NPORTS, sizeof (struct port));
  assert (ports);
  for (i = 0; i < NPORTS; i++) {
    ports[i].ldev = 0;
    ports[i].lport = i;
    port_nums[ports[i].ldev * CPSS_MAX_PORTS_NUM_CNS + ports[i].lport] = i;
  }
  nports = NPORTS;

  return 0;
}

GT_STATUS
port_set_sgmii_mode (int n)
{
  GT_U8 dev, port;

  assert (port_valid (n));
  dev = ports[n].ldev;
  port = ports[n].lport;

  CRPR (cpssDxChPortInterfaceModeSet
        (dev, port, CPSS_PORT_INTERFACE_MODE_SGMII_E));
  CRPR (cpssDxChPortSpeedSet (dev, port, CPSS_PORT_SPEED_1000_E));
  CRPR (cpssDxChPortSerdesPowerStatusSet
        (dev, port, CPSS_PORT_DIRECTION_BOTH_E, 0x01, GT_TRUE));
  CRPR (cpssDxChPortInbandAutoNegEnableSet (dev, port, GT_TRUE));

  return GT_OK;
}

int
port_exists (GT_U8 dev, GT_U8 port)
{
  return CPSS_PORTS_BMP_IS_PORT_SET_MAC
    (&(PRV_CPSS_PP_MAC (dev)->existingPorts), port);
}

void
port_handle_link_change (GT_U8 ldev, GT_U8 lport)
{
  CPSS_PORT_ATTRIBUTES_STC attrs;
  GT_STATUS rc;
  int p;
  struct port *port;

  if ((p = port_num (ldev, lport)) < 0)
    return;
  port = &ports[p];

  rc = CRP (cpssDxChPortAttributesOnPortGet (ldev, lport, &attrs));
  if (rc != GT_OK)
    return;

  if (attrs.portLinkUp    != port->state.attrs.portLinkUp ||
      attrs.portSpeed     != port->state.attrs.portSpeed  ||
      attrs.portDuplexity != port->state.attrs.portDuplexity) {
    port->state.attrs = attrs;
    if (attrs.portLinkUp)
      osPrintSync ("port %2d link up at %s, %s\n", p,
                   SHOW (CPSS_PORT_SPEED_ENT, attrs.portSpeed),
                   SHOW (CPSS_PORT_DUPLEX_ENT, attrs.portDuplexity));
    else
      osPrintSync ("port %2d link down\n", p);
  }
}
