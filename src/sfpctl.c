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
clear_bits (int fd, int addr, __u8 cr, __u8 or, __u8 mask)
{
  __u8 crv = 0, orv = 0;

  i2c_slave (fd, addr);

  i2c_read (fd, addr, cr, &crv);
  fprintf (stderr, "reg %d = %02X\n", cr, crv);
  crv &= ~mask;
  fprintf (stderr, "set reg %d = %02X\n", cr, crv);
  i2c_write (fd, addr, cr, crv);

  i2c_read (fd, addr, or, &orv);
  fprintf (stderr, "reg %d = %02X\n", or, orv);
  orv &= ~mask;
  fprintf (stderr, "set reg %d = %02X\n", or, orv);
  i2c_write (fd, addr, or, orv);
}

static void
set_bits (int fd, int addr, __u8 cr, __u8 mask)
{
  __u8 crv = 0;

  i2c_slave (fd, addr);

  i2c_read (fd, addr, cr, &crv);
  crv |= mask;
  i2c_write (fd, addr, cr, crv);
}


int
main (int argc, char **argv)
{
  int fd, addr, start, stop = 0x25;
  char *val;
  int hw_type = 0, hw_subtype = 0;


  if ((val = getenv ("HW_TYPE")))
    hw_type = atoi (val);
  else {
    fprintf (stderr, "HW_TYPE not defined\n");
    exit (1);
  }

  if ((val = getenv ("HW_SUBTYPE")))
    hw_subtype = atoi (val);
  else
    fprintf (stderr, "HW_SUBTYPE not defined, assuming default\n");

  fprintf (stderr, "enable SFP for %d:%d\n", hw_type, hw_subtype);

  fd = open ("/dev/i2c-0", O_RDWR);
  if (fd < 0) {
    perror ("open");
    exit (1);
  }

  if (hw_type == 23 || hw_type == 26) {
//    clear_bits (fd, 0x20, 6, 2, (1 << 3) | (1 << 7));
    clear_bits (fd, 0x20, 7, 3, (1 << 3) | (1 << 7));
  } else if (hw_type == 10)
    clear_bits (fd, 0x20, 6, 2, (1 << 2) | (1 << 3));
  else if (hw_type == 20 || hw_type == 11) {
    clear_bits (fd, 0x20, 6, 2, (1 << 2) | (1 << 3) | (1 << 6));
    set_bits (fd, 0x20, 2, (1 << 6));
    set_bits (fd, 0x20, 6, (1 << 0) | (1 << 1) | (1 << 4) | (1 << 5));
    clear_bits (fd, 0x20, 7, 3, (1 << 2) | (1 << 3));
    set_bits (fd, 0x20, 7, (1 << 0) | (1 << 1) | (1 << 4) | (1 << 5));
  } else if (hw_type == 24 || hw_type == 25) {
    clear_bits (fd, 0x20, 7, 3, (1 << 3) | (1 << 7));
  } else if (hw_type == 12) {
    switch (hw_subtype) {
    case 0:
    case 1:
      start = stop = 0x26;
      break;
    case 2:
    case 3:
      start = 0x20;
      break;
    case 4:
      start = 0x23;
      break;
    default:
      fprintf (stderr, "hw subtype %d not supported\n", hw_subtype);
      exit (1);
    }

    for (addr = start; addr <= stop; addr++) {
      clear_bits (fd, addr, 6, 2, (1 << 0) | (1 << 3));
      clear_bits (fd, addr, 7, 3, (1 << 0) | (1 << 3));
    }
    set_bits (fd, 0x26, 6, (1 << 1) | (1 << 2));
    set_bits (fd, 0x26, 7, (1 << 1) | (1 << 2));
  }

  close (fd);

  fprintf(stderr, "%s: done\n", argv[0]);
  return 0;
}
