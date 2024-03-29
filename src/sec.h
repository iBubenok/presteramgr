#ifndef __SEC_H__
#define __SEC_H__

#include <control-proto.h>
#include <sysdeps.h>
#include <port.h>

#define SEC_EVENT_NOTIFY_EP  "inproc://sec-notify"
#define SEC_PUBSUB_EP  "inproc://sec-pubsub"

extern enum status sec_port_na_delay_set (port_id_t, uint32_t);
extern enum status sec_moved_static_delay_set (port_id_t, uint32_t);
extern enum status sec_handle_security_breach_updates (GT_U8, GT_U32);
extern enum status sec_port_na_enable (const struct port *, GT_BOOL);
extern enum status sec_moved_static_enable (uint8_t, GT_BOOL);
extern enum status sec_init(void);
extern enum status sec_start(void);
extern int sec_port_enable (port_id_t pid, int);

#endif /* __SEC_H__ */
