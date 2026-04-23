#include <string.h>
#include <stdio.h>
#include <components/log.h>

#include "sentino_rtc_export.h"
#include "sentino_iot_engine.h"   /* sentino_register_rtc_* */
#include "sentino_mqtt.h"         /* sentino_rtc_params_t   */

#if CONFIG_AGORA_IOT_SDK
#include "agora_rtc_main.h"
#endif

#define TAG "sentino_rtc"

#if CONFIG_AGORA_IOT_SDK
/* Translate cloud-issued params → Agora's configs struct + extern globals,
 * then start the data plane. Same logic as the old engine code, just moved
 * out of the SDK so the SDK has zero compile-time dependency on Agora. */
static int agora_handoff_cb(const sentino_rtc_params_t *p)
{
    static agora_convoai_configs_resp_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.app_id,    sizeof(cfg.app_id),    "%s", p->app_id);
    snprintf(cfg.rtc_token, sizeof(cfg.rtc_token), "%s", p->rtc_token);
    cfg.token_enable = (cfg.rtc_token[0] != '\0' &&
                        0 != strcmp(cfg.app_id, cfg.rtc_token));
    snprintf(agora_channel_name, sizeof(agora_channel_name), "%s", p->channel_name);
    agora_rtc_option.uid = p->uid;
    BK_LOGI(TAG, "handoff agora: appid=%s, channel=%s, uid=%u\n",
            cfg.app_id, agora_channel_name, p->uid);
    return agora_start(&cfg);
}

static void agora_release_cb(void)
{
    agora_stop();
}
#endif /* CONFIG_AGORA_IOT_SDK */

void sentino_rtc_export_init(void)
{
#if CONFIG_AGORA_IOT_SDK
    sentino_register_rtc_handoff(agora_handoff_cb);
    sentino_register_rtc_release(agora_release_cb);
#else
    BK_LOGE(TAG, "no RTC backend compiled in — Sentino conversation will fail\n");
#endif
}

void Sentino_Stop_Session_Export(void)
{
    sentino_iot_engine_stop();
    /* Don't disconnect MQTT here — the WiFi-stop path drops the TCP and
     * lets the mqtts reader exit naturally on the next read error. The
     * old IOT_MQTT_Destroy bug (queue assert on teardown) was specific
     * to the legacy ali_mqtt path; the mqtts client doesn't have it,
     * but keeping behaviour the same on this seam is safer. */
}
