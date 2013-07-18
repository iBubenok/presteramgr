#ifndef __DGASP_H__
#define __DGASP_H__

#include <control-proto.h>

extern int dgasp_init (void);
extern enum status dgasp_enable (int);
extern enum status dgasp_add_packet (size_t, const void *);
extern enum status dgasp_clear_packets (void);
extern enum status dgasp_port_op (port_id_t, int);

#endif /* __DGASP_H__ */
