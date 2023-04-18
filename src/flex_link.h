#ifndef __FLEX_LINK__
#define __FLEX_LINK__

#include "control-proto.h" // for vif_id_t

typedef struct flex_link {
    vif_id_t primary;
    vif_id_t backup;
} FlexLink;

typedef struct flex_link_list {
    struct flex_link data;
    struct flex_link_list *next;
} FlexLinkList;

typedef enum flex_link_status {
    FLEX_LINK_OK,
    FLEX_LINK_BAD_ALLOC,
    FLEX_LINK_NOT_FOUND
} FlexLinkState;

// Тип для указателя на функцию отключения интерфейса
typedef enum status (*FlexLinkShutdown) (vif_id_t vif, int state);

void
flex_link_handle_link_change(vif_id_t vif_id,
                             uint8_t new_link_state,
                             FlexLinkShutdown shutdown);

FlexLinkState
flex_link_add(vif_id_t primary, vif_id_t backup);

FlexLinkState
flex_link_del(vif_id_t primary);

FlexLink*
flex_link_get(void);

FlexLink*
flex_link_next(void);

#endif

