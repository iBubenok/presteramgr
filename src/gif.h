#ifndef __GIF_H__
#define __GIF_H__

#include <control-proto.h>
#include <sysdeps.h>

#define PORT_DEF(name) struct port_def name[NPORTS];

extern void gif_init (void);
extern enum status gif_get_hw_port (struct hw_port *, uint8_t, uint8_t, uint8_t);
extern enum status gif_get_hw_ports (struct port_def *);
extern enum status gif_set_hw_ports (uint8_t, uint8_t, const struct port_def *);

#endif /* __GIF_H__ */
