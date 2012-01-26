#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <log.h>

void
log_init (int daemon, int debug)
{
  openlog ("presteramgr", LOG_CONS | (daemon ? 0 : LOG_PERROR), LOG_DAEMON);
  setlogmask (LOG_UPTO (debug ? LOG_DEBUG : LOG_INFO));
}
