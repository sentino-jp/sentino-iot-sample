#include <stdio.h>
#include <string.h>
#include <components/log.h>

#include "cJSON.h"
#include "sentino_ota.h"
#include "sentino_mqtt.h"   /* for future progress publishing */

#define TAG "sentino_ota"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

/* SKELETON: parse + log only. See sentino_ota.h header comment for the
 * cloud wire format. The real implementation needs:
 *   1. HTTP(S) download to a scratch flash partition (chunked)
 *   2. MD5 verification against md5sum field
 *   3. Bank swap + reboot (likely via the existing armino OTA driver)
 *   4. Progress events via sentino_ota_report_progress at each stage
 *   5. Concurrency guard (return BUSY if a download is in flight) */

void sentino_ota_handle_issue(const char *payload_json)
{
    if (!payload_json || !*payload_json) {
        LOGE("ota issue: empty payload\n");
        return;
    }

    cJSON *root = cJSON_Parse(payload_json);
    if (!root) {
        LOGE("ota issue: JSON parse failed\n");
        sentino_ota_report_progress(SENTINO_OTA_RES_NO_FILE, SENTINO_OTA_TYPE_FAIL, -1, NULL);
        return;
    }

    cJSON *jurl     = cJSON_GetObjectItem(root, "url");
    cJSON *jmd5     = cJSON_GetObjectItem(root, "md5sum");
    cJSON *jsize    = cJSON_GetObjectItem(root, "fileSize");
    cJSON *jver     = cJSON_GetObjectItem(root, "version");
    cJSON *jtype    = cJSON_GetObjectItem(root, "firmwareType");

    if (!cJSON_IsString(jurl) || !cJSON_IsString(jmd5) ||
        !cJSON_IsNumber(jsize) || !cJSON_IsString(jver) ||
        !cJSON_IsNumber(jtype)) {
        LOGE("ota issue: missing required field(s) in payload: %s\n", payload_json);
        sentino_ota_report_progress(SENTINO_OTA_RES_NO_FILE, SENTINO_OTA_TYPE_FAIL, -1, NULL);
        cJSON_Delete(root);
        return;
    }

    LOGI("ota issue received:\n");
    LOGI("  url=%s\n", jurl->valuestring);
    LOGI("  md5=%s\n", jmd5->valuestring);
    LOGI("  size=%d bytes\n", jsize->valueint);
    LOGI("  version=%s\n", jver->valuestring);
    LOGI("  firmwareType=%d (1=factory 2=standard 3=mcu ...)\n", jtype->valueint);

    /* TODO: kick off download. For now, fail cleanly so cloud doesn't
     * wait forever. */
    LOGW("ota: download not implemented yet — reporting fail to cloud\n");
    sentino_ota_report_progress(SENTINO_OTA_RES_UPDATE_FAIL, SENTINO_OTA_TYPE_FAIL, -1, NULL);

    cJSON_Delete(root);
}

int sentino_ota_report_progress(int resCode,
                                const char *type,
                                int percent,
                                const char *version)
{
    /* Build the data field per ref-mqtt §4.7. The actual publish to
     * report topic with code="ota_progress" needs sentino_mqtt to expose
     * a generic publish_event(code, data_json) — it currently only has
     * publish_property/publish_bind/publish_info/etc. Add when wiring
     * real OTA. */
    LOGI("ota_progress (TODO publish): resCode=%d type=%s percent=%d ver=%s\n",
         resCode, type ? type : "null", percent, version ? version : "");
    (void)resCode; (void)type; (void)percent; (void)version;
    return 0;
}
