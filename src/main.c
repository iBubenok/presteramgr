#include <cpss/extServices/os/gtOs/gtGenTypes.h>

#include <presteramgr.h>
#include <debug.h>
#include <log.h>

extern GT_STATUS osStartEngine (int, const char **, const char *, GT_VOIDFUNCPTR);

int
main (int argc, char **argv)
{
  GT_STATUS rc;

  log_init ();

  rc = CRP (osStartEngine (argc, (const char **) argv,
                           "presteramgr", cpss_start));

  return (rc == GT_OK) ? 0 : 1;
}
