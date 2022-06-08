#ifndef __LTTINDEX_H__
#define __LTTINDEX_H__

#include <control-proto.h>
#include <utlist.h>

#undef IN
#define IN
#undef OUT
#define OUT
#undef INOUT
#define INOUT

struct row_colum {
    uint32_t row;
    uint8_t colum;
};

struct db_ltt_index
{
    struct row_colum ix;
    struct db_ltt_index *next;
    struct db_ltt_index  *prev;
};

extern void ltt_index_get(OUT struct row_colum *index);
extern void ltt_index_del(IN struct row_colum *index);


#endif /* __LTTINDEX_H__ */
