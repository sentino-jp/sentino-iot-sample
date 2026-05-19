#include <common/sys_config.h>

#if CONFIG_SENTINO_IOT && CONFIG_ENABLE_AGORA_DATASTREAM

#include <string.h>
#include <components/log.h>

#include "cJSON.h"
#include "sentino_command_router.h"
#include "sentino_mqtt_import.h"
#include "app_dp_handler.h"
#include "genie_action_volume_set.h"

#define TAG "act_vol_set"

/* Reference impl of a DP-overlap action handler. AI agent emits
 *   { "executor":"volume_set", "parameters":{"value":<int 0-10>} }
 * we build a dp_obj_t and route it through app_dp_apply_set, which is
 * the same code path cloud-side property_set takes. No actuator code
 * here — range clamp / level mapping / hardware apply / echo report
 * all live in app_dp_handler.c and run once regardless of trigger. */
static int handler(const cJSON *parameters, int priority)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(parameters, "value");
    if (!cJSON_IsNumber(v)) {
        BK_LOGW(TAG, "missing/non-number parameters.value\n");
        return -1;
    }

    dp_obj_t dp;
    memset(&dp, 0, sizeof(dp));
    strncpy(dp.identifier, DP_ID_VOLUME_SET, DP_IDENTIFIER_MAX - 1);
    dp.type = DP_TYPE_INT;
    dp.v.i  = (int32_t)v->valuedouble;

    BK_LOGW(TAG, "priority=%d value=%ld → app_dp_apply_set\n",
            priority, (long)dp.v.i);
    app_dp_apply_set(&dp);
    return 0;
}

void genie_action_volume_set_register(void)
{
    sentino_command_router_register_action(DP_ID_VOLUME_SET, handler);
}

#endif /* CONFIG_SENTINO_IOT && CONFIG_ENABLE_AGORA_DATASTREAM */
