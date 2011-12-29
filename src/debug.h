#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <extsvc.h>

#define DECLSHOW(type) extern const char *show_##type (type)
#define SHOW(type, value) (show_##type (value))

DECLSHOW (GT_STATUS);

#if (CPSS_RPRINT == 1)
#include <stdio.h>
#define CRP(call) ({                                                    \
      GT_STATUS __st = (call);                                          \
      if (__st != GT_OK)                                                \
        osPrintSync ("%s:%d: %s => %s (%04X)\r\n",                      \
                     __FILE__, __LINE__, #call,                         \
                     SHOW (GT_STATUS, __st), __st);                     \
      __st;                                                             \
    })
#elif (CPSS_RPRINT == 2)
#include <stdio.h>
#define CRP(call) ({                                 \
      GT_STATUS __st = (call);                       \
      osPrintSync ("%s:%d: %s => %s (%04X)\r\n",     \
                   __FILE__, __LINE__, #call,        \
                   SHOW (GT_STATUS, __st), __st);    \
      __st;                                          \
    })
#else
#define CRP(call) call
#endif /* CPSS_RPRINT */

#define CRPR(call) ({                           \
      GT_STATUS __st = CRP (call);              \
      if (__st != GT_OK)                        \
        return __st;                            \
    })

#endif /* __DEBUG_H__ */
