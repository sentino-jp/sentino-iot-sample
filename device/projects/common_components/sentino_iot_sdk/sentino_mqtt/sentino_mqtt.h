#ifndef __SENTINO_MQTT_H__
#define __SENTINO_MQTT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SENTINO_UUID_SIZE           64
#define SENTINO_KEY_SIZE            64
#define SENTINO_PID_SIZE            32
#define SENTINO_USER_ID_SIZE        64
#define SENTINO_ASSET_ID_SIZE       64
#define SENTINO_BROKER_URL_SIZE     128
#define SENTINO_APP_ID_SIZE         33
#define SENTINO_RTC_TOKEN_SIZE      512
#define SENTINO_CHANNEL_NAME_SIZE   128

/* Three-tuple credentials live in sentino_dev_info.{h,c} (NVS-backed). */

/* Provisioning info received from BLE (thing.network.set) */
typedef struct {
    char user_id[SENTINO_USER_ID_SIZE];
    char asset_id[SENTINO_ASSET_ID_SIZE];
    char mqtt_broker[SENTINO_BROKER_URL_SIZE];
    uint16_t mqtt_port;
    char pid[SENTINO_PID_SIZE];
} sentino_provision_info_t;

/* RTC parameters returned by cloud via MQTT report_response */
typedef struct {
    char app_id[SENTINO_APP_ID_SIZE];
    char rtc_token[SENTINO_RTC_TOKEN_SIZE];
    char channel_name[SENTINO_CHANNEL_NAME_SIZE];
    uint32_t uid;
} sentino_rtc_params_t;

/* Cloud command handler callback */
typedef void (*sentino_issue_handler_t)(const char *code, const char *payload_json);

/**
 * Initialize the MQTT client. Does not connect yet.
 * @param broker_url  MQTT broker hostname (e.g. "mqtt-iot.sentino.jp")
 * @param port        MQTT broker port (e.g. 1883)
 * @param uuid        Device UUID from three-tuple
 * @param key         Device KEY from three-tuple (for HMAC-SHA256 password)
 * @param pid         Product ID
 */
int sentino_mqtt_init(const char *broker_url, uint16_t port,
                      const char *uuid, const char *key, const char *pid);

/** Connect to MQTT broker. Subscribes to report_response and issue topics. */
int sentino_mqtt_connect(void);

/** Disconnect from MQTT broker. */
int sentino_mqtt_disconnect(void);

/** Check if MQTT is connected. */
bool sentino_mqtt_is_connected(void);

/**
 * Publish bind message to report topic.
 * Sent after first WiFi connection to register device with cloud.
 */
int sentino_mqtt_publish_bind(const char *user_id, const char *asset_id, const char *version);

/** Publish info message to report topic. Sent on reconnect. */
int sentino_mqtt_publish_info(const char *version, bool bind_status);

/**
 * Request RTC access parameters from cloud.
 * Publishes agora_agent_device_access to report topic, blocks waiting for response.
 * @param out  Filled with appId, rtcToken, channelName, uid on success
 * @return 0 on success, -1 on timeout/error
 */
int sentino_mqtt_request_rtc_access(sentino_rtc_params_t *out);

/** Publish NFC report. */
int sentino_mqtt_publish_nfc_report(const uint8_t *nfc_id, int nfc_id_len, int only_report);

/** Publish single-property report (convenience). For multi-property
 *  reports build a `properties` cJSON object and use publish_event. */
int sentino_mqtt_publish_property(const char *key, const char *value);

/** Generic device→cloud event publish on the report topic.
 *  `data` is the body that goes under "data" — pass a cJSON object or NULL.
 *  Caller retains ownership of `data`; it is deep-copied internally. */
struct cJSON;  /* forward-decl so callers don't need cJSON.h */
int sentino_mqtt_publish_event(const char *code, int ack, struct cJSON *data);

/** Reply to a cloud-issued command on the issue_response topic.
 *  `id` MUST echo the original issue id so cloud can correlate (ref-mqtt §3.4).
 *  res=0 means success; non-zero means failure. msg may be NULL (then
 *  "success"/"fail" is filled in). data may be NULL. Caller retains
 *  ownership of `data`; it is deep-copied internally. */
int sentino_mqtt_publish_issue_response(const char *id, const char *code,
                                        int res, const char *msg,
                                        struct cJSON *data);

/** Register handler for cloud-issued commands (reset, ota, ping, property_set). */
void sentino_mqtt_register_issue_handler(sentino_issue_handler_t handler);

/** Register a transport-layer hook fired on every successful (re)connect.
 *  Invoked from the mqtts reader task with io_mutex held — caller MUST NOT
 *  publish synchronously (would deadlock); dispatch to a worker. Idempotent;
 *  latest call wins. NULL clears. */
void sentino_mqtt_register_connected_cb(void (*cb)(void));

/* Provisioning info persistence (NVS) */
void sentino_provision_info_write(const sentino_provision_info_t *info);
void sentino_provision_info_read(sentino_provision_info_t *info);
void sentino_provision_info_clear(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_MQTT_H__ */
