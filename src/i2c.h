#ifndef __PRESTERAMGR_I2C_H__
#define __PRESTERAMGR_I2C_H__

#include <errno.h>

static void __attribute__ ((unused))
i2c_slave (int fd, int addr)
{
  if (ioctl (fd, I2C_SLAVE, addr) < 0)
    fprintf (stderr, "I2C_SLAVE(0x%X): %s\n", addr, strerror (errno));
}

static void __attribute__ ((unused))
i2c_read (int fd, int addr, __u8 r, __u8 *v)
{
  struct i2c_msg msgs[2] = {
    {
      .addr = addr,
      .flags = 0,
      .len = 1,
      .buf = &r
    },
    {
      .addr = addr,
      .flags = I2C_M_RD,
      .len = 1,
      .buf = v
    }
  };
  struct i2c_rdwr_ioctl_data data = {
    .msgs  = msgs,
    .nmsgs = 2
  };

  if (ioctl (fd, I2C_RDWR, &data) < 0)
    fprintf (stderr, "I2C_READ(0x%X, 0x%X): %s\n",
             addr, r, strerror (errno));
}

static void __attribute__ ((unused))
i2c_write (int fd, int addr, __u8 r, __u8 v)
{
  __u8 buf[2] = {r, v};
  struct i2c_msg msgs[1] = {
    {
      .addr = addr,
      .flags = 0,
      .len = 2,
      .buf = buf
    }
  };
  struct i2c_rdwr_ioctl_data data = {
    .msgs  = msgs,
    .nmsgs = 1
  };

  if (ioctl (fd, I2C_RDWR, &data) < 0)
    fprintf (stderr, "I2C_WRITE(0x%X, 0x%X): %s\n",
             addr, r, strerror (errno));
}

#endif /* __PRESTERAMGR_I2C_H__ */
