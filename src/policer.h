#ifndef POLICER_H
#define POLICER_H


/* Модуль для работы с policer'ами. */


/* includes ----------------------------------------------------------------- */

#include <stdbool.h>
#include <stdint.h>

#include "control-proto.h"

/* types -------------------------------------------------------------------- */

/**
 * Статус возвращаемый функциями модуля
 */
typedef enum policer_status {
    POLICER_OK,           ///< всё ок
    POLICER_NO_MORE_IX,   ///< индексы кончились
    POLICER_NOT_IN_USE,   ///< попытка вернуть неиспользуемый индекс
    POLICER_CREATE_ERR,   ///< ошибка при создании policer'а
    POLICER_NOT_IN_RANGE, ///< выход за границы массива
    POLICER_QOS_ERR,      ///< ошибка при работе с qos
    POLICER_BAD_PARAM,    ///< переданы неверные параметры
    POLICER_BILLING_ERR   ///< ошибка при получении billing entry
} policer_status_t;

typedef struct policer_stat {
    // green..red - conform levels stats
    uint64_t green;
    uint64_t yellow;
    uint64_t red;
    // cir..pbs - real tocken bucket params
    uint32_t cir;
    uint32_t cbs;
    uint32_t ebs;
    uint32_t pir;
    uint32_t pbs;
} policer_stat_t;

/* function prototypes ------------------------------------------------------ */

/**
 * Создать policer.
 *
 * @param[out] ix             указатель на место куда сохранить индекс нового
 *                            policer'а
 * @param[in]  policer_params параметры создаваемого policer'а
 *
 * @return статус выполнения
 */
policer_status_t
policer_create(uint32_t *ix, struct policer_params *policer_params);

/**
 * Удалить policer.
 *
 * @param[in] ix   индекс policer'а который необходимо удалить
 * @param[in] dest ingress или egress policer
 *
 * @return статус выполнения
 */
policer_status_t
policer_delete(uint32_t ix, policer_dest_t dest);

/**
 * Вернуть сколько еще policer'ов можно создать
 *
 * @param[in] dest ingress или egress policer
 *
 * @return кол-во доступных policer'ов (0 или больше)
 */
int policer_available(policer_dest_t dest);

/**
 * Получить параметры policer'а.
 *
 * @param[out] stat       указатель на структуру для сохранения параметров
 *                        policer'а
 * @param[in]  policer_ix индекс policer'а для которого необходимо получить
 *                        параметры
 * @param[in]  dest       ingress или egress policer
 *
 * @return статус выполнения
 */
policer_status_t
policer_get_params(policer_stat_t *stat,
                   uint32_t policer_ix,
                   policer_dest_t dest);

#endif /* end of include guard POLICER_H */
