#ifndef __UTILS_H__
#define __UTILS_H__

#include <cpssdefs.h>
#include <gtOs/gtOsGen.h>

#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <log.h>
#include <czmq.h>
#include <stdint.h>
#include <netinet/in.h>

#define ARRAY_SIZE(a) (sizeof (a) / sizeof (*a))
#define ON_GT_ERROR(rc) if ((rc) != GT_OK)

#define GET_BIT(bmp, n) (!!(bmp[n / sizeof (*bmp)] & (1 << (n % sizeof (*bmp)))))
#define SET_BIT(bmp, n) (bmp[n / sizeof (*bmp)] |= (1 << (n % sizeof (*bmp))))
#define CLR_BIT(bmp, n) (bmp[n / sizeof (*bmp)] &= ~(1 << (n % sizeof (*bmp))))

#define container_of(ptr, type, member)                     \
  ({                                                        \
    const typeof (((type *) 0)->member) *__mptr = (ptr);    \
    (type *) ((char *) __mptr - offsetof (type, member));   \
  })

#define IPv4_FMT "%hhd.%hhd.%hhd.%hhd"
#define IPv6_FMT "%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX"
#define IPv4_ARG(p) p[0], p[1], p[2], p[3]
#define IPv6_ARG(p) p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]

static inline int
gt_bool (int val)
{
  return val ? GT_TRUE : GT_FALSE;
}

typedef unsigned long long monotimemsec_t;

static inline monotimemsec_t
time_monotonic (void) {
  struct timespec ts;
  int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
  assert(!rc);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static inline void
err (const char *msg)
{
  int err = errno;
  DEBUG ("%s: %s (%d)\r\n", msg, strerror (err), err);
}

static inline void
errex (const char *msg)
{
  err (msg);
  exit (EXIT_FAILURE);
}

#define MAC_FMT "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC_ARG(n) n[0], n[1], n[2], n[3], n[4], n[5]

static inline int
in_range (val, min, max)
{
  return val >= min && val <= max;
}

static inline void hexdump (void *addr, int len) {
  int i;
  unsigned char buff[17];
  unsigned char sbuff[80];
  unsigned char *pbuff;
  size_t nc = 0;
  unsigned char *pc = (unsigned char*)addr;
  if (!addr) {fprintf(stderr, "(NULL)"); return; }
  /* process every byte in the data. */
  for (i=0;i<len;i++) {
    /* multiple of 16 means new line (with line offset). */
    if ((i%16) == 0) {
      pbuff = sbuff;
      /* just don't print  for the 0-th line. */
      if (i!=0) {
        nc += snprintf((char*) pbuff + nc, 80 - nc, "  %s\n", buff);
        fprintf (stderr, "%s", pbuff);
        nc = 0;
      }
      /* output the offset. */
      nc += snprintf((char*)pbuff + nc, 80 - nc, "  %04x ", i);
    }
    /* Now the hex code for the specific character. */
    nc += snprintf((char *)pbuff + nc, 80 - nc, " %02x", pc[i]);
    /* And store a printable ASCII character for later. */
    if (pc[i]>=0x20)  buff[i%16]=pc[i];
    else              buff[i%16]='.';
    buff[(i%16)+1]=0;
  }
  // Pad out last line if not exactly 16 characters.
  while ((i%16)!=0) { nc += snprintf((char*)pbuff + nc, 80 - nc, "   "); i++; }
  // And print the final ASCII bit.
  nc += snprintf((char*)pbuff + nc, 80 -nc,  "  %s\n", buff);
  fprintf(stderr, "%s", pbuff);
}

#define PRINTHexDump(x, y)  if ((y)<=0) fprintf(stderr, "(NULL)"); else hexdump((void*)(x),(y))

static inline int
is_llc_snap_frame__ (void* frame, int len) {
  uint8_t *ptr = ((uint8_t*) frame);

  int type_or_len_offset = 12;
  uint16_t type_or_len;

  memcpy(&type_or_len, ptr + type_or_len_offset, sizeof(type_or_len));
  type_or_len = ntohs(type_or_len);

  if ( type_or_len > 1500 )
    return GT_FALSE; /* Ethernet DIX */

  int ipx_offset = type_or_len_offset + sizeof(type_or_len);
  uint16_t ipx_start;

  memcpy(&ipx_start, ptr + ipx_offset, sizeof(ipx_start));

  if ( ipx_start == 0xFFFF )
    return GT_FALSE; /* Ethernet Novell 802.3 */

  if ( (*(ptr + ipx_offset)) == 0xAA )
    return GT_TRUE; /* Ethernet SNAP */

  return GT_FALSE; /* Ethernet LLC */
}

#define is_llc_snap_frame(frame, len) is_llc_snap_frame__(frame, len)

struct _zmsg_t {
  zlist_t *frames;
  size_t content_size;
};

#define DEBUG_ZMSG_SEND(self_p, socket) \
do { zmsg_t **sself = (zmsg_t**)(self_p); \
  struct _zmsg_t *self = *sself; \
    if (self) { \
      zframe_t *frame = (zframe_t *) zlist_pop (self->frames); \
      while (frame) { \
        int rc; \
        rc = zframe_send (&frame, socket, \
        zlist_size (self->frames)? ZFRAME_MORE: 0); \
        if (rc != 0) { \
          DEBUG ("DEBUG_ZMSG_SEND: UNABLE to zframe_send " #self_p " with rc==%d, message: %s", \
              rc, strerror(rc)); \
        } \
        frame = (zframe_t *) zlist_pop (self->frames); \
      } \
   zmsg_destroy (sself); \
   } \
} while (0)

#endif /* __UTILS_H__ */
