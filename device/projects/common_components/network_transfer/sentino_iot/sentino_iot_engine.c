#include <stdio.h>
#include <string.h>
#include <components/log.h>

#include "sentino_iot_engine.h"
#include "sentino_mqtt.h"
#include "sentino_dev_info.h"

#include "app_event.h"
#include "agora_config.h"   /* AGORA_CONVOAI_APP_VERSION — bind/info version string.
                             * Header is misnamed (lives under agora_rtc/) but the
                             * value is just a firmware version. Move to
                             * sentino_iot_common.h in a later phase. */

#define TAG "sentino_iot"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static sentino_rtc_params_t       s_sentino_rtc_params;
static bool                       s_sentino_started = false;
static sentino_rtc_handoff_cb_t   s_rtc_handoff = NULL;
static sentino_rtc_release_cb_t   s_rtc_release = NULL;

void sentino_register_rtc_handoff(sentino_rtc_handoff_cb_t cb) { s_rtc_handoff = cb; }
void sentino_register_rtc_release(sentino_rtc_release_cb_t cb) { s_rtc_release = cb; }

static void sentino_issue_handler(const char *code, const char *payload_json)
{
    LOGI("sentino issue: code=%s\n", code);
    if (0 == strcmp(code, "reset")) {
        LOGI("cloud requested reset\n");
        // TODO: trigger device reset
    } else if (0 == strcmp(code, "ping")) {
        LOGI("cloud ping\n");
    } else if (0 == strcmp(code, "ota")) {
        LOGI("cloud OTA command\n");
        // TODO: parse OTA URL and trigger download
    } else if (0 == strcmp(code, "property_set")) {
        LOGI("cloud property_set: %s\n", payload_json);
    }
}

void sentino_iot_init(void)
{
    sentino_dev_info_cli_init();
}

void sentino_iot_engine_init(void)
{
    /* Step 1: load device triple (NVS-backed). In TEST builds the seed
     * triple from sentino_dev_info.h is auto-written if flash is empty.
     * Boarding may have already loaded it via
     * sentino_provision_ensure_loaded(); this call is idempotent. */
#ifdef SENTINO_TRIPLE_TEST
    sentino_triple_t test = {0};
    strncpy(test.Uuid,   SENTINO_TEST_UUID,   sizeof(test.Uuid)   - 1);
    strncpy(test.Secret, SENTINO_TEST_SECRET, sizeof(test.Secret) - 1);
    strncpy(test.Mac,    SENTINO_TEST_MAC,    sizeof(test.Mac)    - 1);
    strncpy(test.Pid,    SENTINO_TEST_PID,    sizeof(test.Pid)    - 1);
    sentino_dev_info_load(SENTINO_DEFAULT_PID, &test);
#else
    sentino_dev_info_load(SENTINO_DEFAULT_PID, NULL);
#endif

    if (sentino_dev_info_get_state() != SENTINO_DEV_AUTHORIZED) {
        LOGE("sentino UNAUTHORIZED — device needs factory burn-in or dynamic register.\n");
        // TODO: hook factory_dynamic_register here once backend supports it.
        return;
    }

    sentino_provision_info_t prov_info = {0};
    sentino_provision_info_read(&prov_info);

    if (prov_info.mqtt_broker[0] == '\0') {
        LOGE("sentino provision info not found. need BLE provisioning first.\n");
        return;
    }

    const sentino_triple_t *t = sentino_dev_info_get_triple();
    LOGW("sentino init: broker=%s, port=%u, uuid=%s, pid=%s\n",
         prov_info.mqtt_broker, prov_info.mqtt_port, t->Uuid, t->Pid);

    sentino_mqtt_init(prov_info.mqtt_broker, prov_info.mqtt_port,
                      t->Uuid, t->Secret, t->Pid);

    if (0 != sentino_mqtt_connect()) {
        LOGE("sentino MQTT connect failed\n");
        return;
    }

    sentino_mqtt_register_issue_handler(sentino_issue_handler);

    sentino_mqtt_publish_bind(prov_info.user_id, prov_info.asset_id, AGORA_CONVOAI_APP_VERSION);
    sentino_mqtt_publish_info(AGORA_CONVOAI_APP_VERSION, true);

    LOGI("sentino engine initialized\n");
}

void sentino_iot_engine_start(void)
{
    if (s_sentino_started) {
        LOGI("sentino already started\n");
        return;
    }

    if (!sentino_mqtt_is_connected()) {
        LOGE("sentino MQTT not connected, cannot start\n");
        return;
    }

    if (!s_rtc_handoff) {
        LOGE("no RTC backend registered — call sentino_interface_init() at boot\n");
        app_event_send_msg(APP_EVT_AGENT_START_FAIL, 0);
        return;
    }

    memset(&s_sentino_rtc_params, 0, sizeof(s_sentino_rtc_params));
    if (0 != sentino_mqtt_request_rtc_access(&s_sentino_rtc_params)) {
        LOGE("sentino RTC access request failed\n");
        app_event_send_msg(APP_EVT_AGENT_START_FAIL, 0);
        return;
    }

    LOGI("sentino starting RTC: appid=%s, channel=%s, uid=%u\n",
         s_sentino_rtc_params.app_id, s_sentino_rtc_params.channel_name,
         s_sentino_rtc_params.uid);

    if (0 != s_rtc_handoff(&s_sentino_rtc_params)) {
        LOGE("sentino RTC handoff failed\n");
        app_event_send_msg(APP_EVT_AGENT_START_FAIL, 0);
        return;
    }

    s_sentino_started = true;
}

void sentino_iot_engine_stop(void)
{
    if (s_rtc_release) s_rtc_release();
    s_sentino_started = false;
    LOGI("sentino engine stopped\n");
}
