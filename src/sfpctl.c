#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>

static void
clear_bits (int fd, int addr, __u8 cr, __u8 or, __u8 mask)
{
  struct i2c_msg msgs[2];
  struct i2c_rdwr_ioctl_data data = {
    .msgs  = msgs,
    .nmsgs = 2
  };
  __u8 buf[2];

  if (ioctl (fd, I2C_SLAVE, addr) < 0) {
    fprintf (stderr, "I2C_SLAVE(0x%X): %s\n", addr, strerror (errno));
    return;
  }

  buf[0] = cr;
  buf[1] = 0;

  msgs[0].addr  = addr;
  msgs[0].flags = 0;
  msgs[0].len   = 1;
  msgs[0].buf   = buf;
  msgs[1].addr  = addr;
  msgs[1].flags = I2C_M_RD;
  msgs[1].len   = 1;
  msgs[1].buf   = buf + 1;
  if (ioctl (fd, I2C_RDWR, &data) < 0) {
    fprintf (stderr, "I2C_READ(0x%X, 0x%X): %s\n", addr, cr, strerror (errno));
    return;
  }

  buf[1] &= ~mask;

  msgs[0].addr  = addr;
  msgs[0].flags = 0;
  msgs[0].len   = 2;
  msgs[0].buf   = buf;
  data.nmsgs = 1;
  if (ioctl (fd, I2C_RDWR, &data) < 0) {
    fprintf (stderr, "I2C_WRITE(0x%X, 0x%X): %s\n", addr, cr, strerror (errno));
    return;
  }

  buf[0] = or;
  buf[1] = 0;

  msgs[0].addr  = addr;
  msgs[0].flags = 0;
  msgs[0].len   = 1;
  msgs[0].buf   = buf;
  msgs[1].addr  = addr;
  msgs[1].flags = I2C_M_RD;
  msgs[1].len   = 1;
  msgs[1].buf   = buf + 1;
  if (ioctl (fd, I2C_RDWR, &data) < 0) {
    fprintf (stderr, "I2C_READ(0x%X, 0x%X): %s\n", addr, or, strerror (errno));
    return;
  }

  buf[1] &= ~mask;

  msgs[0].addr  = addr;
  msgs[0].flags = 0;
  msgs[0].len   = 2;
  msgs[0].buf   = buf;
  data.nmsgs = 1;
  if (ioctl (fd, I2C_RDWR, &data) < 0) {
    fprintf (stderr, "I2C_WRITE(0x%X, 0x%X): %s\n", addr, or, strerror (errno));
    return;
  }
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

  if (hw_type == 10 || hw_type == 11 || hw_type == 20)
    clear_bits (fd, 0x20, 6, 2, (1 << 2) | (1 << 3));
  else if (hw_type == 12) {
    switch (hw_subtype) {
    case 0:
    case 1:
      goto out;
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
  }

 out:
  close (fd);

  return 0;
}
