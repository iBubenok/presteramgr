#ifndef __MLL_H__
#define __MLL_H__

#include <control-proto.h>

extern enum status mll_init (void);
extern int mll_get (void);
extern int mll_put (int);

#endif /* __MLL_H__ */
