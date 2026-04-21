#ifndef __SENTINO_IOT_ENGINE_H__
#define __SENTINO_IOT_ENGINE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Sentino IoT control plane.
 *
 * Owns the MQTT signalling lifecycle (bind/info/issue/report) and the
 * triple/provisioning NVS records. The engine asks the MQTT layer for RTC
 * session params, then hands them to whichever RTC backend is compiled in.
 *
 * Currently the only supported RTC backend is Agora (CONFIG_AGORA_IOT_SDK).
 * A future Volc-RTC variant will be selected with the same compile gate
 * once the cloud protocol settles.
 */

/* Module bring-up: register CLI (set_triple/get_triple/reset_triple).
 * Safe to call once at boot regardless of provisioning state. */
void sentino_iot_init(void);

/* Load triple, read provisioning, connect MQTT, publish bind+info.
 * Triggered on WiFi-up by the app event loop. */
void sentino_iot_engine_init(void);

/* Request RTC session params via MQTT, then start the underlying RTC
 * backend (Agora today). Triggered when user starts a conversation. */
void sentino_iot_engine_start(void);

/* Leave the RTC channel. MQTT stays connected. */
void sentino_iot_engine_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_IOT_ENGINE_H__ */
