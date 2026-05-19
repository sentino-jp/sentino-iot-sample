#ifndef __APP_DP_HANDLER_H__
#define __APP_DP_HANDLER_H__

#include <stdbool.h>
#include "sentino_mqtt_import.h"        /* dp_obj_t */

/* Business-side DP (Thing-Model) handler.
 *
 * Owns the product's identifier set and the on-property-set switch.
 * Registers itself with sentino_interface at boot so cloud→device
 * property_set messages land here as typed dp_obj_t.
 *
 * For now this is a skeleton — the switch logs and reflects values back
 * via the issue_response that Sentino_Dp_Set_Parse already emits. Wire
 * the real volume/switch/charge actuators when needed. */

#ifdef __cplusplus
extern "C" {
#endif

/* Identifier strings match BUILD_GUIDE.md Thing-Model and the cloud
 * model registered against this PID. The cloud sends these as keys in
 * data.properties (ref-mqtt §5.4), so they must match exactly. */
#define DP_ID_SWITCH                "switch"
#define DP_ID_BATTERY_PERCENTAGE    "battery_percentage"
#define DP_ID_VOLUME_SET            "volume_set"
#define DP_ID_CHARGE_STATUS         "charge_status"

/* Register the handler with sentino_interface. Idempotent.
 *
 * MUST be called BEFORE sentino_iot_engine_init() — the engine fires the
 * cloud-ready cb (registered here) on the first MQTT CONNECTED, which
 * may happen synchronously inside engine_init. Register late = miss
 * first snapshot. */
void app_dp_handler_init(void);

/* Push current device-side values for the 4 known DPs in one batched
 * property_report. Idempotent. Internally throttled (cooldown) to avoid
 * reconnect storms. Registered as the sentino cloud-ready cb. */
void app_dp_report_snapshot(void);

/* Sample helper exported for app_battery_dp.c to share driver state. */
void app_dp_battery_sample(unsigned char *out_pct, bool *out_charging);

/* Single-DP push helpers used by app_battery_dp.c on edge/delta events.
 * Thin wrappers over Sentino_Dp_Report_Export. */
void app_dp_report_charge_status(bool charging);
void app_dp_report_battery_percentage(unsigned char pct);

/* Schedule a debounced volume report for a local-key change. The actual
 * MQTT publish runs ~500 ms after the LAST call, so a burst of taps
 * (+ + + + +) coalesces into one property_report carrying the final
 * value. Called by app_event worker on APP_EVT_VOLUME_CHANGED.
 *
 * Cloud-set path does NOT use this — it echoes synchronously via
 * Sentino_Dp_Report_Export, since cloud expects an immediate ack. */
void app_dp_request_volume_report(unsigned char local_level);

/* Apply one DP set as if it came from the cloud.
 *
 * This IS the function registered with Sentino_Dp_Set_Cb — exposed so
 * AI agent command handlers (sentino_command_router actions) can
 * converge on the same code path when their executor name matches a
 * cloud-controllable DP. Plan §6 "重叠 executor 合流" — same map / apply
 * / echo / property_report logic runs regardless of whether the trigger
 * came from cloud DP set or AI action.
 *
 * Caller builds a dp_obj_t with identifier / type / value matching the
 * Thing-Model, then calls this. The handler validates type and either
 * applies (rw DPs) or rejects (read-only DPs). */
void app_dp_apply_set(const dp_obj_t *dp);

#ifdef __cplusplus
}
#endif
#endif /* __APP_DP_HANDLER_H__ */
