#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <control-proto.h>
#include <control-utils.h>
#include <debug.h>


int
control_handler (zloop_t *loop, zmq_pollitem_t *pi, void *handler_data)
{
  zmsg_t *msg;
  command_t cmd;
  cmd_handler_t handler;
  enum status result;
  struct handler_data *hdata = handler_data;

  DEBUG ("we're here");

  msg = zmsg_recv (hdata->sock);

  result = pop_size (&cmd, msg, sizeof (cmd), 0);
  if (result != ST_OK) {
    __report_status (result, hdata->sock);
    goto out;
  }

  if (cmd >= hdata->nhandlers ||
      (handler = hdata->handlers[cmd]) == NULL) {
    __report_status (ST_BAD_REQUEST, hdata->sock);
    goto out;
  }
  handler (msg, hdata->sock);

 out:
  zmsg_destroy (&msg);
  return 0;
}
