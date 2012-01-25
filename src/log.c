#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <log.h>

void
log_init (void)
{
  openlog ("presteramgr", LOG_CONS | LOG_PERROR, LOG_DAEMON);
}
