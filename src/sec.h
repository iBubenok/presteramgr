#ifndef __SEC_H__
#define __SEC_H__

#include <control-proto.h>
#include <sysdeps.h>
    
#define SEC_EVENT_NOTIFY_EP  "inproc://sec-notify"
#define SEC_PUBSUB_EP  "inproc://sec-pubsub"
    
extern enum status sec_set_sb_delay_port_na (port_id_t, uint32_t);
extern enum status sec_set_sb_delay_moved_static (port_id_t, uint32_t);
extern enum status sec_handle_security_breach_updates (GT_U8, GT_U32);
extern enum status sec_init(void);
extern enum status sec_start(void);

#endif /* __SEC_H__ */
