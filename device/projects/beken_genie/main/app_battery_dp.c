#include <stdint.h>
#include <stdbool.h>
#include <components/log.h>
#include <os/os.h>

#include "app_battery_dp.h"
#include "app_dp_handler.h"   /* app_dp_battery_sample / app_dp_report_* */

#define TAG "app_bat_dp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* 5 s — fast enough that charge plug/unplug reports within 5s feels
 * responsive, slow enough that a 100% battery doesn't hammer the broker.
 * Actual battery % only changes minutes apart; hysteresis below filters
 * out per-tick noise. */
#define BAT_TICK_MS                5000

/* Hysteresis: only report battery % when it moves by ≥ 5 from last
 * reported value. ADC noise on this board flaps ±2-3 in steady state
 * (cloud log showed 49/51/49/51/48/53 within minutes), so threshold 2
 * was too tight. 5 covers the noise floor with margin.
 * Charge status is bool — no hysteresis needed; edge detect alone. */
#define BAT_PCT_REPORT_THRESHOLD   5

/* Floor on report rate: even when delta exceeds the threshold, never
 * publish % more than once per minute. Real battery doesn't drain that
 * fast, and this caps cloud writes during noise bursts that briefly
 * straddle the threshold. */
#define BAT_PCT_MIN_INTERVAL_MS    (60u * 1000u)

/* Heartbeat: force-report % every 10 min even if unchanged, so the
 * cloud dashboard timestamp stays fresh and ops can tell the device
 * is still alive on a stable charge. */
#define BAT_PCT_HEARTBEAT_MS       (10u * 60u * 1000u)

/* 0xFF sentinel — first tick always reports because no real % == 0xFF
 * and (any value vs 0xFF) trivially exceeds BAT_PCT_REPORT_THRESHOLD.
 * Same idea for charging: 0xFF != true && 0xFF != false. */
#define SENTINEL_PCT               0xFF
#define SENTINEL_CHARGING          0xFF

static beken_timer_t s_timer;
static uint8_t       s_last_pct           = SENTINEL_PCT;
static uint8_t       s_last_charging      = SENTINEL_CHARGING;
static uint32_t      s_last_pct_report_ms = 0;

static void on_tick(void *arg)
{
    (void)arg;

    unsigned char pct = 0;
    bool charging = false;
    app_dp_battery_sample(&pct, &charging);

    /* charge_status edge detect */
    if (s_last_charging == SENTINEL_CHARGING ||
        (uint8_t)charging != s_last_charging) {
        LOGI("charge edge: %u → %u\n",
             s_last_charging == SENTINEL_CHARGING ? 0xFFu : s_last_charging,
             (unsigned)charging);
        s_last_charging = (uint8_t)charging;
        app_dp_report_charge_status(charging);
    }

    /* battery % decision: hysteresis + min-interval floor + heartbeat ceiling.
     * - first tick always reports (sentinel)
     * - otherwise report iff (|Δ| ≥ threshold AND ≥ MIN_INTERVAL since last)
     *   OR HEARTBEAT elapsed since last (covers stable-charge case) */
    uint32_t now = (uint32_t)rtos_get_time();
    int diff = (int)pct - (int)s_last_pct;
    if (diff < 0) diff = -diff;

    bool first         = (s_last_pct == SENTINEL_PCT);
    uint32_t since     = first ? 0 : (now - s_last_pct_report_ms);
    bool delta_ok      = !first && diff >= BAT_PCT_REPORT_THRESHOLD;
    bool interval_ok   = !first && since >= BAT_PCT_MIN_INTERVAL_MS;
    bool heartbeat_due = !first && since >= BAT_PCT_HEARTBEAT_MS;

    if (first || (delta_ok && interval_ok) || heartbeat_due) {
        const char *reason = first ? "first"
                           : heartbeat_due ? "heartbeat"
                           : "delta";
        LOGI("battery %%: %u → %u (Δ=%d, %ums since last, reason=%s)\n",
             first ? 0xFFu : s_last_pct, pct, diff, (unsigned)since, reason);
        s_last_pct = pct;
        s_last_pct_report_ms = now;
        app_dp_report_battery_percentage(pct);
    }
}

void app_battery_dp_init(void)
{
    bk_err_t err = rtos_init_timer(&s_timer, BAT_TICK_MS, on_tick, NULL);
    if (kNoErr != err) {
        LOGE("rtos_init_timer failed: %d\n", err);
        return;
    }
    err = rtos_start_timer(&s_timer);
    if (kNoErr != err) {
        LOGE("rtos_start_timer failed: %d\n", err);
        return;
    }
    LOGI("battery DP poll started (period=%ums, hysteresis=%d, "
         "min-interval=%ums, heartbeat=%ums)\n",
         BAT_TICK_MS, BAT_PCT_REPORT_THRESHOLD,
         BAT_PCT_MIN_INTERVAL_MS, BAT_PCT_HEARTBEAT_MS);
}
