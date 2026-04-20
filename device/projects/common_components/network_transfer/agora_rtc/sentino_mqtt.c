#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <string.h>
#include <stdio.h>

#include "cJSON.h"
#include "bk_ef.h"
#include "mbedtls/md.h"
#include "iot_export_mqtt.h"
#include "agora_config.h"
#include "sentino_mqtt.h"

#define TAG "sentino_mqtt"

#define LOGI(format, ...) BK_LOGW(TAG, format "\n", ##__VA_ARGS__)
#define LOGE(format, ...) BK_LOGE(TAG, format "\n", ##__VA_ARGS__)
#define LOGW(format, ...) BK_LOGW(TAG, format "\n", ##__VA_ARGS__)
#define LOGD(format, ...) BK_LOGD(TAG, format "\n", ##__VA_ARGS__)

/* NVS keys for provisioning persistence */
#define NVS_KEY_PROV_INFO       "d_stn_prov"

/* MQTT buffer sizes */
#define MQTT_WRITE_BUF_SIZE     2048
#define MQTT_READ_BUF_SIZE      4096

/* Timeout for RTC access request (ms) */
#define RTC_ACCESS_TIMEOUT_MS   10000

/* MQTT message ID counter */
static uint32_t s_msg_id_counter = 0;

/* MQTT client state */
static struct {
    void *handle;
    char uuid[SENTINO_UUID_SIZE];
    char key[SENTINO_KEY_SIZE];
    char pid[SENTINO_PID_SIZE];
    char broker_url[SENTINO_BROKER_URL_SIZE];
    uint16_t port;

    /* Topic strings (built from pid + uuid) */
    char topic_report[192];
    char topic_report_response[192];
    char topic_issue[192];
    char topic_issue_response[192];

    /* MQTT buffers */
    char *write_buf;
    char *read_buf;

    /* RTC access request sync */
    beken_semaphore_t rtc_access_sem;
    sentino_rtc_params_t *rtc_access_result;

    /* Issue handler */
    sentino_issue_handler_t issue_handler;

    bool connected;
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

    /* Hex-encode the 32-byte HMAC to a 64-char string */
    if (out_len < 65) return -1;
    for (int i = 0; i < 32; i++) {
        snprintf(out_password + i * 2, 3, "%02x", hmac[i]);
    }

    return 0;
}


/* ────────────────────────────────────────────────────────────────────
 *  Unique message ID generator
 * ──────────────────────────────────────────────────────────────────── */

static void generate_msg_id(char *buf, size_t buf_size)
{
    s_msg_id_counter++;
    snprintf(buf, buf_size, "%s_%u_%u", s_mqtt.uuid, (unsigned)rtos_get_time(), s_msg_id_counter);
}


/* ────────────────────────────────────────────────────────────────────
 *  MQTT callbacks
 *
 *  IOT_MQTT_Subscribe callback signature:
 *    void (*)(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
 *  When event_type == IOTX_MQTT_EVENT_PUBLISH_RECVEIVED,
 *    msg->msg is iotx_mqtt_topic_info_pt with ptopic/payload/payload_len
 * ──────────────────────────────────────────────────────────────────── */

static void handle_report_response_payload(const char *payload, int payload_len)
{
    /* Make null-terminated copy for cJSON */
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

    /* Handle agora_agent_device_access response */
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

            /* Signal the waiting thread */
            if (s_mqtt.rtc_access_sem) {
                rtos_set_semaphore(&s_mqtt.rtc_access_sem);
            }
        }
    }

    /* Handle bind response */
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

    /* Dispatch to registered handler */
    if (s_mqtt.issue_handler) {
        char *payload_str = cJSON_PrintUnformatted(root);
        s_mqtt.issue_handler(code->valuestring, payload_str);
        if (payload_str) cJSON_free(payload_str);
    }

    cJSON_Delete(root);
}

/* IOT_MQTT_Subscribe callback — matches iotx_mqtt_event_handle_func_fpt */
static void on_report_response(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
    if (msg->event_type == IOTX_MQTT_EVENT_PUBLISH_RECVEIVED) {
        iotx_mqtt_topic_info_pt topic_info = (iotx_mqtt_topic_info_pt)msg->msg;
        handle_report_response_payload(topic_info->payload, topic_info->payload_len);
    }
}

/* IOT_MQTT_Subscribe callback — matches iotx_mqtt_event_handle_func_fpt */
static void on_issue(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
    if (msg->event_type == IOTX_MQTT_EVENT_PUBLISH_RECVEIVED) {
        iotx_mqtt_topic_info_pt topic_info = (iotx_mqtt_topic_info_pt)msg->msg;
        handle_issue_payload(topic_info->payload, topic_info->payload_len);
    }
}

static void on_mqtt_event(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
    switch (msg->event_type) {
    case IOTX_MQTT_EVENT_DISCONNECT:
        LOGW("MQTT disconnected");
        s_mqtt.connected = false;
        break;
    case IOTX_MQTT_EVENT_RECONNECT:
        LOGI("MQTT reconnected");
        s_mqtt.connected = true;
        break;
    default:
        break;
    }
}


/* ────────────────────────────────────────────────────────────────────
 *  Publish helper
 * ──────────────────────────────────────────────────────────────────── */

static int publish_json(const char *topic, cJSON *root)
{
    if (!s_mqtt.handle) {
        LOGE("MQTT not connected");
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        LOGE("cJSON_Print failed");
        return -1;
    }

    iotx_mqtt_topic_info_t topic_msg;
    memset(&topic_msg, 0, sizeof(topic_msg));
    topic_msg.qos = IOTX_MQTT_QOS1;
    topic_msg.payload = json_str;
    topic_msg.payload_len = strlen(json_str);

    LOGI("PUB %s: %s", topic, json_str);
    int ret = IOT_MQTT_Publish(s_mqtt.handle, topic, &topic_msg);
    cJSON_free(json_str);

    return (ret >= 0) ? 0 : -1;
}


/* ────────────────────────────────────────────────────────────────────
 *  MQTT yield task
 * ──────────────────────────────────────────────────────────────────── */

static beken_thread_t s_yield_thread = NULL;
static bool s_yield_running = false;
static beken_semaphore_t s_yield_exit_sem = NULL;

static void mqtt_yield_task(void *arg)
{
    while (s_yield_running && s_mqtt.handle) {
        IOT_MQTT_Yield(s_mqtt.handle, 200);
        rtos_delay_milliseconds(100);
    }

    s_yield_thread = NULL;
    if (s_yield_exit_sem) {
        rtos_set_semaphore(&s_yield_exit_sem);
    }
    rtos_delete_thread(NULL);
}


/* ════════════════════════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════════════════════════ */

int sentino_mqtt_init(const char *broker_url, uint16_t port,
                      const char *uuid, const char *key, const char *pid)
{
    if (s_mqtt.initialized) {
        LOGI("already initialized, re-init");
        /* Stop yield thread if still running, but don't call IOT_MQTT_Destroy */
        s_yield_running = false;
        if (s_yield_thread && s_yield_exit_sem) {
            rtos_get_semaphore(&s_yield_exit_sem, 2000);
        }
        s_mqtt.handle = NULL;
        s_mqtt.connected = false;
    }

    memset(&s_mqtt, 0, sizeof(s_mqtt));
    strncpy(s_mqtt.uuid, uuid, sizeof(s_mqtt.uuid) - 1);
    strncpy(s_mqtt.key, key, sizeof(s_mqtt.key) - 1);
    strncpy(s_mqtt.pid, pid, sizeof(s_mqtt.pid) - 1);
    strncpy(s_mqtt.broker_url, broker_url, sizeof(s_mqtt.broker_url) - 1);
    s_mqtt.port = port;

    /* Build topic strings: rlink/v2/${pid}/${uuid}/report etc. */
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
    if (s_mqtt.handle) {
        LOGI("already connected");
        return 0;
    }

    /* Compute HMAC-SHA256 password. ts=0 literal matches reference firmware
     * (rino_mqtt_app.c) — backend doesn't validate ts as freshness; uptime
     * gave no real replay protection anyway. */
    const uint64_t ts = 0;
    char password[65] = {0};
    if (0 != compute_mqtt_password(s_mqtt.uuid, s_mqtt.key, ts, password, sizeof(password))) {
        LOGE("compute password failed");
        return -1;
    }

    /* Build client ID */
    char client_id[128] = {0};
    snprintf(client_id, sizeof(client_id), "rlink_%s_V2", s_mqtt.uuid);

    /* Build username: uuid|signMethod=hmacSha256,ts=0 */
    char username[256] = {0};
    snprintf(username, sizeof(username), "%s|signMethod=hmacSha256,ts=%llu",
             s_mqtt.uuid, (unsigned long long)ts);

    /* Allocate MQTT buffers from PSRAM */
    s_mqtt.write_buf = psram_malloc(MQTT_WRITE_BUF_SIZE);
    s_mqtt.read_buf = psram_malloc(MQTT_READ_BUF_SIZE);
    if (!s_mqtt.write_buf || !s_mqtt.read_buf) {
        LOGE("alloc MQTT buffers failed");
        goto fail;
    }

    /* Build MQTT connection parameters */
    iotx_mqtt_param_t mqtt_params;
    memset(&mqtt_params, 0, sizeof(mqtt_params));
    mqtt_params.port = s_mqtt.port;
    mqtt_params.host = s_mqtt.broker_url;
    mqtt_params.client_id = client_id;
    mqtt_params.username = username;
    mqtt_params.password = password;
    mqtt_params.pub_key = NULL; /* TCP, no TLS for now */
    mqtt_params.clean_session = 1;
    mqtt_params.request_timeout_ms = 5000;
    mqtt_params.keepalive_interval_ms = 60000;
    mqtt_params.pwrite_buf = s_mqtt.write_buf;
    mqtt_params.write_buf_size = MQTT_WRITE_BUF_SIZE;
    mqtt_params.pread_buf = s_mqtt.read_buf;
    mqtt_params.read_buf_size = MQTT_READ_BUF_SIZE;
    mqtt_params.handle_event.h_fp = on_mqtt_event;
    mqtt_params.handle_event.pcontext = NULL;

    LOGI("connecting to %s:%u as %s", s_mqtt.broker_url, s_mqtt.port, client_id);

    /* ali_mqtt requires device info to be initialized for random seed generation */
    extern int iotx_device_info_init(void);
    extern int iotx_device_info_set(const char *product_key, const char *device_name, const char *device_secret);
    iotx_device_info_init();
    iotx_device_info_set("sentino", s_mqtt.uuid, s_mqtt.key);

    s_mqtt.handle = IOT_MQTT_Construct(&mqtt_params);
    if (!s_mqtt.handle) {
        LOGE("IOT_MQTT_Construct failed");
        goto fail;
    }

    s_mqtt.connected = true;
    LOGI("MQTT connected");

    /* Subscribe to report_response and issue topics */
    int ret;
    ret = IOT_MQTT_Subscribe(s_mqtt.handle, s_mqtt.topic_report_response,
                             IOTX_MQTT_QOS1, on_report_response, NULL);
    if (ret < 0) {
        LOGE("subscribe report_response failed");
    } else {
        LOGI("subscribed: %s", s_mqtt.topic_report_response);
    }

    ret = IOT_MQTT_Subscribe(s_mqtt.handle, s_mqtt.topic_issue,
                             IOTX_MQTT_QOS1, on_issue, NULL);
    if (ret < 0) {
        LOGE("subscribe issue failed");
    } else {
        LOGI("subscribed: %s", s_mqtt.topic_issue);
    }

    /* Start yield task */
    s_yield_running = true;
    if (s_yield_exit_sem) { rtos_deinit_semaphore(&s_yield_exit_sem); s_yield_exit_sem = NULL; }
    rtos_init_semaphore(&s_yield_exit_sem, 1);
    bk_err_t err = rtos_create_thread(&s_yield_thread, 4, "mqtt_yield",
                                       (beken_thread_function_t)mqtt_yield_task,
                                       4 * 1024, NULL);
    if (err != kNoErr) {
        LOGE("create yield task failed");
    }

    return 0;

fail:
    if (s_mqtt.write_buf) { psram_free(s_mqtt.write_buf); s_mqtt.write_buf = NULL; }
    if (s_mqtt.read_buf)  { psram_free(s_mqtt.read_buf);  s_mqtt.read_buf = NULL; }
    return -1;
}

int sentino_mqtt_disconnect(void)
{
    /* Stop yield task and wait for it to exit */
    s_yield_running = false;
    if (s_yield_thread) {
        if (!s_yield_exit_sem) {
            rtos_init_semaphore(&s_yield_exit_sem, 1);
        }
        rtos_get_semaphore(&s_yield_exit_sem, 2000);
        if (s_yield_exit_sem) {
            rtos_deinit_semaphore(&s_yield_exit_sem);
            s_yield_exit_sem = NULL;
        }
    }

    /* Do NOT call IOT_MQTT_Destroy() here — its internal recv thread teardown
     * triggers xQueueGenericSend assert when entering provisioning mode.
     * Just null out the handle; the TCP connection dies when WiFi goes down.
     * sentino_mqtt_init() will re-init cleanly on reconnect. */
    s_mqtt.handle = NULL;
    s_mqtt.connected = false;

    /* Don't free buffers — they may still be referenced by the MQTT library's
     * recv thread during async teardown. They will be re-allocated on next init. */

    LOGI("MQTT disconnected");
    return 0;
}

bool sentino_mqtt_is_connected(void)
{
    return s_mqtt.connected;
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

    /* Create semaphore for sync wait */
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

    /* Publish request */
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

    /* Wait for response */
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

    /* Hex-encode NFC ID */
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
 *  Provisioning info persistence
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
