#ifndef __TRUNK_H__
#define __TRUNK_H__

#include <port.h>

#define TRUNK_MAX_MEMBERS 8
#define TRUNK_MAX 127

struct trunk_port {
  int valid;
  int enabled;
  uint8_t ldev;
  uint8_t lport;
  uint8_t hdev;
  uint8_t hport;
};

struct trunk {
  int nports;
  struct trunk_port port[TRUNK_MAX_MEMBERS];
};

extern struct trunk trunks[];

extern void trunk_init (void);
extern enum status trunk_set_members (trunk_id_t, int, struct trunk_member *);


#endif /* __TRUNK_H__ */
