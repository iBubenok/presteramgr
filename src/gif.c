#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <gif.h>
#include <port.h>
#include <stack.h>
#include <utils.h>
#include <dev.h>
#include <sysdeps.h>
#include <assert.h>


struct dev_ports {
  int n_total;
  int n_by_type[GIFT_PORT_TYPES];
  union {
    struct port *local;
    struct hw_port remote;
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
    dp->port[i].local = &ports[i];
    dp->n_total++;
    if (IS_FE_PORT (i))
      dp->n_by_type[GIFT_FE]++;
    else if (IS_GE_PORT (i))
      dp->n_by_type[GIFT_GE]++;
    else if (IS_XG_PORT (i))
      dp->n_by_type[GIFT_XG]++;
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
    return ST_BAD_VALUE;

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


