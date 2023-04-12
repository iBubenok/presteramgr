/* includes ----------------------------------------------------------------- */

#include "policer.h"

#include <stdint.h>
#include <string.h>

#include <cpss/generic/cpssCommonDefs.h>
#include <cpssdefs.h>
#include <cpss/dxCh/dxCh3/policer/cpssDxCh3Policer.h>
#include <cpss/extServices/os/gtOs/gtEnvDep.h>

#include "control-proto.h"
#include "debug.h"
#include "port.h"
#include "qos.h"
#include "sysdeps.h"


/* PRIVATE FUNCTION PROTOTYPES ---------------------------------------------- */

// получить индекс для нового policer'а
static policer_status_t
policer_ix_pull(uint32_t *ix, policer_dest_t dest);

// вернуть индекс удаленного policer'а
static policer_status_t
policer_ix_push(uint32_t ix, policer_dest_t dest);

// создать policer
static policer_status_t
add_metering_entry
(
    int                          policer_ix,
    const struct policer_params *policer_params,
    int                          dev_num
);

// создать счётчик
static policer_status_t
add_billing_entry
(
    int                          policer_ix,
    const struct policer_params *policer_params,
    int                          dev_num
);

static policer_status_t
add_qos_profile
(
    qos_profile_id_t            *profile_id,
    const struct policer_params *policer_params,
    int                          policer_ix
);

void
set_meter_mode
(
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC *entry,
    const struct policer_params           *policer_params
);

void
set_tocken_bucket
(
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC *entry,
    const struct policer_params           *policer_params
);

policer_status_t
set_action
(
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC *entry,
    const struct policer_params           *policer_params
);

static policer_status_t
del_qos_profile(int policer_ix);

policer_status_t
get_billing_by_ix(policer_stat_t *stat, int policer_ix, policer_dest_t dest);

policer_status_t
get_tocken_bucket(policer_stat_t *stat, int policer_ix, policer_dest_t dest);

policer_status_t
set_qos
(
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC *entry,
    const struct policer_params *policer_params,
    int policer_ix
);

// проверяет существует ли policer с таким индексом
// если существует возвращает 1, иначе 0
int is_policer_exist(int policer_ix, policer_dest_t dest);

/* GLOBAL VARIABLES --------------------------------------------------------- */

enum
{
    ixs_sz_ingress = 255, // кол-во индексов для ingress policer'а
    ixs_sz_egress  = 255, // кол-во индексов для egress policer'а
    ixs_start_pos  = 2    // отсчет начинаем с 2 (индекс 1 что-то не работает)
};

enum { FREE = 0, USED = 1 };  // индекс свободен или занят

static uint8_t  ixs_ingress[ixs_sz_ingress] = {FREE};
static uint8_t  ixs_egress[ixs_sz_egress]   = {FREE};
static uint16_t ixs_qos[ixs_sz_ingress]     = {0};

/* PUBLIC FUNCTIONS --------------------------------------------------------- */

policer_status_t
policer_create(uint32_t *ix, struct policer_params *policer_params)
{
    policer_status_t rv = policer_ix_pull(ix, policer_params->dest);
    if (rv != POLICER_OK) {
        return rv;
    }

    int dev_num;
    for_each_dev (dev_num) {

        policer_status_t metering_rv =
                            add_metering_entry(*ix, policer_params, dev_num);
        if (metering_rv != POLICER_OK) {
            DEBUG("%s: add_metering_entry() is failed with code %d",
               __FUNCTION__, metering_rv);
            return POLICER_CREATE_ERR;
        }

        policer_status_t billing_rv =
                            add_billing_entry(*ix, policer_params, dev_num);
        if (billing_rv != POLICER_OK) {
            DEBUG("%s: add_billing_entry() is failed with code %d",
               __FUNCTION__, billing_rv);
            return POLICER_CREATE_ERR;
        }
    }

    return POLICER_OK;
}

policer_status_t
policer_delete(uint32_t policer_ix, policer_dest_t dest)
{
    policer_status_t rv = policer_ix_push(policer_ix, dest);
    if (rv != POLICER_OK) {
        return rv;
    }

    if (dest == POLICER_DEST_INGRESS) { // for egress qos profile isn't created
        rv = del_qos_profile(policer_ix);
        if (rv != POLICER_OK) {
            return rv;
        }
    }

    return POLICER_OK;
}

int
policer_available(policer_dest_t dest)
{
    uint8_t *ixs = (dest == POLICER_DEST_INGRESS) ? ixs_ingress : ixs_egress;

    int ixs_sz = (dest == POLICER_DEST_INGRESS) ? ixs_sz_ingress : ixs_sz_egress;

    int quantity = 0;

    int i;
    for (i = 0; i < ixs_sz; ++i) {
        if (ixs[i] == FREE) {
            ++quantity;
        }
    }

    return quantity;
}

policer_status_t
policer_get_params
(
    policer_stat_t *stat,
    uint32_t policer_ix,
    policer_dest_t dest
)
{
    if (!is_policer_exist(policer_ix, dest)) {
        DEBUG("%s: policer with policer_ix %d and dest %d does not exist",
                __FUNCTION__, policer_ix, dest);
        return POLICER_BAD_PARAM;
    }

    // get green, yellow, red stats
    policer_status_t rv = get_billing_by_ix(stat, policer_ix, dest);
    if (rv != POLICER_OK) {
        DEBUG("%s: get_billing_by_ix() is failed: %d", __FUNCTION__, rv);
        return rv;
    }

    // get cir..pbs
    rv = get_tocken_bucket(stat, policer_ix, dest);
    if (rv != POLICER_OK) {
        DEBUG("%s: get_tocken_bucket() is failed: %d", __FUNCTION__, rv);
        return rv;
    }

    return POLICER_OK;
}

/* PRIVATE FUNCTIONS -------------------------------------------------------- */

static policer_status_t
policer_ix_pull(uint32_t *ix, policer_dest_t dest)
{
    uint8_t *ixs = (dest == POLICER_DEST_INGRESS) ? ixs_ingress : ixs_egress;

    int ixs_sz = (dest == POLICER_DEST_INGRESS) ? ixs_sz_ingress : ixs_sz_egress;

    int i;
    for (i = 0; i < ixs_sz; ++i) {
        if (ixs[i] == FREE) {
            ixs[i] = USED;
            *ix = i + ixs_start_pos;
            return POLICER_OK;
        }
    }

    return POLICER_NO_MORE_IX;
}

static policer_status_t
policer_ix_push(uint32_t ix, policer_dest_t dest)
{
    uint8_t *ixs = (dest == POLICER_DEST_INGRESS) ? ixs_ingress : ixs_egress;

    int ixs_sz = (dest == POLICER_DEST_INGRESS) ? ixs_sz_ingress : ixs_sz_egress;

    uint32_t arr_ix = ix - ixs_start_pos;

    if (arr_ix > ixs_sz - 1) {
        return POLICER_NOT_IN_RANGE;
    }

    if (ixs[arr_ix] == FREE) {
        return POLICER_NOT_IN_USE;
    }

    ixs[arr_ix] = FREE;

    return POLICER_OK;
}

static policer_status_t
add_metering_entry
(
    int                          policer_ix,
    const struct policer_params *policer_params,
    int                          dev_num
)
{
    // init metering entry
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC entry;
    memset(&entry, 0, sizeof(entry));

    entry.countingEntryIndex = policer_ix;
    entry.meterColorMode     = CPSS_POLICER_COLOR_BLIND_E;

    // set meter mode
    set_meter_mode(&entry, policer_params);

    // set tocket bucket params
    set_tocken_bucket(&entry, policer_params);

    // set action
    if (set_action(&entry, policer_params) != POLICER_OK) {
        return POLICER_BAD_PARAM;
    }

    // set qos
    if (set_qos(&entry, policer_params, policer_ix) != POLICER_OK) {
        DEBUG("%s: set_qos() is failed", __FUNCTION__);
        return POLICER_CREATE_ERR;
    }

    // set dest
    policer_dest_t dest =
        (policer_params->dest == POLICER_DEST_INGRESS)
        ? CPSS_DXCH_POLICER_STAGE_INGRESS_0_E
        : CPSS_DXCH_POLICER_STAGE_EGRESS_E;

    // init output token bucket params
    CPSS_DXCH3_POLICER_METER_TB_PARAMS_UNT tokenBucket;
    memset(&tokenBucket, 0, sizeof(tokenBucket));

    // add metering entry
    GT_STATUS rc = cpssDxCh3PolicerMeteringEntrySet(
            dev_num,
            dest,
            policer_ix,
            &entry,
            &tokenBucket);

    if (rc != GT_OK) {
        DEBUG("%s: cpssDxCh3PolicerMeteringEntrySet() is failed", __FUNCTION__);
        return POLICER_CREATE_ERR;
    }

    return POLICER_OK;
}

static policer_status_t
add_billing_entry
(
    int                          policer_ix,
    const struct policer_params *policer_params,
    int                          dev_num
)
{
    CPSS_DXCH3_POLICER_BILLING_ENTRY_STC bill_entry;
    memset(&bill_entry, 0, sizeof(bill_entry));
    bill_entry.billingCntrMode = CPSS_DXCH3_POLICER_BILLING_CNTR_PACKET_E;
    bill_entry.greenCntr.l[0]  = 0x0;
    bill_entry.greenCntr.l[1]  = 0x0;
    bill_entry.yellowCntr.l[0] = 0x0;
    bill_entry.yellowCntr.l[1] = 0x0;
    bill_entry.redCntr.l[0]    = 0x0;
    bill_entry.redCntr.l[1]    = 0x0;

    policer_dest_t dest =
        (policer_params->dest == POLICER_DEST_INGRESS)
        ? CPSS_DXCH_POLICER_STAGE_INGRESS_0_E
        : CPSS_DXCH_POLICER_STAGE_EGRESS_E;

    // add billing entry
    GT_STATUS rc = cpssDxCh3PolicerBillingEntrySet(dev_num,
                                                   dest,
                                                   policer_ix,
                                                   &bill_entry);

    if (rc != GT_OK) {
        DEBUG("%s: cpssDxCh3PolicerBillingEntrySet() is failed", __FUNCTION__);
        return POLICER_CREATE_ERR;
    }

    // reset counters
    int resetCounter = GT_TRUE;
    rc = cpssDxCh3PolicerBillingEntryGet(
                                        dev_num,
                                        dest == POLICER_DEST_INGRESS
                                        ? CPSS_DXCH_POLICER_STAGE_INGRESS_0_E
                                        : CPSS_DXCH_POLICER_STAGE_EGRESS_E,
                                        policer_ix,
                                        resetCounter,
                                        &bill_entry);

    if (rc != GT_OK) {
        DEBUG("%s: cpssDxCh3PolicerBillingEntryGet() is failed", __FUNCTION__);
        return POLICER_BILLING_ERR;
    }

    return POLICER_OK;
}

static policer_status_t
add_qos_profile
(
    qos_profile_id_t            *profile_id,
    const struct policer_params *policer_params,
    int                          policer_ix
)
{
    struct qos_profile_mgmt qpm;
    memset(&qpm, 0, sizeof(qpm));

    qpm.cmd = QOS_PROFILE_ADD;
    qpm.num  = 1;

    switch (policer_params->exceed_action.type) {
        case POLICER_ACTION_SET_DSCP:
            qpm.dscp = policer_params->exceed_action.value;
            break;
        case POLICER_ACTION_SET_COS:
            qpm.cos = policer_params->exceed_action.value;
            break;
    }

    enum status result = qos_profile_manage(&qpm, profile_id);
    if (result != GT_OK) {
        DEBUG("qos_profile_manage() is failed with code %d", result);
        return POLICER_QOS_ERR;
    }

    uint32_t arr_ix = policer_ix - ixs_start_pos;
    ixs_qos[arr_ix] = *profile_id;

    return POLICER_OK;
}

static policer_status_t
del_qos_profile(int policer_ix)
{
    qos_profile_id_t qos_profile_id; // don't used here but need for func

    uint32_t arr_ix = policer_ix - ixs_start_pos;
    qos_profile_id = ixs_qos[arr_ix];

    if (qos_profile_id == 0) { // 0 - qos-профиль не создавался, удалять не надо
        return GT_OK;
    }

    struct qos_profile_mgmt qpm = {0};
    qpm.id  = qos_profile_id;
    qpm.cmd = QOS_PROFILE_DEL;

    enum status result = qos_profile_manage(&qpm, &qos_profile_id);
    if (result != GT_OK) {
        DEBUG("%s: qos_profile_manage() is failed with code %d",
               __FUNCTION__,
               result);

        return POLICER_QOS_ERR;
    }

    return POLICER_OK;
}

void
set_meter_mode
(
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC *entry,
    const struct policer_params           *policer_params
)
{
    if (policer_params->pir != 0) {
        entry->meterMode = CPSS_DXCH3_POLICER_METER_MODE_TR_TCM_E;
    }
    else {
        entry->meterMode = CPSS_DXCH3_POLICER_METER_MODE_SR_TCM_E;
    }
}

void
set_tocken_bucket
(
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC *entry,
    const struct policer_params           *policer_params
)
{
    if (policer_params->pir) { // two rate
        entry->tokenBucketParams.trTcmParams.cir = policer_params->cir;
        entry->tokenBucketParams.trTcmParams.cbs = policer_params->cbs ?:
                                                   0xFFFFFFFF;
        entry->tokenBucketParams.trTcmParams.pir = policer_params->pir;
        entry->tokenBucketParams.trTcmParams.pbs = policer_params->pbs ?:
                                                   0xFFFFFFFF;
    }
    else { // single rate
        entry->tokenBucketParams.srTcmParams.cir = policer_params->cir;
        // 10750 |10000 - 11500|
        entry->tokenBucketParams.srTcmParams.cbs = policer_params->cbs ?:
                                                   0xFFFFFFFF;
        int def_ebs = // 12750
            policer_params->violate_action.type == POLICER_ACTION_UNDEF
            ?  policer_params->cbs // one color
            : 0xFFFFFFFF;          // two color
        entry->tokenBucketParams.srTcmParams.ebs = policer_params->ebs ?:
                                                   def_ebs;
    }
}

policer_status_t
set_action
(
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC *entry,
    const struct policer_params           *policer_params
)
{
    // set exceed action
    switch (policer_params->exceed_action.type) {
        case POLICER_ACTION_TRANSMIT:
            entry->yellowPcktCmd =
                        CPSS_DXCH3_POLICER_NON_CONFORM_CMD_NO_CHANGE_E;
            break;
        case POLICER_ACTION_SET_DSCP:
        case POLICER_ACTION_SET_COS:
            if (policer_params->dest == POLICER_DEST_EGRESS) {
                DEBUG("%s: qos remark is not permit for egress", __FUNCTION__);
                return POLICER_BAD_PARAM;
            }
            entry->yellowPcktCmd =
                        CPSS_DXCH3_POLICER_NON_CONFORM_CMD_REMARK_BY_ENTRY_E;
            break;
        case POLICER_ACTION_DROP:
            entry->yellowPcktCmd =
                        CPSS_DXCH3_POLICER_NON_CONFORM_CMD_DROP_E;
            break;
        default:
            DEBUG("%s: invalid exceed action type %d",
                  __FUNCTION__,
                  policer_params->exceed_action.type);
            return POLICER_BAD_PARAM;
    }

    // set violate action
    switch (policer_params->violate_action.type) {
        case POLICER_ACTION_UNDEF:
            entry->redPcktCmd = entry->yellowPcktCmd;
            break;
        case POLICER_ACTION_DROP:
            entry->redPcktCmd = CPSS_DXCH3_POLICER_NON_CONFORM_CMD_DROP_E;
            break;
        default:
            DEBUG("%s: invalid violate action type %d",
                  __FUNCTION__,
                  policer_params->violate_action.type);
            return POLICER_BAD_PARAM;
    }

    return POLICER_OK;
}

policer_status_t
get_billing_by_ix(policer_stat_t *stat, int policer_ix, policer_dest_t dest)
{
    int dev_num = 0; // TODO for_each_dev
    int resetCounter = GT_FALSE; // only read, not reset
    CPSS_DXCH3_POLICER_BILLING_ENTRY_STC billEntry;

    GT_STATUS rc = cpssDxCh3PolicerBillingEntryGet(
                                        dev_num,
                                        dest == POLICER_DEST_INGRESS
                                        ? CPSS_DXCH_POLICER_STAGE_INGRESS_0_E
                                        : CPSS_DXCH_POLICER_STAGE_EGRESS_E,
                                        policer_ix,
                                        resetCounter,
                                        &billEntry);

    if (rc != GT_OK) {
        DEBUG("cpssDxCh3PolicerBillingEntryGet failed: rc = %u\n", rc);
        return POLICER_BILLING_ERR;
    }

    GT_U32 sum = billEntry.greenCntr.l[0]  + billEntry.greenCntr.l[1]  +
                 billEntry.yellowCntr.l[0] + billEntry.yellowCntr.l[1] +
                 billEntry.redCntr.l[0]    + billEntry.redCntr.l[1];

    DEBUG("dest: %s", dest ? "EGRESS" : "INGRESS");
    DEBUG("\n\tindex   : %d"
            "\n\tsum   : %d"
            "\n\tgreen : %d + %d"
            "\n\tyellow: %d + %d"
            "\n\tred   : %d + %d\n",
          policer_ix,
          sum,
          billEntry.greenCntr.l[0],
          billEntry.greenCntr.l[1],
          billEntry.yellowCntr.l[0],
          billEntry.yellowCntr.l[1],
          billEntry.redCntr.l[0],
          billEntry.redCntr.l[1]);

    // converts 2 uint32_t to 1 uint64_t
    stat->green = billEntry.greenCntr.l[1];
    stat->green <<= 32;
    stat->green += billEntry.greenCntr.l[0];

    stat->yellow = billEntry.yellowCntr.l[1];
    stat->yellow <<= 32;
    stat->yellow += billEntry.yellowCntr.l[0];

    stat->red  = billEntry.redCntr.l[1];
    stat->red <<= 32;
    stat->red += billEntry.redCntr.l[0];

    return POLICER_OK;
}

policer_status_t
get_tocken_bucket(policer_stat_t *stat, int policer_ix, policer_dest_t dest)
{
    int dev_num = 0; // TODO for_each_dev
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC entry;
    GT_STATUS rc = cpssDxCh3PolicerMeteringEntryGet(
            dev_num,
            dest == POLICER_DEST_INGRESS
                ? CPSS_DXCH_POLICER_STAGE_INGRESS_0_E
                : CPSS_DXCH_POLICER_STAGE_EGRESS_E,
            policer_ix,
            &entry);

    if (rc != GT_OK) {
        DEBUG("cpssDxCh3PolicerBillingEntryGet failed: rc = %u\n", rc);
        return POLICER_BILLING_ERR;
    }

    switch (entry.meterMode) {
        case CPSS_DXCH3_POLICER_METER_MODE_SR_TCM_E:
            stat->cir = entry.tokenBucketParams.srTcmParams.cir;
            stat->cbs = entry.tokenBucketParams.srTcmParams.cbs;
            stat->ebs = entry.tokenBucketParams.srTcmParams.ebs;
            break;
        case CPSS_DXCH3_POLICER_METER_MODE_TR_TCM_E:
            stat->cir = entry.tokenBucketParams.trTcmParams.cir;
            stat->cbs = entry.tokenBucketParams.trTcmParams.cbs;
            stat->pir = entry.tokenBucketParams.trTcmParams.pir;
            stat->pbs = entry.tokenBucketParams.trTcmParams.pbs;
            break;
        default:
            DEBUG("%s: undefined meter mode: %d", __FUNCTION__, entry.meterMode);
    }

    return POLICER_OK;
}

policer_status_t
set_qos
(
    CPSS_DXCH3_POLICER_METERING_ENTRY_STC *entry,
    const struct policer_params           *policer_params,
    int                                    policer_ix
)
{
    // set modify param type
    entry->modifyDscp = CPSS_PACKET_ATTRIBUTE_MODIFY_DISABLE_E;
    entry->modifyUp   = CPSS_PACKET_ATTRIBUTE_MODIFY_DISABLE_E;
    entry->modifyDp   = CPSS_PACKET_ATTRIBUTE_MODIFY_DISABLE_E;

    qos_profile_id_t qos_profile_id;
    policer_status_t qos_rc;

    // create qos profile
    switch (policer_params->exceed_action.type) {
        case POLICER_ACTION_SET_DSCP:
        case POLICER_ACTION_SET_COS:
            qos_rc =
                add_qos_profile(&qos_profile_id, policer_params, policer_ix);

            if (qos_rc != POLICER_OK) {
                DEBUG("%s: add_qos_profile() is failed", __FUNCTION__);
                return POLICER_CREATE_ERR;
            }
            break;
    }

    // assign created qos profile
    switch (policer_params->exceed_action.type) {
        case POLICER_ACTION_SET_DSCP: // edit dscp
            entry->modifyDscp = CPSS_PACKET_ATTRIBUTE_MODIFY_ENABLE_E;
            entry->qosProfile = qos_profile_id;
            entry->remarkMode = CPSS_DXCH_POLICER_REMARK_MODE_L3_E;
            break;
        case POLICER_ACTION_SET_COS: // edit up
            entry->modifyUp   = CPSS_PACKET_ATTRIBUTE_MODIFY_ENABLE_E;
            entry->qosProfile = qos_profile_id;
            entry->remarkMode = CPSS_DXCH_POLICER_REMARK_MODE_L2_E;
            break;
    }

    // up or tc
    if (policer_params->dest == POLICER_DEST_EGRESS &&
            policer_params->exceed_action.type == POLICER_ACTION_SET_COS
        )
    {
        int dev_num;
        GT_STATUS rc;
        for_each_dev(dev_num) {
            rc = cpssDxChPolicerEgressL2RemarkModelSet(
                    dev_num,
                    CPSS_DXCH_POLICER_L2_REMARK_MODEL_UP_E);

            if (rc != GT_OK) {
                DEBUG("cpssDxChPolicerEgressL2RemarkModelSet failed %u", rc);
                return POLICER_CREATE_ERR;
            }
        }
    }

    return POLICER_OK;
}

int is_policer_exist(int policer_ix, policer_dest_t dest)
{
    uint8_t *ixs = (dest == POLICER_DEST_INGRESS) ? ixs_ingress : ixs_egress;

    uint32_t arr_ix = policer_ix - ixs_start_pos;

    if (ixs[arr_ix] == FREE) {
        return 0;
    }

    return 1;
}
