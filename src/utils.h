#ifndef __UTILS_H__
#define __UTILS_H__

#include <cpssdefs.h>
#include <gtOs/gtOsGen.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <log.h>

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

static inline int
gt_bool (int val)
{
  return val ? GT_TRUE : GT_FALSE;
}

static inline void
err (const char *msg)
{
  int err = errno;
  DEBUG ("%s: %s (%d)\r\n", msg, strerror (err), err);
}

static inline void
errex (const char *msg)
{
  err (msg);
  exit (EXIT_FAILURE);
}

#endif /* __UTILS_H__ */
