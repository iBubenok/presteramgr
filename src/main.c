#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>

#include <cpss/extServices/os/gtOs/gtGenTypes.h>

#include <presteramgr.h>
#include <utils.h>
#include <debug.h>
#include <log.h>

extern GT_STATUS osStartEngine (int, const char **, const char *, GT_VOIDFUNCPTR);

static void
start (void)
{
  GT_STATUS rc;
  const char *argv[] = {
    "presteramgr"
  };
  int argc = ARRAY_SIZE (argv);

  rc = osStartEngine (argc, argv, "presteramgr", cpss_start);
  if (rc != GT_OK) {
    CRIT ("engine startup failed with %s (%d)\n", SHOW (GT_STATUS, rc), rc);
    exit (EXIT_FAILURE);
  }
}

int
main (int argc, char **argv)
{
  int daemonize = 1, debug = 0;

  while (1) {
    int c, option_index = 0;
    static struct option opts[] = {
      {"angel", 0, NULL, 'a'},
      {"debug", 0, NULL, 'd'},
      {0, 0, NULL, 0}
    };

    c = getopt_long (argc, argv, "ad", opts, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'a':
      daemonize = 0;
      break;
    case 'd':
      debug = 1;
      break;
    default:
      fprintf (stderr, "invalid arguments\n");
      exit (EXIT_FAILURE);
    }
  }

  if (daemonize)
    daemon (0, 0);

  log_init (daemonize, debug);
  start ();

  exit (EXIT_SUCCESS);
}
