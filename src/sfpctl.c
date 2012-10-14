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

int
main (int argc, char **argv)
{
  int fd;
  struct i2c_msg msgs[2];
  struct i2c_rdwr_ioctl_data data = {
    .msgs  = msgs,
    .nmsgs = 2
  };
  __u8 buf[2];

  fd = open ("/dev/i2c-0", O_RDWR);
  if (fd < 0) {
    perror ("open");
    exit (1);
  }

  int addr = 0x20;
  if (ioctl (fd, I2C_SLAVE, addr) < 0) {
    perror ("I2C_SLAVE");
    exit(1);
  }

  buf[0] = 6;
  buf[1] = 0;

  msgs[0].addr  = 0x20;
  msgs[0].flags = 0;
  msgs[0].len   = 1;
  msgs[0].buf   = buf;
  msgs[1].addr  = 0x20;
  msgs[1].flags = I2C_M_RD;
  msgs[1].len   = 1;
  msgs[1].buf   = buf + 1;
  if (ioctl (fd, I2C_RDWR, &data) < 0) {
    perror ("I2C_RDWR");
    exit (1);
  }

  buf[1] &= ~((1 << 2) | (1 << 3));

  msgs[0].addr  = 0x20;
  msgs[0].flags = 0;
  msgs[0].len   = 2;
  msgs[0].buf   = buf;
  data.nmsgs = 1;
  if (ioctl (fd, I2C_RDWR, &data) < 0) {
    perror ("I2C_RDWR");
    exit (1);
  }

  buf[0] = 2;
  buf[1] = 0;

  msgs[0].addr  = 0x20;
  msgs[0].flags = 0;
  msgs[0].len   = 1;
  msgs[0].buf   = buf;
  msgs[1].addr  = 0x20;
  msgs[1].flags = I2C_M_RD;
  msgs[1].len   = 1;
  msgs[1].buf   = buf + 1;
  if (ioctl (fd, I2C_RDWR, &data) < 0) {
    perror ("I2C_RDWR");
    exit (1);
  }

  buf[1] &= ~((1 << 2) | (1 << 3));

  msgs[0].addr  = 0x20;
  msgs[0].flags = 0;
  msgs[0].len   = 2;
  msgs[0].buf   = buf;
  data.nmsgs = 1;
  if (ioctl (fd, I2C_RDWR, &data) < 0) {
    perror ("I2C_RDWR");
    exit (1);
  }

  close (fd);

  return 0;
}
