#ifndef __RET_H__
#define __RET_H__

#include <route-p.h>

extern enum status ret_init (void);
extern enum status ret_add (const struct gw *, int);
extern enum status ret_unref (const struct gw *, int);

#endif /* __RET_H__ */