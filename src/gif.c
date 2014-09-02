#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <gif.h>
#include <port.h>
#include <stack.h>
#include <utils.h>
#include <dev.h>
#include <mgmt.h>
#include <debug.h>
#include <sysdeps.h>
#include <assert.h>

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/networkIf/cpssDxChNetIf.h>

struct dev_ports {
  int n_total;
  int n_by_type[GIFT_PORT_TYPES];
  struct {
    struct gif_id id;
    union {
      struct port *local;
      struct hw_port remote;
    };
  } port[64];
};

static struct dev_ports __dev_ports[16];

void
gif_init (void)
{
  struct dev_ports *dp;
  int i;

  memset (__dev_ports, 0, sizeof (__dev_ports));

  dp = (stack_id == 0) ? &__dev_ports[0] : &__dev_ports[stack_id - 1];

  for (i = 0; i < nports; i++) {
    if (IS_FE_PORT (i))
      dp->port[i].id.type = GIFT_FE;
    else if (IS_GE_PORT (i))
      dp->port[i].id.type = GIFT_GE;
    else if (IS_XG_PORT (i))
      dp->port[i].id.type = GIFT_XG;
    dp->port[i].id.dev = stack_id;
    dp->port[i].id.num = ++dp->n_by_type[dp->port[i].id.type];
    dp->port[i].local = &ports[i];
    dp->n_total++;
  }
}

enum status
gif_get_hw_port (struct hw_port *hp, uint8_t type, uint8_t dev, uint8_t num)
{
  int i, o = 0, local;

  if (!in_range (type, GIFT_FE, GIFT_XG) ||
      !in_range (dev, 0, 16))
    return ST_BAD_VALUE;

  if (stack_id == 0) {
    if (dev != 0)
      return ST_BAD_VALUE;

    local = 1;
  } else {
    if (dev == 0)
      dev = stack_id;

    local = dev == stack_id;
    dev -= 1;
  }

  if (num > __dev_ports[dev].n_by_type[type])
    return ST_DOES_NOT_EXIST;

  for (i = 0; i < type; i++)
    o += __dev_ports[dev].n_by_type[i];
  o += num - 1;

  assert (o < __dev_ports[dev].n_total);

  if (local) {
    hp->hw_dev  = phys_dev (__dev_ports[dev].port[o].local->ldev);
    hp->hw_port = __dev_ports[dev].port[o].local->lport;
  } else {
    hp->hw_dev  = __dev_ports[dev].port[o].remote.hw_dev;
    hp->hw_port = __dev_ports[dev].port[o].remote.hw_port;
  }

  return ST_OK;
}

enum status
gif_tx (const struct gif_id *id,
        const struct gif_tx_opts *opts,
        uint16_t size,
        const void *data)
{
  CPSS_DXCH_NET_DSA_PARAMS_STC tp;
  uint8_t tag[8];
  enum status result;
  struct hw_port hp;

  result = gif_get_hw_port (&hp, id->type, id->dev, id->num);
  if (result != ST_OK)
    return result;

  memset (&tp, 0, sizeof (tp));
  tp.commonParams.dsaTagType = CPSS_DXCH_NET_DSA_TYPE_EXTENDED_E;
  tp.commonParams.vid = opts->vid;
  tp.commonParams.vpt = 7;
  tp.dsaType = CPSS_DXCH_NET_DSA_CMD_FROM_CPU_E;
  tp.dsaInfo.fromCpu.tc = 7;
  tp.dsaInfo.fromCpu.dstInterface.type = CPSS_INTERFACE_PORT_E;
  tp.dsaInfo.fromCpu.dstInterface.devPort.devNum = hp.hw_dev;
  tp.dsaInfo.fromCpu.dstInterface.devPort.portNum = hp.hw_port;
  tp.dsaInfo.fromCpu.cascadeControl = gt_bool (opts->ignore_stp);
  tp.dsaInfo.fromCpu.srcDev = stack_id;
  tp.dsaInfo.fromCpu.srcId = stack_id;
  CRP (cpssDxChNetIfDsaTagBuild (CPU_DEV, &tp, tag));

  mgmt_send_gen_frame (tag, data, size);

  return ST_OK;
}

enum status
gif_get_hw_ports (struct port_def *pd)
{
  struct dev_ports *dp;
  int i;

  dp = (stack_id == 0) ? &__dev_ports[0] : &__dev_ports[stack_id - 1];

  for (i = 0; i < dp->n_total; i++) {
    memcpy (&pd[i].id, &dp->port[i].id, sizeof (struct gif_id));
    pd[i].hp.hw_dev = phys_dev (dp->port[i].local->ldev);
    pd[i].hp.hw_port = dp->port[i].local->lport;
    pd[i].hp.sr = dp->port[i].local->stack_role;
  }

  return ST_OK;
}

static void __attribute__ ((unused))
gif_dump_hw_ports (void)
{
  int d, p;

  DEBUG ("\r\n*** BEGIN KNOWN PORT DUMP ***\r\n");
  for (d = 0; d < 16; d++) {
    struct dev_ports *dp = &__dev_ports[d];

    for (p = 0; p < dp->n_total; p++) {
      char *t, *r;
      int hd, hp, sr;

      switch (dp->port[p].id.type) {
      case GIFT_FE: t = "fastethernet"; break;
      case GIFT_GE: t = "gigabitethernet"; break;
      case GIFT_XG: t = "tengigabitethernet"; break;
      default:      t = "unknown";
      }

      if ((d == 0 && stack_id == 0) || d == stack_id - 1) {
        hd = phys_dev (dp->port[p].local->ldev);
        hp = dp->port[p].local->lport;
        sr = dp->port[p].local->stack_role;
      } else {
        hd = dp->port[p].remote.hw_dev;
        hp = dp->port[p].remote.hw_port;
        sr = dp->port[p].remote.sr;
      }

      switch (sr) {
      case PSR_PRIMARY:   r = "primary"; break;
      case PSR_SECONDARY: r = "secondary"; break;
      case PSR_NONE:      r = "none"; break;
      default:            r = "unknown";
      }

      DEBUG ("%s %d/%d, %d:%d, %s\r\n",
             t, dp->port[p].id.dev, dp->port[p].id.num,
             hd, hp, r);
    }
  }
  DEBUG ("\r\n*** END KNOWN PORT DUMP ***\r\n");
}

enum status
gif_set_hw_ports (uint8_t dev, uint8_t n, const struct port_def *pd)
{
  struct dev_ports *dp;
  int i;

  if (!stack_active ()
      || !in_range (dev, 1, 16)
      || dev == stack_id
      || !in_range (n, 0, 60))
    return ST_BAD_VALUE;

  dp = &__dev_ports[dev - 1];
  memset (dp, 0, sizeof (*dp));
  for (i = 0; i < n; i++) {
    memcpy (&dp->port[i].id, &pd[i].id, sizeof (struct gif_id));
    memcpy (&dp->port[i].remote, &pd[i].hp, sizeof (struct hw_port));
    dp->n_by_type[pd[i].id.type]++;
    dp->n_total++;
  }

  return ST_OK;
}
