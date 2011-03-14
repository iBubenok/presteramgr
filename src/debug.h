#ifndef __DEBUG_H__
#define __DEBUG_H__

#if (CPSS_RPRINT == 1)
#include <stdio.h>
#define CRP(call) ({                            \
  GT_STATUS __st = (call);                      \
  if (__st != GT_OK)                            \
    osPrintSync ("%s:%d: %s => %04X\n",         \
             __FILE__, __LINE__, #call, __st);  \
  __st;                                         \
    })
#elif (CPSS_RPRINT == 2)
#include <stdio.h>
#define CRP(call) ({                            \
  GT_STATUS __st = (call);                      \
  osPrintSync ("%s:%d: %s => %04X\n",           \
           __FILE__, __LINE__, #call, __st);    \
  __st;                                         \
    })
#else
#define CRP(call) call
#endif /* CPSS_RPRINT */

#endif /* __DEBUG_H__ */
