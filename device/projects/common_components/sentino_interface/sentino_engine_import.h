#ifndef __SENTINO_ENGINE_IMPORT_H__
#define __SENTINO_ENGINE_IMPORT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* sentino_interface — Sentino IoT engine lifecycle bridge.
 *
 * The ONLY header business code (app_event, key_app, etc.) should include
 * to drive the Sentino engine lifecycle. Direct includes of
 * sentino_iot_engine.h from outside the SDK defeat the layering.
 *
 * The thin wrappers below mirror the SDK API names minus the "_iot_"
 * infix — adapter convention. RTC handoff/release cb registration lives
 * in sentino_rtc_export.c (boot-time wiring, not business-driven). */

/* Load triple, read provisioning, connect MQTT, publish bind+info.
 * Triggered on WiFi-up. Idempotent at the engine level. */
void sentino_engine_init(void);

/* Request RTC session params via MQTT, then invoke the registered RTC
 * handoff callback. Triggered when user starts a conversation. */
void sentino_engine_start(void);

/* Invoke the registered RTC release callback. MQTT stays connected. */
void sentino_engine_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_ENGINE_IMPORT_H__ */
