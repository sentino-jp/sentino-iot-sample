#include <string.h>
#include <components/log.h>

#include "app_dp_handler.h"
#include "sentino_mqtt_import.h"

#define TAG "app_dp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

/* Build a typed dp_obj_t for a report-back. Caller fills the union value. */
static void make_echo(dp_obj_t *out, const char *identifier, dp_type_e type)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->identifier, identifier, DP_IDENTIFIER_MAX - 1);
    out->type = type;
}

static void on_dp_set(const dp_obj_t *dp)
{
    /* Apply actuator + report the *applied* value back via property_report.
     * The SDK only echoes via issue_response which does NOT update cloud-side
     * DP state — without this report cloud's getDpInfos / app UI would show
     * a stale value forever (verified against real cloud 2026-04-24). */

    if (0 == strcmp(dp->identifier, DP_ID_SWITCH)) {
        if (dp->type != DP_TYPE_BOOL) {
            LOGW("DP %s: expected BOOL, got type=%d — ignoring",
                 dp->identifier, dp->type);
            return;
        }
        bool v = dp->v.b;
        /* TODO: wire actual switch hardware (relay, GPIO, etc.) */
        LOGI("DP %s <- %s", dp->identifier, v ? "on" : "off");

        dp_obj_t echo;
        make_echo(&echo, DP_ID_SWITCH, DP_TYPE_BOOL);
        echo.v.b = v;
        Sentino_Dp_Report_Export(&echo);

    } else if (0 == strcmp(dp->identifier, DP_ID_VOLUME_SET)) {
        if (dp->type != DP_TYPE_INT) {
            LOGW("DP %s: expected INT, got type=%d — ignoring",
                 dp->identifier, dp->type);
            return;
        }
        /* Cloud doesn't validate range — clamp to model's [0,10] before apply.
         * Reporting the clamped value back keeps cloud's view of truth. */
        int32_t v = dp->v.i;
        if (v < 0)  v = 0;
        if (v > 10) v = 10;
        if (v != dp->v.i) {
            LOGW("DP %s clamped %ld -> %ld",
                 dp->identifier, (long)dp->v.i, (long)v);
        }
        /* TODO: wire actual volume control (Volume_Set_Abs etc.) */
        LOGI("DP %s <- %ld", dp->identifier, (long)v);

        dp_obj_t echo;
        make_echo(&echo, DP_ID_VOLUME_SET, DP_TYPE_INT);
        echo.v.i = v;
        Sentino_Dp_Report_Export(&echo);

    } else if (0 == strcmp(dp->identifier, DP_ID_BATTERY_PERCENTAGE) ||
               0 == strcmp(dp->identifier, DP_ID_CHARGE_STATUS)) {
        /* Both are read-only per cloud product model. Cloud usually won't
         * send these (UI hides the controls), but the propsIssue HTTP
         * endpoint does NOT enforce accessMode server-side, so a buggy
         * client can still inject these — reject defensively. */
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
