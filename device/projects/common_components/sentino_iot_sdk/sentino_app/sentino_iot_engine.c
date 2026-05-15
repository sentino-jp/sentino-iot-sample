#include <stdio.h>
#include <string.h>
#include <components/log.h>

#include "sentino_iot_engine.h"
#include "sentino_mqtt.h"
#include "sentino_mqtt_dp.h"
#include "sentino_dev_info.h"
#include "sentino_ota.h"

#include "app_event.h"

/* Firmware version reported in MQTT bind/info messages. The Sentino SDK
 * owns this — it must NOT reach into agora_rtc/agora_config.h. Bump on
 * release; later phases will expose it via sentino_iot_common.h. */
#define SENTINO_FW_VERSION  "1.0.3"

#define TAG "sentino_iot"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static sentino_rtc_params_t       s_sentino_rtc_params;
static bool                       s_sentino_started = false;
static sentino_rtc_handoff_cb_t   s_rtc_handoff = NULL;
static sentino_rtc_release_cb_t   s_rtc_release = NULL;
static void                      (*s_cloud_ready_cb)(void) = NULL;
static sentino_provision_info_t   s_prov_cache = {0};   /* snapshot at engine_init for worker */

void sentino_register_rtc_handoff(sentino_rtc_handoff_cb_t cb) { s_rtc_handoff = cb; }
void sentino_register_rtc_release(sentino_rtc_release_cb_t cb) { s_rtc_release = cb; }
void sentino_engine_register_cloud_ready_cb(void (*cb)(void)) { s_cloud_ready_cb = cb; }

/* Posted from mqtts reader task — io_mutex is held there. Just enqueue
 * onto the app_event worker; bind/info/cb run there. */
static void on_mqtt_connected(void)
{
    app_event_send_msg(APP_EVT_CLOUD_CONNECTED, 0);
}

/* Runs in app_event worker context. Serializes bind → info → cloud_ready_cb
 * so business publish from the cb arrives after bind/info on the wire.
 * Don't gate cb on publish failures — next reconnect will retry the lot. */
static void on_cloud_connected_worker(app_evt_msg_t *msg, void *user_data)
{
    (void)msg; (void)user_data;
    LOGI("cloud connected — pushing bind/info\n");

    /* Sticking to current behavior: always publish both. bk7258aitoypro
     * branches on Flag_Bind to send only one — porting that flag (NVS-
     * backed bind state) is a separate task, out of scope here. */
    int rc1 = sentino_mqtt_publish_bind(s_prov_cache.user_id,
                                        s_prov_cache.asset_id,
                                        SENTINO_FW_VERSION);
    if (rc1 != 0) LOGW("publish_bind failed (rc=%d) — continuing\n", rc1);

    int rc2 = sentino_mqtt_publish_info(SENTINO_FW_VERSION, /*bind_status=*/true);
    if (rc2 != 0) LOGW("publish_info failed (rc=%d) — continuing\n", rc2);

    if (s_cloud_ready_cb) s_cloud_ready_cb();
}

static void sentino_issue_handler(const char *code, const char *payload_json)
{
    LOGI("sentino issue: code=%s\n", code);
    if (0 == strcmp(code, "reset")) {
        LOGI("cloud requested reset\n");
        // TODO: trigger device reset
    } else if (0 == strcmp(code, "ping")) {
        LOGI("cloud ping\n");
    } else if (0 == strcmp(code, "ota")) {
        sentino_ota_handle_issue(payload_json);
    } else if (0 == strcmp(code, "property_set")) {
        Sentino_Dp_Set_Parse(payload_json);
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

    sentino_provision_info_read(&s_prov_cache);

    if (s_prov_cache.mqtt_broker[0] == '\0') {
        LOGE("sentino provision info not found. need BLE provisioning first.\n");
        return;
    }

    const sentino_triple_t *t = sentino_dev_info_get_triple();
    LOGW("sentino init: broker=%s, port=%u, uuid=%s, pid=%s\n",
         s_prov_cache.mqtt_broker, s_prov_cache.mqtt_port, t->Uuid, t->Pid);

    sentino_mqtt_init(s_prov_cache.mqtt_broker, s_prov_cache.mqtt_port,
                      t->Uuid, t->Secret, t->Pid);

    /* Register handlers BEFORE connect: CONNECTED event may fire from the
     * reader task before sentino_mqtt_connect() returns. Worker handler
     * also needs to be in place before the dispatcher fires. */
    sentino_mqtt_register_issue_handler(sentino_issue_handler);
    sentino_mqtt_register_connected_cb(on_mqtt_connected);
    app_event_register_handler(APP_EVT_CLOUD_CONNECTED,
                               on_cloud_connected_worker, NULL);

    if (0 != sentino_mqtt_connect()) {
        LOGE("sentino MQTT connect failed\n");
        return;
    }

    /* No more inline publish_bind/publish_info here — both run in the
     * worker on every CONNECTED (boot + reconnect) via on_cloud_connected_worker.
     * Fixes the prior reconnect-no-rebind bug. */

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
