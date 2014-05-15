#ifndef __GIF_H__
#define __GIF_H__

#include <control-proto.h>

struct hw_port {
  uint8_t hw_dev;
  uint8_t hw_port;
};

extern void gif_init (void);
extern enum status gif_get_hw_port (struct hw_port *, uint8_t, uint8_t, uint8_t);


#endif /* __GIF_H__ */
