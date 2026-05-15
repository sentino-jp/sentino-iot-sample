#ifndef __APP_BATTERY_DP_H__
#define __APP_BATTERY_DP_H__

/* Periodic battery + charge sampling that pushes the read-only DPs
 * (battery_percentage / charge_status) to cloud on change.
 *
 * Why timer (not driver event cb):
 *   $BK_AIDK bat_monitor.c fires EVT_BATTERY_CHARGING level-triggered every
 *   poll period (no edge dedupe) and never fires a "discharging" event,
 *   so we'd miss every unplug. Cleaner to poll + own the edge state here.
 *
 * Call once after battery_monitor_init() — see app_main.c CONFIG_BAT_MONITOR
 * block. */

#ifdef __cplusplus
extern "C" {
#endif

void app_battery_dp_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_BATTERY_DP_H__ */
