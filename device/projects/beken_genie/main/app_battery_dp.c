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

/* Hysteresis: only report battery % when it moves by ≥ 2 from last
 * reported value. Stops 76↔77↔76 noise from spamming property_report.
 * Charge status is bool — no hysteresis needed; edge detect alone. */
#define BAT_PCT_REPORT_THRESHOLD   2

/* 0xFF sentinel — first tick always reports because no real % == 0xFF
 * and (any value vs 0xFF) trivially exceeds BAT_PCT_REPORT_THRESHOLD.
 * Same idea for charging: 0xFF != true && 0xFF != false. */
#define SENTINEL_PCT               0xFF
#define SENTINEL_CHARGING          0xFF

static beken_timer_t s_timer;
static uint8_t       s_last_pct      = SENTINEL_PCT;
static uint8_t       s_last_charging = SENTINEL_CHARGING;

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

    /* battery % hysteresis (signed diff to handle 0xFF sentinel naturally) */
    int diff = (int)pct - (int)s_last_pct;
    if (diff < 0) diff = -diff;
    if (s_last_pct == SENTINEL_PCT || diff >= BAT_PCT_REPORT_THRESHOLD) {
        LOGI("battery %%: %u → %u\n",
             s_last_pct == SENTINEL_PCT ? 0xFFu : s_last_pct, pct);
        s_last_pct = pct;
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
    LOGI("battery DP poll started (period=%u ms, hysteresis=%d)\n",
         BAT_TICK_MS, BAT_PCT_REPORT_THRESHOLD);
}
