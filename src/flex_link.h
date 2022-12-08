#ifndef __FLEX_LINK__
#define __FLEX_LINK__

#include <stdint.h>

typedef struct flex_link {
    uint8_t iface_primary;
    uint8_t iface_backup;
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

void
flex_link_handle_link_change(uint8_t port, uint8_t new_link_state);

FlexLinkState
flex_link_add(uint8_t primary, uint8_t backup);

FlexLinkState
flex_link_del(uint8_t primary);

FlexLink*
flex_link_get(void);

FlexLink*
flex_link_next(void);

#endif

