#include <stdio.h>
#include <string.h>
#include <os/os.h>           /* rtos_delay_milliseconds */
#include <components/log.h>
#include <components/system.h>  /* bk_reboot */

#include "sentino_iot_engine.h"
#include "sentino_mqtt.h"
#include "sentino_mqtt_dp.h"
#include "sentino_dev_info.h"
#include "sentino_ota.h"
#include "cJSON.h"

#include "app_event.h"

#define TAG "sentino_iot"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static sentino_rtc_params_t       s_sentino_rtc_params;
static bool                       s_sentino_started = false;
static sentino_rtc_handoff_cb_t   s_rtc_handoff = NULL;
static sentino_rtc_release_cb_t   s_rtc_release = NULL;
static void                      (*s_cloud_ready_cb)(void) = NULL;
static sentino_wifi_signal_query_fn_t s_wifi_signal_query = NULL;
static sentino_clean_data_handler_fn_t s_clean_data_handler = NULL;
static sentino_provision_info_t   s_prov_cache = {0};   /* snapshot at engine_init for worker */

void sentino_register_rtc_handoff(sentino_rtc_handoff_cb_t cb) { s_rtc_handoff = cb; }
void sentino_register_rtc_release(sentino_rtc_release_cb_t cb) { s_rtc_release = cb; }
void sentino_engine_register_cloud_ready_cb(void (*cb)(void)) { s_cloud_ready_cb = cb; }
void sentino_engine_register_wifi_signal_query(sentino_wifi_signal_query_fn_t fn) { s_wifi_signal_query = fn; }
void sentino_engine_register_clean_data_handler(sentino_clean_data_handler_fn_t fn) { s_clean_data_handler = fn; }

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

    /* ref-mqtt §4.4 + aitoypro convention: request cloud time right after
     * info so any subsequent property_report/event timestamps are aligned
     * with cloud. Cloud's `time` reply will hit handle_report_response_payload
     * and fire the registered time_response cb (if any). Failure is
     * non-fatal — local UTC stays whatever was set last. */
    int rc3 = sentino_mqtt_publish_time_request();
    if (rc3 != 0) LOGW("publish_time_request failed (rc=%d) — continuing\n", rc3);

    if (s_cloud_ready_cb) s_cloud_ready_cb();
}

/* ref-mqtt §5.3: reply on issue_response with the same id. Ported from
 * bk7258aitoypro Rino_Mqtt_Cmd_Ping_Parse — minimum-viable subset that
 * keeps the cloud keep-alive happy. Extra fields (currentSsid, wifiList,
 * ipaddr, networkType) are doc-spec'd but the legacy reference omits
 * them, so we match that. */
static void handle_ping_issue(const char *payload_json)
{
    cJSON *root = payload_json ? cJSON_Parse(payload_json) : NULL;
    cJSON *jid  = root ? cJSON_GetObjectItem(root, "id") : NULL;
    const char *issue_id = (jid && cJSON_IsString(jid) && jid->valuestring)
                           ? jid->valuestring : "";

    cJSON *data = cJSON_CreateObject();
    if (s_wifi_signal_query) {
        uint8_t level = 0, quality = 0;
        int rc = s_wifi_signal_query(&level, &quality);
        if (rc == 0) {
            cJSON_AddNumberToObject(data, "signal",      level);
            cJSON_AddNumberToObject(data, "signalValue", quality);
        } else {
            LOGW("ping: wifi signal query rc=%d\n", rc);
        }
    } else {
        LOGW("ping: no wifi_signal_query registered — replying empty data\n");
    }

    sentino_mqtt_publish_issue_response(issue_id, "ping", 0, "success", data);

    cJSON_Delete(data);
    if (root) cJSON_Delete(root);
}

/* ref-mqtt §5.1: cloud-issued reset → wipe BLE-provisioned user/asset/broker
 * NV (NOT the factory triple) and reboot. Triggered when user unbinds via
 * REST, or when cloud detects local/cloud bind-state divergence. Ported
 * from bk7258aitoypro Rino_Mqtt_Cmd_Reset_Parse + Import_Callback. */
static void handle_reset_issue(const char *payload_json)
{
    cJSON *root  = payload_json ? cJSON_Parse(payload_json) : NULL;
    cJSON *jid   = root ? cJSON_GetObjectItem(root, "id")   : NULL;
    cJSON *jdata = root ? cJSON_GetObjectItem(root, "data") : NULL;
    cJSON *jclear = jdata ? cJSON_GetObjectItem(jdata, "clearData") : NULL;

    const char *issue_id = (jid && cJSON_IsString(jid) && jid->valuestring)
                           ? jid->valuestring : "";
    bool clear_data = (jclear && cJSON_IsBool(jclear))   ? cJSON_IsTrue(jclear) :
                      (jclear && cJSON_IsNumber(jclear)) ? jclear->valueint != 0 : false;

    LOGW("cloud reset: clearData=%d — wiping provision + reboot\n", clear_data);

    /* ACK first so cloud sees res=0 before we go dark on reboot. */
    sentino_mqtt_publish_issue_response(issue_id, "reset", 0, "success", NULL);

    if (root) cJSON_Delete(root);

    /* Wipe BLE-provisioned info (user_id, asset_id, mqtt_broker, ports).
     * Keeps the factory triple untouched — sentino_dev_info_reset would
     * wipe THAT and brick the device's identity. clearData is currently
     * a no-op extension hook (aitoypro reference also leaves it empty);
     * the unconditional NV clear is the §5.1 semantics. */
    (void)clear_data;
    sentino_provision_info_clear();

    /* Aitoypro uses 200ms to let MQTT flush the ack frame on the wire
     * before the reboot kills the TCP. Matching that. */
    rtos_delay_milliseconds(200);
    bk_reboot();
}

/* ref-mqtt §5.5: clean_data fires from cloud right after a successful
 * bind. ack=0 — no response on the wire. Device should clear ONLY
 * pre-bind temp data (offline queues, scratch); NOT the network config
 * or user/asset association (that's §5.1 reset's job). */
static void handle_clean_data_issue(const char *payload_json)
{
    cJSON *root  = payload_json ? cJSON_Parse(payload_json) : NULL;
    cJSON *jdata = root ? cJSON_GetObjectItem(root, "data")     : NULL;
    cJSON *juuid = jdata ? cJSON_GetObjectItem(jdata, "subUuid") : NULL;

    const char *sub_uuid = (juuid && cJSON_IsString(juuid) && juuid->valuestring)
                           ? juuid->valuestring : NULL;

    if (s_clean_data_handler) {
        LOGI("cloud clean_data: subUuid=%s\n", sub_uuid ? sub_uuid : "(self)");
        s_clean_data_handler(sub_uuid);
    } else {
        LOGW("cloud clean_data ignored — no handler registered\n");
    }

    if (root) cJSON_Delete(root);
}

static void sentino_issue_handler(const char *code, const char *payload_json)
{
    LOGI("sentino issue: code=%s\n", code);
    if (0 == strcmp(code, "reset")) {
        handle_reset_issue(payload_json);
    } else if (0 == strcmp(code, "ping")) {
        handle_ping_issue(payload_json);
    } else if (0 == strcmp(code, "clean_data")) {
        handle_clean_data_issue(payload_json);
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
