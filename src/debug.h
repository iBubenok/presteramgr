#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <extsvc.h>
#include <log.h>

#define DECLSHOW(type) extern const char *show_##type (type)
#define SHOW(type, value) (show_##type (value))

DECLSHOW (GT_STATUS);

extern int log_cpss_errors;

#define CRP(status) ({                                                  \
      GT_STATUS __st = (status);                                        \
      if (log_cpss_errors && __st != GT_OK)                             \
        DEBUG ("GT_STATUS: %s (%04X)\n", SHOW (GT_STATUS, __st), __st); \
      __st;                                                             \
    })

#define CRPR(status) ({                           \
      GT_STATUS __st = CRP (status);              \
      if (__st != GT_OK)                          \
        return __st;                              \
    })

#endif /* __DEBUG_H__ */
