#ifndef __MLL_H__
#define __MLL_H__

#include <control-proto.h>

extern enum status mll_init (void);
extern int mll_get (void);
extern int mll_put (int);

extern int add_node (int, mcg_t, vid_t);
extern int del_node (int, mcg_t, vid_t);

#endif /* __MLL_H__ */
