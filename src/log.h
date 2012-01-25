#ifndef __LOG_H__
#define __LOG_H__

#include <syslog.h>

extern void log_init (void);

#define LOG(level, format, arg...)                          \
  syslog (level, "%s:%d: " format, __FILE__, __LINE__, ##arg)

#define EMERG(format, arg...)   LOG (LOG_EMERG, format, ##arg)
#define ALERT(format, arg...)   LOG (LOG_ALERT, format, ##arg)
#define CRIT(format, arg...)    LOG (LOG_CRIT, format, ##arg)
#define ERR(format, arg...)     LOG (LOG_ERR, format, ##arg)
#define WARNING(format, arg...) LOG (LOG_WARNING, format, ##arg)
#define NOTICE(format, arg...)  LOG (LOG_NOTICE, format, ##arg)
#define INFO(format, arg...)    LOG (LOG_INFO, format, ##arg)
#define DEBUG(format, arg...)   LOG (LOG_DEBUG, format, ##arg)

#endif /* __LOG_H__ */
