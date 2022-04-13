#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <pbr.h>
#include <pcl.h>
#include <log.h>
#include <lttindex.h>
#include <debug.h>

#include <utlist.h>

uint32_t firstIndex = 1205;

struct db_ltt_index *db_ltt_index = NULL;

void next(INOUT struct db_ltt_index *el){
    el->ix.row++;
}

int cmp(struct db_ltt_index *a, struct db_ltt_index *b){
  if (a->ix.row == b->ix.row && a->ix.colum == b->ix.colum) {
    return 0;
  }
  if (a->ix.row > b->ix.row) {
    return 1;
  }
  if (a->ix.row < b->ix.row) {
    return -1;
  }
  if (a->ix.row == b->ix.row && a->ix.colum > b->ix.colum) {
    return 1;
  }
  return -1;

}

void ltt_index_get(OUT struct row_colum *index){
  struct db_ltt_index *el = NULL, etmp, *add = NULL;

  etmp.ix.row = firstIndex;
  etmp.ix.colum = 0;

  do {
    DL_SEARCH(db_ltt_index,el,&etmp,cmp);
    if (!el){
      add = calloc(1, sizeof(struct db_ltt_index));
      add->ix = etmp.ix;
      // add->ix.colum = etmp.ix.colum;
      // add->ix.row = etmp.ix.row;
      DL_APPEND(db_ltt_index,add);
      *index = add->ix;
      // index->colum = add->ix.colum;
      // index->row = add->ix.row;
      return;
    }
     next(&etmp);

  } while (true);
}
void ltt_index_del(IN struct row_colum *index){
  struct db_ltt_index *el = NULL, etmp;
  etmp.ix = *index;
  // etmp.ix.colum = index->colum;
  // etmp.ix.row = index->row;

  DL_SEARCH(db_ltt_index,el,&etmp,cmp);
  if (el){
    DL_DELETE(db_ltt_index,el);
    free(el);
  }
}
