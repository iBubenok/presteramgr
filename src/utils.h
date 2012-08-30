#ifndef __UTILS_H__
#define __UTILS_H__

#include <stddef.h>

#define ARRAY_SIZE(a) (sizeof (a) / sizeof (*a))
#define ON_GT_ERROR(rc) if ((rc) != GT_OK)

#define GET_BIT(bmp, n) (!!(bmp[n / sizeof (*bmp)] & (1 << (n % sizeof (*bmp)))))
#define SET_BIT(bmp, n) (bmp[n / sizeof (*bmp)] |= (1 << (n % sizeof (*bmp))))
#define CLR_BIT(bmp, n) (bmp[n / sizeof (*bmp)] &= ~(1 << (n % sizeof (*bmp))))

#define container_of(ptr, type, member)                     \
  ({                                                        \
    const typeof (((type *) 0)->member) *__mptr = (ptr);    \
    (type *) ((char *) __mptr - offsetof (type, member));   \
  })

#endif /* __UTILS_H__ */
