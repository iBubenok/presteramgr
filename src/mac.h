#ifndef __MAC_H__
#define __MAC_H__

#include <control-proto.h>


extern enum status mac_op (const struct mac_op_arg *);
extern enum status mac_set_aging_time (aging_time_t);

#endif /* __MAC_H__ */
