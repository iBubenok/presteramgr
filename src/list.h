#ifndef __LIST_H__
#define __LIST_H__

#include <utils.h>


typedef struct LIST_HANDLE {
  struct LIST_HANDLE *prev, *next;
} LIST_HANDLE;

#define LIST_APPEND(type, member, hptr, iptr)                       \
  ({                                                                \
    if (!hptr) {                                                    \
      hptr = iptr;                                                  \
      hptr->member.prev = hptr->member.next = &hptr->member;        \
    } else {                                                        \
      LIST_HANDLE *__h = &(hptr)->member, *__i = &(iptr)->member;   \
      __i->prev = __h->prev;                                        \
      __i->prev->next = __i;                                        \
      __i->next = __h;                                              \
      __h->prev = __i;                                              \
    }                                                               \
  })

#define LIST_DELETE(type, member, hptr, iptr)                    \
  ({                                                             \
    if (iptr) {                                                  \
      LIST_HANDLE *__i = &(iptr)->member;                        \
      if (__i->next == __i)                                      \
        hptr = NULL;                                             \
      else {                                                     \
        __i->next->prev = __i->prev;                             \
        __i->prev->next = __i->next;                             \
        if (iptr == hptr)                                        \
          hptr = container_of (__i->next, type, member);         \
      }                                                          \
      __i->prev = __i->next = NULL;                              \
    }                                                            \
  })

#define LIST_NEXT(type, member, hptr, iptr)                          \
  ({                                                                 \
    type *__r = NULL;                                                \
    if (iptr && (iptr->member.next != &hptr->member))                \
      __r = container_of (iptr->member.next, type, member);          \
    __r;                                                             \
  })

#define LIST_FOREACH(type, member, hptr, iptr)                          \
  for (iptr = hptr; iptr; iptr = LIST_NEXT (type, member, hptr, iptr))

#define LIST_FOREACH_SAFE(type, member, hptr, iptr, tmp)                \
  for (iptr = hptr;                                                     \
       tmp = LIST_NEXT (type, member, hptr, iptr), iptr;                \
       iptr = tmp)


#endif /* __LIST_H__ */
