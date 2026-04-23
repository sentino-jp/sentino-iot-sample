#include <components/log.h>

#include "app_dp_handler.h"
#include "sentino_mqtt_import.h"

#define TAG "app_dp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static void on_dp_set(const dp_obj_t *dp)
{
    /* Skeleton: log + reflect. Wire actual actions (Volume_Set_Abs,
     * led blink, etc.) when the cloud schema for property_set lands. */
    switch (dp->dpid) {
        case DPID_SWITCH:
            LOGI("DP switch <- %s\n", dp->v.b ? "on" : "off");
            break;
        case DPID_BATTERY_PERCENT:
            LOGW("DP battery_percent is read-only; cloud should not set it (got %ld)\n",
                 (long)dp->v.i);
            return;
        case DPID_VOLUME_SET:
            LOGI("DP volume <- %ld\n", (long)dp->v.i);
            break;
        case DPID_CHARGE_STATUS:
            LOGW("DP charge_status is read-only; cloud should not set it (got %ld)\n",
                 (long)dp->v.i);
            return;
        default:
            LOGW("DP unknown dpid=%u\n", dp->dpid);
            return;
    }

    /* Reflect the new value back to cloud so shadow stays consistent.
     * Once real actuators are wired, gate this on the action succeeding. */
    Sentino_Dp_Report_Export(dp);
}

void app_dp_handler_init(void)
{
    Register_Sentino_Dp_Set_Cb(on_dp_set);
    LOGI("DP handler registered\n");
}
