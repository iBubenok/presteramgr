#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <extsvc.h>
#include <syslog.h>

#define DECLSHOW(type) extern const char *show_##type (type)
#define SHOW(type, value) (show_##type (value))

DECLSHOW (GT_STATUS);

#if (CPSS_RPRINT == 1)
#include <stdio.h>
#define CRP(status) ({                                                  \
      GT_STATUS __st = (status);                                        \
      if (__st != GT_OK)                                                \
        osPrintSync ("<C>:%s:%d: %s (%04X)\r\n",                        \
                     __FILE__, __LINE__,                                \
                     SHOW (GT_STATUS, __st), __st);                     \
      __st;                                                             \
    })
#elif (CPSS_RPRINT == 2)
#include <stdio.h>
#define CRP(status) ({                                 \
      GT_STATUS __st = (status);                       \
      osPrintSync ("<C>:%s:%d: %s (%04X)\r\n",         \
                   __FILE__, __LINE__,                 \
                   SHOW (GT_STATUS, __st), __st);      \
      __st;                                            \
    })
#else
#define CRP(status) (status)
#endif /* CPSS_RPRINT */

#define CRPR(status) ({                           \
      GT_STATUS __st = CRP (status);              \
      if (__st != GT_OK)                          \
        return __st;                              \
    })

#endif /* __DEBUG_H__ */
