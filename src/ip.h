#ifndef __IP_H__
#define __IP_H__

#include <control-proto.h>

extern enum status ip_cpss_lib_init (void);
extern enum status ip_start (void);
extern enum status ip_arp_trap_enable (int);

#endif /* __IP_H__ */
