#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <string.h>
#include <stdio.h>

#include "cJSON.h"
#include "bk_ef.h"
#include "mbedtls/md.h"
#include "agora_config.h"
#include "mqtts.h"
#include "sentino_mqtt.h"

#define TAG "sentino_mqtt"

#define LOGI(format, ...) BK_LOGW(TAG, format "\n", ##__VA_ARGS__)
#define LOGE(format, ...) BK_LOGE(TAG, format "\n", ##__VA_ARGS__)
#define LOGW(format, ...) BK_LOGW(TAG, format "\n", ##__VA_ARGS__)
#define LOGD(format, ...) BK_LOGD(TAG, format "\n", ##__VA_ARGS__)

/* NVS keys for provisioning persistence */
#define NVS_KEY_PROV_INFO       "d_stn_prov"

/* Timeout for RTC access request (ms) */
#define RTC_ACCESS_TIMEOUT_MS   10000

/* Pick TLS based on port. 8883 is standard mqtts; everything else (notably
 * 1883) stays on plain TCP for backward compat with NVS-provisioned devices. */
#define IS_TLS_PORT(p)          ((p) == 8883)

/* MQTT message ID counter (for app-layer JSON id field, not MQTT packet id) */
static uint32_t s_msg_id_counter = 0;

/* Sentino state */
static struct {
    mqtts_t *mq;
    char uuid[SENTINO_UUID_SIZE];
    char key[SENTINO_KEY_SIZE];
    char pid[SENTINO_PID_SIZE];
    char broker_url[SENTINO_BROKER_URL_SIZE];
    uint16_t port;

    char client_id[128];
    char username[256];
    char password[65];

    char topic_report[192];
    char topic_report_response[192];
    char topic_issue[192];
    char topic_issue_response[192];

    /* RTC access request sync */
    beken_semaphore_t rtc_access_sem;
    sentino_rtc_params_t *rtc_access_result;

    sentino_issue_handler_t issue_handler;

    bool initialized;
} s_mqtt = {0};


/* ────────────────────────────────────────────────────────────────────
 *  HMAC-SHA256 password computation
 * ──────────────────────────────────────────────────────────────────── */

static int compute_mqtt_password(const char *uuid, const char *key,
                                 uint64_t ts, char *out_password, size_t out_len)
{
    char input[256];
    unsigned char hmac[32];

    snprintf(input, sizeof(input), "uuid=%s,ts=%llu", uuid, (unsigned long long)ts);

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int ret = mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    if (ret != 0) {
        LOGE("mbedtls_md_setup failed: %d", ret);
        mbedtls_md_free(&ctx);
        return -1;
    }

    mbedtls_md_hmac_starts(&ctx, (unsigned char *)key, strlen(key));
    mbedtls_md_hmac_update(&ctx, (unsigned char *)input, strlen(input));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    if (out_len < 65) return -1;
    for (int i = 0; i < 32; i++) {
        snprintf(out_password + i * 2, 3, "%02x", hmac[i]);
    }
    return 0;
}


/* ────────────────────────────────────────────────────────────────────
 *  Unique message ID generator (app-layer)
 * ──────────────────────────────────────────────────────────────────── */

static void generate_msg_id(char *buf, size_t buf_size)
{
    s_msg_id_counter++;
    snprintf(buf, buf_size, "%s_%u_%u", s_mqtt.uuid, (unsigned)rtos_get_time(), s_msg_id_counter);
}


/* ────────────────────────────────────────────────────────────────────
 *  Inbound payload handlers (unchanged from previous implementation)
 * ──────────────────────────────────────────────────────────────────── */

static void handle_report_response_payload(const char *payload, int payload_len)
{
    char *json_buf = psram_malloc(payload_len + 1);
    if (!json_buf) return;
    memcpy(json_buf, payload, payload_len);
    json_buf[payload_len] = '\0';

    LOGI("report_response: %s", json_buf);

    cJSON *root = cJSON_Parse(json_buf);
    psram_free(json_buf);
    if (!root) {
        LOGE("report_response: JSON parse failed");
        return;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || (code->type & 0xFF) != cJSON_String) {
        cJSON_Delete(root);
        return;
    }

    if (0 == strcmp(code->valuestring, "agora_agent_device_access")) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data && s_mqtt.rtc_access_result) {
            cJSON *app_id = cJSON_GetObjectItem(data, "appId");
            cJSON *rtc_token = cJSON_GetObjectItem(data, "rtcToken");
            cJSON *channel_name = cJSON_GetObjectItem(data, "channelName");
            cJSON *uid = cJSON_GetObjectItem(data, "uid");

            if (app_id && (app_id->type & 0xFF) == cJSON_String) {
                snprintf(s_mqtt.rtc_access_result->app_id,
                         sizeof(s_mqtt.rtc_access_result->app_id),
                         "%s", app_id->valuestring);
            }
            if (rtc_token && (rtc_token->type & 0xFF) == cJSON_String) {
                snprintf(s_mqtt.rtc_access_result->rtc_token,
                         sizeof(s_mqtt.rtc_access_result->rtc_token),
                         "%s", rtc_token->valuestring);
            }
            if (channel_name && (channel_name->type & 0xFF) == cJSON_String) {
                snprintf(s_mqtt.rtc_access_result->channel_name,
                         sizeof(s_mqtt.rtc_access_result->channel_name),
                         "%s", channel_name->valuestring);
            }
            if (uid && (uid->type & 0xFF) == cJSON_Number) {
                s_mqtt.rtc_access_result->uid = (uint32_t)uid->valueint;
            }

            if (s_mqtt.rtc_access_sem) {
                rtos_set_semaphore(&s_mqtt.rtc_access_sem);
            }
        }
    }

    if (0 == strcmp(code->valuestring, "bind")) {
        cJSON *res = cJSON_GetObjectItem(root, "res");
        if (res && (res->type & 0xFF) == cJSON_Number) {
            LOGI("bind response: res=%d", res->valueint);
        }
    }

    cJSON_Delete(root);
}

static void handle_issue_payload(const char *payload, int payload_len)
{
    char *json_buf = psram_malloc(payload_len + 1);
    if (!json_buf) return;
    memcpy(json_buf, payload, payload_len);
    json_buf[payload_len] = '\0';

    LOGI("issue: %s", json_buf);

    cJSON *root = cJSON_Parse(json_buf);
    psram_free(json_buf);
    if (!root) {
        LOGE("issue: JSON parse failed");
        return;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || (code->type & 0xFF) != cJSON_String) {
        cJSON_Delete(root);
        return;
    }

    if (s_mqtt.issue_handler) {
        char *payload_str = cJSON_PrintUnformatted(root);
        s_mqtt.issue_handler(code->valuestring, payload_str);
        if (payload_str) cJSON_free(payload_str);
    }

    cJSON_Delete(root);
}


/* ────────────────────────────────────────────────────────────────────
 *  mqtts message dispatch — route by topic prefix to the existing handlers
 * ──────────────────────────────────────────────────────────────────── */

static bool topic_equals(const char *t, size_t t_len, const char *ref)
{
    size_t rlen = strlen(ref);
    return (t_len == rlen) && (0 == memcmp(t, ref, rlen));
}

static void on_mqtts_message(void *user, const char *topic, size_t topic_len,
                             const uint8_t *payload, size_t payload_len)
{
    (void)user;
    if (topic_equals(topic, topic_len, s_mqtt.topic_report_response)) {
        handle_report_response_payload((const char *)payload, (int)payload_len);
    } else if (topic_equals(topic, topic_len, s_mqtt.topic_issue)) {
        handle_issue_payload((const char *)payload, (int)payload_len);
    } else {
        LOGW("unexpected topic (%.*s)", (int)topic_len, topic);
    }
}

static void on_mqtts_event(void *user, mqtts_event_t evt, int arg)
{
    (void)user;
    switch (evt) {
    case MQTTS_EVT_CONNECTED:
        LOGI("mqtts connected");
        break;
    case MQTTS_EVT_DISCONNECTED:
        LOGW("mqtts disconnected (reason=%d)", arg);
        break;
    case MQTTS_EVT_PUBLISH_FAILED:
        LOGW("publish failed (qos=%d, code=%d) — message dropped",
             (arg >> 16) & 0xff, arg & 0xff);
        break;
    default:
        break;
    }
}


/* ────────────────────────────────────────────────────────────────────
 *  Publish helper (build JSON → mqtts_publish)
 * ──────────────────────────────────────────────────────────────────── */

static int publish_json(const char *topic, cJSON *root)
{
    if (!s_mqtt.mq || !mqtts_is_connected(s_mqtt.mq)) {
        LOGE("MQTT not connected");
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        LOGE("cJSON_Print failed");
        return -1;
    }

    LOGI("PUB %s: %s", topic, json_str);
    int ret = mqtts_publish(s_mqtt.mq, topic, json_str, strlen(json_str), 1);
    cJSON_free(json_str);

    return ret;
}


/* ════════════════════════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════════════════════════ */

int sentino_mqtt_init(const char *broker_url, uint16_t port,
                      const char *uuid, const char *key, const char *pid)
{
    /* If a client already exists, tear it down first. */
    if (s_mqtt.mq) {
        mqtts_destroy(s_mqtt.mq);
        s_mqtt.mq = NULL;
    }

    memset(&s_mqtt, 0, sizeof(s_mqtt));
    strncpy(s_mqtt.uuid, uuid, sizeof(s_mqtt.uuid) - 1);
    strncpy(s_mqtt.key, key, sizeof(s_mqtt.key) - 1);
    strncpy(s_mqtt.pid, pid, sizeof(s_mqtt.pid) - 1);
    strncpy(s_mqtt.broker_url, broker_url, sizeof(s_mqtt.broker_url) - 1);
    s_mqtt.port = port;

    /* Build topic strings: rlink/v2/${pid}/${uuid}/{report,...} */
    snprintf(s_mqtt.topic_report, sizeof(s_mqtt.topic_report),
             "rlink/v2/%s/%s/report", pid, uuid);
    snprintf(s_mqtt.topic_report_response, sizeof(s_mqtt.topic_report_response),
             "rlink/v2/%s/%s/report_response", pid, uuid);
    snprintf(s_mqtt.topic_issue, sizeof(s_mqtt.topic_issue),
             "rlink/v2/%s/%s/issue", pid, uuid);
    snprintf(s_mqtt.topic_issue_response, sizeof(s_mqtt.topic_issue_response),
             "rlink/v2/%s/%s/issue_response", pid, uuid);

    LOGI("init: broker=%s:%u, uuid=%s, pid=%s", broker_url, port, uuid, pid);
    LOGI("topic_report=%s", s_mqtt.topic_report);

    s_mqtt.initialized = true;
    return 0;
}

int sentino_mqtt_connect(void)
{
    if (!s_mqtt.initialized) {
        LOGE("not initialized");
        return -1;
    }
    if (s_mqtt.mq && mqtts_is_connected(s_mqtt.mq)) {
        LOGI("already connected");
        return 0;
    }

    /* Auth: ts=0 matches reference firmware (rino_mqtt_app.c); backend doesn't
     * validate freshness so uptime gave no real replay protection anyway. */
    const uint64_t ts = 0;
    if (0 != compute_mqtt_password(s_mqtt.uuid, s_mqtt.key, ts,
                                   s_mqtt.password, sizeof(s_mqtt.password))) {
        LOGE("compute password failed");
        return -1;
    }
    snprintf(s_mqtt.client_id, sizeof(s_mqtt.client_id), "rlink_%s_V2", s_mqtt.uuid);
    snprintf(s_mqtt.username, sizeof(s_mqtt.username),
             "%s|signMethod=hmacSha256,ts=%llu",
             s_mqtt.uuid, (unsigned long long)ts);

    /* Build mqtts client. Pick TLS purely from port. Defaults cover keepalive,
     * timeouts, buffer sizes, auto-reconnect (1s..60s backoff), PINGRESP
     * liveness; we only override what's identity- or workload-specific. */
    mqtts_config_t cfg     = MQTTS_CFG_DEFAULTS();
    cfg.host               = s_mqtt.broker_url;
    cfg.port               = s_mqtt.port;
    cfg.client_id          = s_mqtt.client_id;
    cfg.username           = s_mqtt.username;
    cfg.password           = s_mqtt.password;
    cfg.use_tls            = IS_TLS_PORT(s_mqtt.port);
    cfg.max_subscriptions  = 4;   /* sentino uses 2; small to save RAM */

    s_mqtt.mq = mqtts_create(&cfg);
    if (!s_mqtt.mq) {
        LOGE("mqtts_create failed");
        return -1;
    }
    mqtts_set_message_cb(s_mqtt.mq, on_mqtts_message, NULL);
    mqtts_set_event_cb(s_mqtt.mq, on_mqtts_event, NULL);

    LOGI("connecting to %s:%u as %s (use_tls=%d)",
         s_mqtt.broker_url, s_mqtt.port, s_mqtt.client_id, (int)cfg.use_tls);

    if (mqtts_connect(s_mqtt.mq) != 0) {
        LOGE("mqtts_connect failed");
        mqtts_destroy(s_mqtt.mq);
        s_mqtt.mq = NULL;
        return -1;
    }

    /* Subscribe to inbound topics. */
    mqtts_subscribe(s_mqtt.mq, s_mqtt.topic_report_response, 1);
    mqtts_subscribe(s_mqtt.mq, s_mqtt.topic_issue, 1);
    return 0;
}

int sentino_mqtt_disconnect(void)
{
    if (s_mqtt.mq) {
        mqtts_destroy(s_mqtt.mq);
        s_mqtt.mq = NULL;
    }
    LOGI("MQTT disconnected");
    return 0;
}

bool sentino_mqtt_is_connected(void)
{
    return s_mqtt.mq && mqtts_is_connected(s_mqtt.mq);
}

int sentino_mqtt_publish_bind(const char *user_id, const char *asset_id, const char *version)
{
    cJSON *root = cJSON_CreateObject();
    char msg_id[96];
    generate_msg_id(msg_id, sizeof(msg_id));

    cJSON_AddStringToObject(root, "code", "bind");
    cJSON_AddStringToObject(root, "id", msg_id);
    cJSON_AddNumberToObject(root, "ack", 1);

    cJSON *data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "userId", user_id);
    cJSON_AddStringToObject(data, "assetId", asset_id);
    cJSON_AddStringToObject(data, "version", version);

    int ret = publish_json(s_mqtt.topic_report, root);
    cJSON_Delete(root);
    return ret;
}

int sentino_mqtt_publish_info(const char *version, bool bind_status)
{
    cJSON *root = cJSON_CreateObject();
    char msg_id[96];
    generate_msg_id(msg_id, sizeof(msg_id));

    cJSON_AddStringToObject(root, "code", "info");
    cJSON_AddStringToObject(root, "id", msg_id);
    cJSON_AddNumberToObject(root, "ack", 0);

    cJSON *data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "version", version);
    cJSON_AddNumberToObject(data, "bindStatus", bind_status ? 1 : 0);

    int ret = publish_json(s_mqtt.topic_report, root);
    cJSON_Delete(root);
    return ret;
}

int sentino_mqtt_request_rtc_access(sentino_rtc_params_t *out)
{
    if (!out) return -1;

    memset(out, 0, sizeof(sentino_rtc_params_t));

    if (s_mqtt.rtc_access_sem) {
        rtos_deinit_semaphore(&s_mqtt.rtc_access_sem);
        s_mqtt.rtc_access_sem = NULL;
    }
    bk_err_t err = rtos_init_semaphore(&s_mqtt.rtc_access_sem, 1);
    if (err != kNoErr) {
        LOGE("create rtc_access_sem failed");
        return -1;
    }

    s_mqtt.rtc_access_result = out;

    cJSON *root = cJSON_CreateObject();
    char msg_id[96];
    generate_msg_id(msg_id, sizeof(msg_id));

    cJSON_AddStringToObject(root, "code", "agora_agent_device_access");
    cJSON_AddStringToObject(root, "id", msg_id);
    cJSON_AddNumberToObject(root, "ack", 1);
    cJSON_AddObjectToObject(root, "data");

    int ret = publish_json(s_mqtt.topic_report, root);
    cJSON_Delete(root);

    if (ret != 0) {
        LOGE("publish agora_agent_device_access failed");
        s_mqtt.rtc_access_result = NULL;
        rtos_deinit_semaphore(&s_mqtt.rtc_access_sem);
        s_mqtt.rtc_access_sem = NULL;
        return -1;
    }

    LOGI("waiting for RTC access response...");
    err = rtos_get_semaphore(&s_mqtt.rtc_access_sem, RTC_ACCESS_TIMEOUT_MS);
    s_mqtt.rtc_access_result = NULL;
    rtos_deinit_semaphore(&s_mqtt.rtc_access_sem);
    s_mqtt.rtc_access_sem = NULL;

    if (err != kNoErr) {
        LOGE("RTC access request timeout");
        return -1;
    }

    LOGI("RTC access: appId=%s, channel=%s, uid=%u",
         out->app_id, out->channel_name, out->uid);
    return 0;
}

int sentino_mqtt_publish_nfc_report(const uint8_t *nfc_id, int nfc_id_len, int only_report)
{
    cJSON *root = cJSON_CreateObject();
    char msg_id[96];
    generate_msg_id(msg_id, sizeof(msg_id));

    char nfc_hex[64] = {0};
    for (int i = 0; i < nfc_id_len && i < 30; i++) {
        snprintf(nfc_hex + i * 2, 3, "%02x", nfc_id[i]);
    }

    cJSON_AddStringToObject(root, "code", "agora_agent_nfc_report");
    cJSON_AddStringToObject(root, "id", msg_id);
    cJSON_AddNumberToObject(root, "ack", 1);

    cJSON *data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "nfcId", nfc_hex);
    cJSON_AddNumberToObject(data, "onlyReport", only_report);

    int ret = publish_json(s_mqtt.topic_report, root);
    cJSON_Delete(root);
    return ret;
}

int sentino_mqtt_publish_property(const char *key, const char *value)
{
    cJSON *root = cJSON_CreateObject();
    char msg_id[96];
    generate_msg_id(msg_id, sizeof(msg_id));

    cJSON_AddStringToObject(root, "code", "property_report");
    cJSON_AddStringToObject(root, "id", msg_id);
    cJSON_AddNumberToObject(root, "ack", 0);

    cJSON *data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, key, value);

    int ret = publish_json(s_mqtt.topic_report, root);
    cJSON_Delete(root);
    return ret;
}

void sentino_mqtt_register_issue_handler(sentino_issue_handler_t handler)
{
    s_mqtt.issue_handler = handler;
}


/* ────────────────────────────────────────────────────────────────────
 *  Provisioning info persistence (NVS)
 * ──────────────────────────────────────────────────────────────────── */

void sentino_provision_info_write(const sentino_provision_info_t *info)
{
    bk_set_env_enhance(NVS_KEY_PROV_INFO, info, sizeof(sentino_provision_info_t));
}

void sentino_provision_info_read(sentino_provision_info_t *info)
{
    bk_get_env_enhance(NVS_KEY_PROV_INFO, info, sizeof(sentino_provision_info_t));
}

void sentino_provision_info_clear(void)
{
    sentino_provision_info_t zero = {0};
    bk_set_env_enhance(NVS_KEY_PROV_INFO, &zero, sizeof(zero));
    LOGW("provisioning info cleared");
}
