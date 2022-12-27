#include "flex_link.h"

#include <stdlib.h>
#include <unistd.h>

#include "log.h"

static FlexLinkList *flex_link_head = NULL;
static FlexLinkList *flex_link_curr = NULL;

/**
 * Обрабатываем событие изменения состояния канала.
 * Если упал канал у которого задан резервный канал, резервный канал поднимается.
 * Если основной канал паднялся. резервный канал отключается.
 *
 * @param[in] port номер канала у которого изменилось состояние (1, 2 и т.п.)
 * @param[in] new_link_state новое состояние канала (1 - поднят, 0 - опущен)
 * @param[in] shutdown указатель на функции отключения интерфейса
 */
void
flex_link_handle_link_change(vif_id_t vif_id,
                             uint8_t new_link_state,
                             FlexLinkShutdown shutdown)
{
    FlexLinkList *item = flex_link_head;

    while (item) {
        if (vif_id == item->data.primary && new_link_state == 0) {
            shutdown(item->data.backup,  0);
        }
        else if (vif_id == item->data.primary && new_link_state == 1) {
            shutdown(item->data.backup,  1);
        }

        item = item->next;
    }
}


/**
 * Создаёт flex-link.
 *
 * @param[in] primary основной канал.
 * @param[in] backup резервный канал.
 *
 * @return в случае успеха возвращает FLEX_LINK_OK, иначе FLEX_LINK_BAD_ALLOC
 */
FlexLinkState
flex_link_add(vif_id_t primary, vif_id_t backup)
{
    FlexLinkList *new = malloc(sizeof(*new));
    if (!new) {
        return FLEX_LINK_BAD_ALLOC;
    }

    new->data.primary = primary;
    new->data.backup  = backup;
    new->next = flex_link_head;
    flex_link_head = new;

    return FLEX_LINK_OK;
}


/**
 * Удаляет flex-link.
 *
 * @param[in] primary основной канал.
 *
 * @return в случае успеха возвращает FLEX_LINK_OK, иначе FLEX_LINK_NOT_FOUND
 */
FlexLinkState
flex_link_del(vif_id_t primary)
{
    FlexLinkList *item = flex_link_head;
    FlexLinkList *prev = NULL;

    while (item) {
        if (item->data.primary == primary) {
            if (!prev) {
                flex_link_head = item->next;
            }
            else {
                prev->next = item->next;
            }

            free(item);

            return FLEX_LINK_OK;
        }

        prev = item;
        item = item->next;
    }

    return FLEX_LINK_NOT_FOUND;
}


/**
 * Возвращает указатель на первый flex-link в списке.
 *
 * @return указатель на первый flex-link в списке или NULL если flex-link'ов нет.
 */
FlexLink*
flex_link_get(void)
{
    flex_link_curr = flex_link_head;

    if (!flex_link_curr) {
        return NULL; // no flex-links
    }

    return &flex_link_curr->data;
}


/**
 * Возвращает указатель на следующий flex-link в списке.
 *
 * @return указатель на следующий flex-link в списке или NULL, если достигнут
 * конец списка.
 */
FlexLink*
flex_link_next(void)
{
    if (!flex_link_curr) {
        return NULL;
    }

    flex_link_curr = flex_link_curr->next;

    if (!flex_link_curr) {
        return NULL;
    }

    return &flex_link_curr->data;
}
