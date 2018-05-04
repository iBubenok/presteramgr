#include <ipsg.h>
#include <debug.h>
#include <control-utils.h>
#include <sys/prctl.h>
#include <pcl.h>

void *ipsg_sub_sock;
void *ipsg_req_sock;

DECLARE_HANDLER (RULE_SET);
DECLARE_HANDLER (RULE_UNSET);
DECLARE_HANDLER (TRAP_ENABLE);
DECLARE_HANDLER (TRAP_DISABLE);
DECLARE_HANDLER (DROP_ENABLE);
DECLARE_HANDLER (DROP_DISABLE);


static cmd_handler_t ipsg_rule_handlers[] = {
  HANDLER (RULE_SET),
  HANDLER (RULE_UNSET),
  HANDLER (TRAP_ENABLE),
  HANDLER (TRAP_DISABLE),
  HANDLER (DROP_ENABLE),
  HANDLER (DROP_DISABLE)
};


int
ipsg_req_handler (zloop_t *loop, zsock_t *reader, void *handler_data)
{
  zmsg_t *msg;
  command_t cmd;
  cmd_handler_t handler;
  enum status result;
  struct handler_data *hdata = handler_data;

  DEBUG("%s\n",__FUNCTION__ );
  msg = zmsg_recv (hdata->sock);

  result = pop_size (&cmd, msg, sizeof (cmd), 0);
  if (result != ST_OK) {
    __report_status (result, hdata->sock);
    goto out;
  }

  if (cmd >= hdata->nhandlers ||
      (handler = hdata->handlers[cmd]) == NULL) {
    __report_status (ST_BAD_REQUEST, hdata->sock);
    DEBUG("ST_BAD_REQUEST\n");
    goto out;
  }
  handler (msg, hdata->sock);

 out:
  zmsg_destroy (&msg);
  return 0;
}


static void *
ipsg_loop (void *dummy)
{
  zloop_t *loop = zloop_new ();

  struct handler_data ipsg_rcmd_hd = { ipsg_req_sock,
                                       ipsg_rule_handlers, ARRAY_SIZE (ipsg_rule_handlers) };
  zloop_reader (loop, ipsg_req_sock, ipsg_req_handler, &ipsg_rcmd_hd);

  prctl(PR_SET_NAME, "ipsg-loop", 0, 0, 0);

  zloop_start (loop);

  return NULL;
}

int
ipsg_start (void)
{
  pthread_t tid;
  pthread_create (&tid, NULL, ipsg_loop, NULL);
  return 0;
}

int
ipsg_init (void)
{
  DEBUG("%s\n",__FUNCTION__ );
  int rc;

  ipsg_sub_sock = zsock_new (ZMQ_PUB);
  assert (ipsg_sub_sock);
  rc = zsock_bind (ipsg_sub_sock, IPSG_REP_EP);
  assert (rc == 0);

  ipsg_req_sock = zsock_new (ZMQ_REP);
  assert (ipsg_req_sock);
  rc = zsock_bind (ipsg_req_sock, IPSG_REQ_EP);
  assert (rc == 0);
  return 0;
}

DEFINE_HANDLER (RULE_SET)
{
  DEBUG ("%s\n",__FUNCTION__);
  zmsg_t *reply;
  mac_addr_t mac;
  ip_addr_t ip;
  vid_t vid;
  port_id_t pi;
  bool_t verify_mac;
  enum status result;
  uint16_t rule_ix = 0;

  result = POP_ARG (&pi);
  if (result != ST_OK) {
    report_status (result);
    return;
  }
  result = POP_ARG (&mac);
  if (result != ST_OK) {
    report_status (result);
    return;
  }
  result = POP_ARG (&vid);
  if (result != ST_OK) {
    report_status (result);
    return;
  }
  result = POP_ARG (&ip);
  if (result != ST_OK) {
    report_status (result);
    return;
  }
  result = POP_ARG (&verify_mac);
  if (result != ST_OK) {
    report_status (result);
    return;
  }

  pcl_source_guard_rule_set ( pi,
                              mac,
                              vid,
                              ip,
                              verify_mac,
                              &rule_ix);

  reply = make_reply (ST_OK);
  zmsg_addmem (reply, &rule_ix, sizeof(uint16_t));
  send_reply (reply);
}

DEFINE_HANDLER (RULE_UNSET)
{
  port_id_t pi;
  uint16_t rule_ix;
  enum status result;

  DEBUG("%s\n",__FUNCTION__);
  result = POP_ARG (&pi);
  if (result != ST_OK)
    goto out;
  result = POP_ARG (&rule_ix);
  if (result != ST_OK)
    goto out;

  pcl_source_guard_rule_unset ( pi,
                                rule_ix);

 out:
  report_status (result);
}

DEFINE_HANDLER (TRAP_ENABLE)
{
  port_id_t pi;
  enum status result;

  DEBUG("%s\n",__FUNCTION__);
  result = POP_ARG (&pi);
  if (result != ST_OK)
    goto out;

  pcl_source_guard_trap_enable (pi);

 out:
  report_status (result);
}

DEFINE_HANDLER (TRAP_DISABLE)
{
  port_id_t pi;
  enum status result;

  DEBUG("%s\n",__FUNCTION__);
  result = POP_ARG (&pi);
  if (result != ST_OK)
    goto out;

  pcl_source_guard_trap_disable (pi);

 out:
  report_status (result);

}

DEFINE_HANDLER (DROP_ENABLE)
{
  port_id_t pi;
  enum status result;

  DEBUG("%s\n",__FUNCTION__);
  result = POP_ARG (&pi);
  if (result != ST_OK)
    goto out;


  pcl_source_guard_drop_enable (pi);

 out:
  report_status (result);
}

DEFINE_HANDLER (DROP_DISABLE)
{
  port_id_t pi;
  enum status result;

  DEBUG("%s\n",__FUNCTION__);
  result = POP_ARG (&pi);
  if (result != ST_OK)
    goto out;

  pcl_source_guard_drop_disable (pi);

 out:
  report_status (result);

}

zmsg_t *
make_notification (enum ntf_frame_t code)
{
  zmsg_t *msg = zmsg_new ();
  assert (msg);

  command_t val = code;
  zmsg_addmem (msg, &val, sizeof (val));

  return msg;
}

void
send_notification (zmsg_t **ntf_msg)
{
  zmsg_send (ntf_msg, ipsg_sub_sock);
  zmsg_destroy (ntf_msg);
}

void
notify_trap_enabled (port_id_t pi,
                     vid_t vid,
                     mac_addr_t mac,
                     ip_addr_t ip)
{
  zmsg_t *msg;

  msg = make_notification (TRAP_ENABLED);

  zmsg_addmem (msg, &pi, sizeof (port_id_t));
  zmsg_addmem (msg, mac, sizeof (mac_addr_t));
  zmsg_addmem (msg, &vid, sizeof (vid_t));
  zmsg_addmem (msg, ip, sizeof (ip_addr_t));
  send_notification (&msg);
}

void
notify_ch_gr_set (vif_id_t vifid,
                  port_id_t pi)
{
  zmsg_t *msg;

  msg = make_notification (CH_GR_SET);

  zmsg_addmem (msg, &vifid, sizeof(vif_id_t));
  zmsg_addmem (msg, &pi, sizeof(port_id_t));

  send_notification (&msg);
}

void
notify_ch_gr_reset (vif_id_t vifid)
{
  zmsg_t *msg;

  msg = make_notification (CH_GR_RESET);

  zmsg_addmem (msg, &vifid, sizeof(vif_id_t));

  send_notification (&msg);
}




