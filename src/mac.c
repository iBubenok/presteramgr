#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <mac.h>
#include <port.h>

/*
 * TODO: maintain shadow FDB.
 */

enum status
mac_add_static (const struct mac_op_arg *arg)
{
  if (arg->vid < 1)
    return ST_BAD_VALUE;

  if (!port_valid (arg->port))
    return ST_BAD_VALUE;

  /* TODO: really do it */
  fprintf (stderr,
           "ADD STATIC MAC (AAAAAAA!):\r\n"
           "\tport = %d\r\n"
           "\tvlan = %d\r\n"
           "\taddr = %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           arg->port, arg->vid,
           arg->mac[0], arg->mac[1], arg->mac[2],
           arg->mac[3], arg->mac[4], arg->mac[5]);

  return ST_OK;
}
