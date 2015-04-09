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

#define IPv4_FMT "%d.%d.%d.%d"
#define IPv4_ARG(p) p[0], p[1], p[2], p[3]

static inline int
gt_bool (int val)
{
  return val ? GT_TRUE : GT_FALSE;
}

static inline unsigned long long
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
  if (!addr) {DEBUG("(NULL)"); return; }
  /* process every byte in the data. */
  for (i=0;i<len;i++) {
    /* multiple of 16 means new line (with line offset). */
    if ((i%16) == 0) {
      pbuff = sbuff;
      /* just don't print  for the 0-th line. */
      if (i!=0) {
        nc += snprintf((char*) pbuff + nc, 80 - nc, "  %s\n", buff);
        DEBUG ("%s", pbuff);
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
  DEBUG("%s", pbuff);
}

#define PRINTHexDump(x, y)  if ((y)<=0) DEBUG("(NULL)"); else hexdump((void*)(x),(y))

#endif /* __UTILS_H__ */
