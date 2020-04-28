#include "dstack.h"
#include <debug.h>

struct dstack_item {
  struct dstack_item *next;
  struct dstack_item *prev;
  void               *data;
  size_t             data_sz;
};

static void
print_data (const void *data, size_t size) {
  int i;
  fprintf(stderr, "%p: ", data);
  for (i = 0; i < size; i++) {
    fprintf(stderr, "%02x ", *((uint8_t*)data + i));
  }
  fprintf(stderr, "\n");
}

static void
dstack_item_init (struct dstack_item **item, const void *data, size_t data_sz) {
  (*item)          = malloc(sizeof(struct dstack_item));
  (*item)->data    = malloc(data_sz);
  memcpy((*item)->data, data, data_sz);
  (*item)->data_sz = data_sz;
  (*item)->next    = NULL;
  (*item)->prev    = NULL;
}

static void
dstack_item_free (struct dstack_item **item) {
  free((*item)->data);
  free((*item));
  (*item) = NULL;
}

static size_t
min_sz2 (size_t a, size_t b) {
  return (a < b) ? a : b;
}

static int
dstack_item_cmp (const void *a, const void *b) {
  const struct dstack_item *dsia = (const struct dstack_item *)a;
  const struct dstack_item *dsib = (const struct dstack_item *)b;
  return memcmp(dsia->data, dsib->data, min_sz2(dsia->data_sz, dsib->data_sz));
}

static int (*custom_cmp_fun)(const void*, const void*) = dstack_item_cmp;

static int
dstack_item_cmp_custom (const void *a,
                        const void *b) {
  const struct dstack_item *dsia = (const struct dstack_item *)a;
  const struct dstack_item *dsib = (const struct dstack_item *)b;
  return custom_cmp_fun(dsia->data, dsib->data);
}

void
dstack_init (struct dstack **s) {
  (*s)        = malloc(sizeof(struct dstack));
  (*s)->head  = NULL;
  (*s)->sp    = NULL;
  (*s)->count = 0;
}

void
dstack_print (const struct dstack *s) {
  fprintf(stderr, "===========================================\n");
  fprintf(stderr, "Count: %llu\n", s->count);
  fprintf(stderr, "Data:\n");
  struct dstack_item *elt = NULL;
  int i = 0;
  int delim = 1;
  DL_FOREACH(s->head, elt) {
    if ((i < 10) || (i > (s->count - 10))) {
      print_data(elt->data, elt->data_sz);
    } else if (delim) {
      fprintf(stderr, "  ... ... ...\n");
      delim = 0;
    }
    i++;
  }
  fprintf(stderr, "\nStackPointer:\n");
  print_data(s->sp->data, s->sp->data_sz);
  fprintf(stderr, "===========================================\n");
}

void
dstack_push (struct dstack *s, const void *data, size_t data_sz) {
  struct dstack_item *item = NULL;
  dstack_item_init(&item, data, data_sz);
  DL_APPEND(s->head, item);
  s->sp = item;
  s->count++;
}

void
dstack_push_back (struct dstack *s, const void *data, size_t data_sz) {
  struct dstack_item *item = NULL;
  dstack_item_init(&item, data, data_sz);
  DL_PREPEND(s->head, item);
  if (!s->count) {
    s->sp = item;
  }
  s->count++;
}

int
dstack_pop (struct dstack *s, void *data, size_t *data_sz) {
  if (s->count) {
    if (data) {
      memcpy(data, s->sp->data, s->sp->data_sz);
    }
    if (data_sz) {
      memcpy(data_sz, &s->sp->data_sz, sizeof(size_t));
    }
    struct dstack_item* tmp = s->sp->prev;
    DL_DELETE(s->head, s->sp);
    s->count--;
    dstack_item_free(&(s->sp));
    s->sp = tmp;
    return 0;
  }
  else
    return 1;
}

void
dstack_sort (struct dstack *s) {
  DL_SORT(s->head, dstack_item_cmp);
  s->sp = s->head;
  if (s->sp) {
    while (s->sp->next) {
      s->sp = s->sp->next;
    }
  }
}

void
dstack_rev_sort (struct dstack *s) {
  DL_SORT(s->head, -dstack_item_cmp);
  s->sp = s->head;
  if (s->sp) {
    while (s->sp->next) {
      s->sp = s->sp->next;
    }
  }
}

void
dstack_sort2 (struct dstack *s, int (*cmp)(const void*, const void*)) {
  custom_cmp_fun = cmp;
  DL_SORT(s->head, dstack_item_cmp_custom);
  custom_cmp_fun = dstack_item_cmp;
  s->sp = s->head;
  if (s->sp) {
    while (s->sp->next) {
      s->sp = s->sp->next;
    }
  }
}

void
dstack_rev_sort2 (struct dstack *s, int (*cmp)(const void*, const void*)) {
  custom_cmp_fun = cmp;
  DL_SORT(s->head, -dstack_item_cmp_custom);
  custom_cmp_fun = dstack_item_cmp;
  s->sp = s->head;
  if (s->sp) {
    while (s->sp->next) {
      s->sp = s->sp->next;
    }
  }
}
void
dstack_free (struct dstack **s) {
  while ((*s)->count) {
    dstack_pop(*s, NULL, NULL);
  }
  free(*s);
}

void
dstack_copy (struct dstack **dst, const struct dstack *src) {
  dstack_free(dst);
  dstack_init(dst);
  struct dstack_item* tmp;
  DL_FOREACH(src->head, tmp) {
    dstack_push(*dst, tmp->data, tmp->data_sz);
  }
}
