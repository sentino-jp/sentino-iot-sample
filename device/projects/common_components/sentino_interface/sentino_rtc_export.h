#ifndef __SENTINO_RTC_EXPORT_H__
#define __SENTINO_RTC_EXPORT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* sentino_interface — Sentino SDK → RTC backend handoff.
 *
 * Sentino's MQTT control plane fetches RTC session params (appId, token,
 * channel, uid). The SDK does NOT know which RTC backend (Agora / Volc /
 * future) is compiled in — it just calls a registered handoff callback.
 *
 * sentino_rtc_export.c implements that callback for whichever backend is
 * built (selected by CONFIG_AGORA_IOT_SDK / future CONFIG_VOLC_RTC_EN)
 * and registers it via sentino_register_rtc_handoff() at boot.
 *
 * Call sentino_rtc_export_init() once at boot before
 * sentino_iot_engine_start(). */
void sentino_rtc_export_init(void);

/* Stop the active RTC session (wraps sentino_iot_engine_stop). MQTT
 * stays connected; only the audio data plane drops. Safe to call when
 * no session is active. */
void Sentino_Stop_Session_Export(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_RTC_EXPORT_H__ */
