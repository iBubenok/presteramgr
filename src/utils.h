#ifndef __UTILS_H__
#define __UTILS_H__

#define ARRAY_SIZE(a) (sizeof (a) / sizeof (*a))
#define ON_GT_ERROR(rc, todo) ({ if ((rc) != GT_OK) todo; })

#endif /* __UTILS_H__ */
