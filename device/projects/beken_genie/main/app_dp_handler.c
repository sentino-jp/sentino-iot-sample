#include <string.h>
#include <components/log.h>

#include "app_dp_handler.h"
#include "sentino_mqtt_import.h"

#define TAG "app_dp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static void on_dp_set(const dp_obj_t *dp)
{
    /* Skeleton: log + accept. Wire real actuators (Volume_Set_Abs, led
     * blink, etc.) here. The SDK already echoes these properties back to
     * cloud via issue_response (ref-mqtt §5.4) so we don't need to call
     * Sentino_Dp_Report from here unless the actuator clamps/normalizes
     * the value differently from what cloud sent. */
    if (0 == strcmp(dp->identifier, DP_ID_SWITCH)) {
        LOGI("DP %s <- %s", dp->identifier, dp->v.b ? "on" : "off");
    } else if (0 == strcmp(dp->identifier, DP_ID_VOLUME_SET)) {
        LOGI("DP %s <- %ld", dp->identifier, (long)dp->v.i);
    } else if (0 == strcmp(dp->identifier, DP_ID_BATTERY_PERCENTAGE) ||
               0 == strcmp(dp->identifier, DP_ID_CHARGE_STATUS)) {
        LOGW("DP %s is read-only — ignoring cloud-set", dp->identifier);
    } else {
        LOGW("DP unknown identifier=%s (type=%d)", dp->identifier, dp->type);
    }
}

void app_dp_handler_init(void)
{
    Register_Sentino_Dp_Set_Cb(on_dp_set);
    LOGI("DP handler registered (identifiers: %s/%s/%s/%s)",
         DP_ID_SWITCH, DP_ID_BATTERY_PERCENTAGE,
         DP_ID_VOLUME_SET, DP_ID_CHARGE_STATUS);
}
