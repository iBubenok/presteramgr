#ifndef __NHT_H__
#define __NHT_H__

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>

#include <control-proto.h>

extern enum status nht_init (void);
extern int nht_add (const GT_ETHERADDR *addr);
extern enum status nht_unref (const GT_ETHERADDR *addr);

#endif /* __NHT_H__ */
