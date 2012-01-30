#ifndef __WNCT_H_
#define __WNCT_H_

#include <control-proto.h>

#define WNCT_STP  0x00
#define WNCT_GVRP 0x21

extern enum status wnct_enable_proto (uint8_t, int);
extern enum status wnct_start (void);

#endif /* __WNCT_H_ */
