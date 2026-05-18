#ifndef __SENTINO_MQTT_IMPORT_H__
#define __SENTINO_MQTT_IMPORT_H__

/* sentino_interface — cloud → business import callbacks for MQTT events.
 *
 * Today this is just the DP set handler. Future: bind/info ack hooks,
 * OTA progress, ping/pong, custom event codes — all centralized here so
 * business code never includes the SDK headers directly.
 *
 * Re-exports dp_obj_t / dp_type_e from the SDK so business code can
 * #include just this one header. */

#include "sentino_mqtt_dp.h"   /* dp_obj_t, dp_type_e, dp_set_cb_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Register the business-side handler invoked when the cloud issues
 * property_set. Latest call wins (NULL clears). Wraps the SDK's
 * sentino_register_dp_set_cb so business code never depends on the
 * SDK header directly — just on this adapter header. */
void Register_Sentino_Dp_Set_Cb(dp_set_cb_t cb);

/* Convenience: report a single typed DP value to cloud. Wraps
 * Sentino_Dp_Report. Same naming convention as the eventual
 * sentino_mqtt_export.h family. */
int  Sentino_Dp_Report_Export(const dp_obj_t *dp);

/* Batched property report — one MQTT message with all DPs grouped under
 * data.properties (ref-mqtt §4.6). Use when several values change at
 * once (e.g. boot snapshot of switch+volume+battery). */
int  Sentino_Dp_Report_Many_Export(const dp_obj_t *dps, size_t count);

/* NFC card scan event — wraps sentino_mqtt_publish_nfc_report. nfc_id is
 * the raw bytes from the card; only_report=1 means upload-only (no RTC
 * session start), 0 means request RTC params (cloud replies with the
 * same shape as agora_agent_device_access — see ref-mqtt §4.9). */
int  Sentino_Nfc_Report_Export(const unsigned char *nfc_id, int len, int only_report);

/* Business hook fired AFTER engine has pushed bind+info on a fresh mqtts
 * CONNECTED. property_report from the handler arrives AFTER bind/info on
 * the wire (worker serializes them). Does NOT wait for any bind ack —
 * cloud accepts property_report based on device credential alone (上游
 * bk7258aitoypro Rino_Mqtt_Connected_Callback 已实测).
 *
 * Fires on every CONNECTED (boot + each reconnect). Handler MUST be
 * idempotent and apply its own throttling. Runs in app_event worker
 * context — safe to publish synchronously here. */
void Register_Sentino_Cloud_Ready_Cb(void (*cb)(void));

/* Register a handler for the cloud's response to publish_bind (code=bind,
 * res=N). res==0 means bind succeeded. Wraps the SDK's
 * sentino_mqtt_register_bind_ack_cb. Same context rule — don't publish
 * synchronously from the cb. */
void Register_Sentino_Bind_Ack_Cb(void (*cb)(int res));

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_MQTT_IMPORT_H__ */
