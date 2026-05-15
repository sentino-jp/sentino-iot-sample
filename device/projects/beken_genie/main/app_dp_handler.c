#include <stdint.h>
#include <string.h>
#include <components/log.h>
#include <os/os.h>

#include "app_dp_handler.h"
#include "app_main.h"               /* volume_set_abs / volume_get_current / volume_get_level_count */
#include "sentino_mqtt_import.h"
#include "bat_monitor.h"            /* battery_get_charge_level / battery_if_is_charging */

#define TAG "app_dp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

/* ────────────────────────────────────────────────────────────────────
 *  DP value cache
 *
 *  switch is a soft state (no GPIO on this product); battery + charge are
 *  sampled on demand from BK driver. volume reads back from key_app_service
 *  state, so no cache needed.
 * ──────────────────────────────────────────────────────────────────── */

static bool s_switch_state = false;

/* Snapshot cooldown: simple bool sentinel beats integer tricks.
 * The "INT32_MIN sentinel" approach was tried first but invokes signed
 * integer overflow UB on the first (now - s_last) — GCC wraps it
 * negative, so the first snapshot was wrongly cooldown-skipped on real
 * hardware (verified 2026-05-15). uint32 wraparound here is well-
 * defined; first_done gates the cooldown until we've actually pushed
 * once. */
static bool     s_snapshot_done = false;
static uint32_t s_last_snapshot_ms = 0;
#define DP_SNAPSHOT_COOLDOWN_MS    120000   /* 2 minutes */

/* ────────────────────────────────────────────────────────────────────
 *  Volume range mapping
 *
 *  Cloud DP `volume_set` is specified as [0,10]. Local SPK_VOLUME_LEVEL
 *  is not necessarily 11 (currently is, but don't bake the equality in).
 *  Map both directions to keep cloud's view of truth aligned with the
 *  device's actual gain step — without this, set 5 → device floor → cloud
 *  still shows 5 → app slider drifts.
 * ──────────────────────────────────────────────────────────────────── */

static int32_t local_to_cloud_volume(uint8_t local_lv)
{
    uint32_t max = volume_get_level_count();
    if (max <= 1) return 0;
    return (int32_t)(((uint32_t)local_lv * 10 + (max - 1) / 2) / (max - 1));
}

static uint8_t cloud_to_local_volume(int32_t cv)
{
    uint32_t max = volume_get_level_count();
    if (max <= 1) return 0;
    if (cv < 0)  cv = 0;
    if (cv > 10) cv = 10;
    return (uint8_t)(((uint32_t)cv * (max - 1) + 5) / 10);
}

/* ────────────────────────────────────────────────────────────────────
 *  Driver sample helper (shared with app_battery_dp.c)
 * ──────────────────────────────────────────────────────────────────── */

void app_dp_battery_sample(unsigned char *out_pct, bool *out_charging)
{
    if (out_pct) {
        uint8_t pct = 0;
        if (battery_get_charge_level(&pct) == 0) {  /* IOT_BATTERY_SUCCESS == 0 */
            *out_pct = pct;
        } else {
            *out_pct = 0;
        }
    }
    if (out_charging) {
        *out_charging = battery_if_is_charging();
    }
}

/* ────────────────────────────────────────────────────────────────────
 *  Build typed dp_obj_t
 * ──────────────────────────────────────────────────────────────────── */

static void dp_set_bool(dp_obj_t *out, const char *id, bool v)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->identifier, id, DP_IDENTIFIER_MAX - 1);
    out->type = DP_TYPE_BOOL;
    out->v.b  = v;
}

static void dp_set_int(dp_obj_t *out, const char *id, int32_t v)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->identifier, id, DP_IDENTIFIER_MAX - 1);
    out->type = DP_TYPE_INT;
    out->v.i  = v;
}

/* ────────────────────────────────────────────────────────────────────
 *  on_dp_set — cloud → device property_set handler
 * ──────────────────────────────────────────────────────────────────── */

static void on_dp_set(const dp_obj_t *dp)
{
    /* Apply actuator + report the *applied* value back via property_report.
     * The SDK only echoes via issue_response which does NOT update cloud-side
     * DP state — without this report cloud's getDpInfos / app UI would show
     * a stale value forever (verified against real cloud 2026-04-24). */

    if (0 == strcmp(dp->identifier, DP_ID_SWITCH)) {
        if (dp->type != DP_TYPE_BOOL) {
            LOGW("DP %s: expected BOOL, got type=%d — ignoring\n",
                 dp->identifier, dp->type);
            return;
        }
        /* Soft state — no GPIO on this product (parallel to bk7258aitoypro
         * Dev_Dp_Obj_Type_Bool_Handle which is also software-only). */
        s_switch_state = dp->v.b;
        LOGI("DP %s <- %s\n", dp->identifier, s_switch_state ? "on" : "off");

        dp_obj_t echo;
        dp_set_bool(&echo, DP_ID_SWITCH, s_switch_state);
        Sentino_Dp_Report_Export(&echo);

    } else if (0 == strcmp(dp->identifier, DP_ID_VOLUME_SET)) {
        if (dp->type != DP_TYPE_INT) {
            LOGW("DP %s: expected INT, got type=%d — ignoring\n",
                 dp->identifier, dp->type);
            return;
        }
        /* Map cloud range [0,10] to local SPK_VOLUME_LEVEL step, apply to
         * audio gain, then report back the *mapped* value the device
         * actually landed on. This keeps cloud's view aligned with the
         * mapped step rather than the requested raw — set=5 on a 5-step
         * device should report 5 back, not 5→2→reported 4. */
        uint8_t local_lv = cloud_to_local_volume(dp->v.i);
        volume_set_abs(local_lv, 0);
        int32_t echoed_cloud = local_to_cloud_volume((uint8_t)volume_get_current());
        LOGI("DP %s <- cloud=%ld → local=%u → echo cloud=%ld\n",
             dp->identifier, (long)dp->v.i, local_lv, (long)echoed_cloud);

        dp_obj_t echo;
        dp_set_int(&echo, DP_ID_VOLUME_SET, echoed_cloud);
        Sentino_Dp_Report_Export(&echo);

    } else if (0 == strcmp(dp->identifier, DP_ID_BATTERY_PERCENTAGE) ||
               0 == strcmp(dp->identifier, DP_ID_CHARGE_STATUS)) {
        /* Both are read-only per cloud product model. Cloud usually won't
         * send these (UI hides the controls), but the propsIssue HTTP
         * endpoint does NOT enforce accessMode server-side, so a buggy
         * client can still inject these — reject defensively. */
        LOGW("DP %s is read-only — ignoring cloud-set\n", dp->identifier);

    } else {
        LOGW("DP unknown identifier=%s (type=%d)\n", dp->identifier, dp->type);
    }
}

/* ────────────────────────────────────────────────────────────────────
 *  app_dp_report_snapshot — full 4-DP batch on cloud-ready
 * ──────────────────────────────────────────────────────────────────── */

void app_dp_report_snapshot(void)
{
    uint32_t now = (uint32_t)rtos_get_time();
    if (s_snapshot_done && (now - s_last_snapshot_ms) < DP_SNAPSHOT_COOLDOWN_MS) {
        LOGI("snapshot skipped (cooldown, %u ms since last)\n",
             (unsigned)(now - s_last_snapshot_ms));
        return;
    }
    s_last_snapshot_ms = now;
    s_snapshot_done = true;

    unsigned char pct = 0;
    bool charging = false;
    app_dp_battery_sample(&pct, &charging);

    dp_obj_t arr[4];
    dp_set_bool(&arr[0], DP_ID_SWITCH,             s_switch_state);
    dp_set_int (&arr[1], DP_ID_VOLUME_SET,         local_to_cloud_volume((uint8_t)volume_get_current()));
    dp_set_int (&arr[2], DP_ID_BATTERY_PERCENTAGE, (int32_t)pct);
    dp_set_bool(&arr[3], DP_ID_CHARGE_STATUS,      charging);

    int rc = Sentino_Dp_Report_Many_Export(arr, 4);
    LOGI("snapshot pushed (rc=%d): switch=%d vol=%ld pct=%u charge=%d\n",
         rc, s_switch_state,
         (long)local_to_cloud_volume((uint8_t)volume_get_current()),
         pct, charging);
}

/* ────────────────────────────────────────────────────────────────────
 *  Single-DP push (used by app_battery_dp.c on edge/delta events)
 * ──────────────────────────────────────────────────────────────────── */

void app_dp_report_charge_status(bool charging)
{
    dp_obj_t dp;
    dp_set_bool(&dp, DP_ID_CHARGE_STATUS, charging);
    Sentino_Dp_Report_Export(&dp);
}

void app_dp_report_battery_percentage(unsigned char pct)
{
    dp_obj_t dp;
    dp_set_int(&dp, DP_ID_BATTERY_PERCENTAGE, (int32_t)pct);
    Sentino_Dp_Report_Export(&dp);
}

/* ────────────────────────────────────────────────────────────────────
 *  Volume key debounce
 *
 *  A single user gesture ("turn it up to 10") usually arrives as 5 quick
 *  VOLUME_UP taps; reporting each tap floods the broker with intermediate
 *  values that get instantly overwritten. Coalesce: each call resets a
 *  500 ms one-shot; only the final value is published.
 * ──────────────────────────────────────────────────────────────────── */

#define VOLUME_REPORT_DEBOUNCE_MS  500

static beken2_timer_t s_vol_timer;
static uint8_t        s_vol_pending;

static void vol_debounce_fire(void *larg, void *rarg)
{
    (void)larg; (void)rarg;
    int32_t cloud_v = local_to_cloud_volume(s_vol_pending);
    dp_obj_t dp;
    dp_set_int(&dp, DP_ID_VOLUME_SET, cloud_v);
    Sentino_Dp_Report_Export(&dp);
    LOGI("volume report (debounced): local=%u → cloud=%ld\n",
         s_vol_pending, (long)cloud_v);
}

void app_dp_request_volume_report(unsigned char local_level)
{
    s_vol_pending = local_level;

    if (rtos_is_oneshot_timer_init(&s_vol_timer)) {
        /* Reload-ex resets the period and reuses the existing timer
         * handle — extends the debounce window to the latest tap. */
        rtos_oneshot_reload_timer_ex(&s_vol_timer,
                                     VOLUME_REPORT_DEBOUNCE_MS,
                                     vol_debounce_fire, NULL, NULL);
    } else {
        bk_err_t err = rtos_init_oneshot_timer(&s_vol_timer,
                                               VOLUME_REPORT_DEBOUNCE_MS,
                                               vol_debounce_fire, NULL, NULL);
        if (kNoErr != err) {
            LOGW("vol debounce init failed: %d — publishing immediately\n", err);
            vol_debounce_fire(NULL, NULL);
            return;
        }
        rtos_start_oneshot_timer(&s_vol_timer);
    }
}

/* ────────────────────────────────────────────────────────────────────
 *  Init
 *
 *  MUST run BEFORE sentino_iot_engine_init() — the engine fires the
 *  cloud-ready cb on the first MQTT CONNECTED, which can happen
 *  synchronously inside engine_init. Register late = miss first
 *  snapshot. See app_dp_handler.h docstring.
 * ──────────────────────────────────────────────────────────────────── */

void app_dp_handler_init(void)
{
    Register_Sentino_Dp_Set_Cb(on_dp_set);
    Register_Sentino_Cloud_Ready_Cb(app_dp_report_snapshot);
    LOGI("DP handler registered (identifiers: %s/%s/%s/%s)\n",
         DP_ID_SWITCH, DP_ID_BATTERY_PERCENTAGE,
         DP_ID_VOLUME_SET, DP_ID_CHARGE_STATUS);
}
