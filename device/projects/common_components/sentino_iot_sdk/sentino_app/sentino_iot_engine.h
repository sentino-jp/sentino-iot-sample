#ifndef __SENTINO_IOT_ENGINE_H__
#define __SENTINO_IOT_ENGINE_H__

#include <stdint.h>         /* uint8_t */
#include "sentino_mqtt.h"   /* sentino_rtc_params_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Sentino IoT control plane.
 *
 * Owns the MQTT signalling lifecycle (bind/info/issue/report) and the
 * triple/provisioning NVS records. The engine asks the MQTT layer for RTC
 * session params, then hands them off via a registered callback — the SDK
 * does NOT directly know about the RTC backend (Agora today, Volc planned).
 *
 * The adapter that owns the RTC backend (sentino_interface/) registers its
 * handoff/release functions at boot. */

/* Module bring-up: register CLI (set_triple/get_triple/reset_triple).
 * Safe to call once at boot regardless of provisioning state. */
void sentino_iot_init(void);

/* Load triple, read provisioning, connect MQTT, publish bind+info.
 * Triggered on WiFi-up by the app event loop. */
void sentino_iot_engine_init(void);

/* Request RTC session params via MQTT, then invoke the registered RTC
 * handoff callback. Triggered when user starts a conversation. */
void sentino_iot_engine_start(void);

/* Invoke the registered RTC release callback. MQTT stays connected. */
void sentino_iot_engine_stop(void);

/* RTC backend handoff hooks. The adapter (sentino_interface/) registers
 * these once at boot. The engine treats the RTC backend as opaque. */
typedef int  (*sentino_rtc_handoff_cb_t)(const sentino_rtc_params_t *p);
typedef void (*sentino_rtc_release_cb_t)(void);

void sentino_register_rtc_handoff(sentino_rtc_handoff_cb_t cb);
void sentino_register_rtc_release(sentino_rtc_release_cb_t cb);

/* WiFi link telemetry hook used by the cloud `ping` issue. Returns 0 on
 * success, non-zero on failure. `level` is 1/2/3 (good/mid/poor),
 * `quality` is 0~100 (signal percentage). Adapter (sentino_interface/)
 * registers a BK-specific impl at boot; if unregistered the engine
 * replies to ping with res=-1. Lifted from bk7258aitoypro Rino SDK
 * 2.0.x Bsp_Wifi_Load_Signal_Level_Quality. */
typedef int (*sentino_wifi_signal_query_fn_t)(uint8_t *level, uint8_t *quality);
void sentino_engine_register_wifi_signal_query(sentino_wifi_signal_query_fn_t fn);

/* Business hook for the cloud-issued `clean_data` command (ref-mqtt §5.5).
 * Fires after a successful bind to ask the device to clear ONLY pre-bind
 * temp data — offline log queues, provisioning-stage scratch, etc.
 * Network config / triple / user-asset association must NOT be touched
 * (that's `reset`'s job, §5.1). The handler receives `sub_uuid`: NULL
 * means clean this device, non-NULL means clean a specific sub-device
 * (gateway scenario). ack=0 — no response is sent. If unregistered, the
 * cloud command is silently dropped (logged). */
typedef void (*sentino_clean_data_handler_fn_t)(const char *sub_uuid);
void sentino_engine_register_clean_data_handler(sentino_clean_data_handler_fn_t fn);

/* Business hook fired AFTER the engine has pushed bind+info on a fresh
 * mqtts CONNECTED. Guarantees subsequent business publish (e.g.
 * property_report snapshot) arrives after bind/info on the wire — the
 * worker serializes them. Does NOT wait for any bind ack — cloud accepts
 * property_report based on device credential alone (实测 alignment with
 * bk7258aitoypro Rino_Mqtt_Connected_Callback).
 *
 * Fires on every CONNECTED (boot + each reconnect). Handler must be
 * idempotent and apply its own throttling. Runs in app_event worker
 * context — safe to publish synchronously here. Latest call wins. */
void sentino_engine_register_cloud_ready_cb(void (*cb)(void));

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_IOT_ENGINE_H__ */
