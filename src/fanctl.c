#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#include <i2c.h>

static void
badargs (void)
{
  fprintf (stderr, "Usage: fanctl <m|c|b> <on|off>\n");
  exit (1);
}

int
main (int argc, char **argv)
{
  int fd;
  __u8 mask = 0, crv = 0, orv = 0;
  int on = 0;

  if (argc != 3)
    badargs ();

  if (!strcmp (argv [1], "m"))
    mask = 1 << 6;
  else if (!strcmp (argv [1], "c"))
    mask = 1 << 7;
  else if (!strcmp (argv [1], "b"))
    mask = (1 << 6) | (1 << 7);
  else
    badargs ();

  if (!strcmp (argv [2], "on"))
    on = 1;
  else if (!strcmp (argv [2], "off"))
    on = 0;
  else
    badargs ();

  fd = open ("/dev/i2c-0", O_RDWR);
  if (fd < 0) {
    perror ("open");
    exit (1);
  }

  int addr = 0x26;
  i2c_slave (fd, addr);

  i2c_read (fd, addr, 7, &crv);
  crv &= ~((1 << 6) | (1 << 7));
  i2c_write (fd, addr, 7, crv);

  i2c_read (fd, addr, 3, &orv);
  if (on)
    orv |= mask;
  else
    orv &= ~mask;
  i2c_write (fd, addr, 3, orv);

  close (fd);

  return 0;
}
