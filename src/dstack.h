#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "utlist.h"

struct dstack {
  struct   dstack_item *head;
  struct   dstack_item *sp;
  uint64_t count;
};

#define INT_CMP(type) int_cmp_##type

#define DECLARE_INT_CMP(type) \
  int INT_CMP(type)(const void*, const void*);

#define DEFINE_INT_CMP(type)                    \
int INT_CMP(type)(const void* a, const void* b) \
{                                               \
  type *__a = (type *) a;                       \
  type *__b = (type *) b;                       \
  return (*__a > *__b) - (*__a < *__b);         \
}

void
dstack_init (struct dstack **s);

void
dstack_free (struct dstack **s);

void
dstack_copy (struct dstack **dst, const struct dstack *src);

void
dstack_print (const struct dstack *s);

void
dstack_push (struct dstack *s, const void *data, size_t data_sz);

void
dstack_push_back (struct dstack *s, const void *data, size_t data_sz);

void
dstack_pop (struct dstack *s, void *data, size_t *data_sz);

void
dstack_sort (struct dstack *s);

void
dstack_rev_sort (struct dstack *s);

void
dstack_sort2 (struct dstack *c, int (*cmp)(const void*, const void*));

void
dstack_rev_sort2 (struct dstack *c, int (*cmp)(const void*, const void*));
