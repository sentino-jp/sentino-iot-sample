#ifndef __SENTINO_IOT_ENGINE_H__
#define __SENTINO_IOT_ENGINE_H__

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
