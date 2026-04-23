#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <modules/ota.h>          /* bk_ota_start_download, OTA_WR_TO_FLASH */

#include "cJSON.h"
#include "sentino_ota.h"
#include "sentino_mqtt.h"   /* sentino_mqtt_publish_event */

#define TAG "sentino_ota"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

/* Re-entrancy guard. bk_ota_start_download blocks for minutes; cloud may
 * resend the issue if it doesn't see ota_progress in time, but we should
 * NOT start a second download in parallel. */
static bool s_busy = false;

/* Worker context — owned by the worker thread, freed on exit. */
typedef struct {
    char *url;
    char *version;        /* may be NULL — only used to echo in success report */
    char *md5sum;         /* may be NULL — armino verifies md5 internally only
                           * when CONFIG_OTA_HASH_FUNCTION; we keep the field
                           * around so when we add post-download verification
                           * the wire shape doesn't change. */
} ota_job_t;

static void ota_job_free(ota_job_t *j)
{
    if (!j) return;
    if (j->url)     os_free(j->url);
    if (j->version) os_free(j->version);
    if (j->md5sum)  os_free(j->md5sum);
    os_free(j);
}

static char *strdup_heap(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)os_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Worker thread: publishes "downloading" → invokes armino OTA → on
 * success armino reboots and we never return; on failure publishes
 * "fail" with the appropriate resCode. */
static void ota_worker(void *arg)
{
    ota_job_t *job = (ota_job_t *)arg;

    LOGI("ota worker: downloading %s\n", job->url);
    sentino_ota_report_progress(SENTINO_OTA_RES_OK, SENTINO_OTA_TYPE_DOWNLOADING, 0, NULL);

    int rc = bk_ota_start_download(job->url, OTA_WR_TO_FLASH);

    /* If we get here, armino did NOT reboot — that means the download or
     * verify failed. armino returns BK_FAIL on any error; we don't have
     * fine-grained reason from this API, so map to generic UPDATE_FAIL.
     * Future: if we add our own MD5 check, distinguish MD5_MISMATCH. */
    LOGE("ota worker: bk_ota_start_download returned %d (no reboot → failed)\n", rc);
    sentino_ota_report_progress(SENTINO_OTA_RES_UPDATE_FAIL, SENTINO_OTA_TYPE_FAIL, -1, NULL);

    s_busy = false;
    ota_job_free(job);
    rtos_delete_thread(NULL);
}

void sentino_ota_handle_issue(const char *payload_json)
{
    if (!payload_json || !*payload_json) {
        LOGE("ota issue: empty payload\n");
        return;
    }

    if (s_busy) {
        LOGW("ota issue: already downloading, rejecting (BUSY)\n");
        sentino_ota_report_progress(SENTINO_OTA_RES_BUSY, SENTINO_OTA_TYPE_FAIL, -1, NULL);
        return;
    }

    cJSON *root = cJSON_Parse(payload_json);
    if (!root) {
        LOGE("ota issue: JSON parse failed\n");
        sentino_ota_report_progress(SENTINO_OTA_RES_NO_FILE, SENTINO_OTA_TYPE_FAIL, -1, NULL);
        return;
    }

    /* The issue handler hands us the FULL message root (code/id/ts/data).
     * The ota fields live under data per ref-mqtt §5.2. */
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsObject(data)) {
        LOGE("ota issue: missing data field\n");
        sentino_ota_report_progress(SENTINO_OTA_RES_NO_FILE, SENTINO_OTA_TYPE_FAIL, -1, NULL);
        cJSON_Delete(root);
        return;
    }

    cJSON *jurl  = cJSON_GetObjectItem(data, "url");
    cJSON *jmd5  = cJSON_GetObjectItem(data, "md5sum");
    cJSON *jsize = cJSON_GetObjectItem(data, "fileSize");
    cJSON *jver  = cJSON_GetObjectItem(data, "version");
    cJSON *jtype = cJSON_GetObjectItem(data, "firmwareType");

    if (!cJSON_IsString(jurl) || !cJSON_IsString(jmd5) ||
        !cJSON_IsNumber(jsize) || !cJSON_IsString(jver) ||
        !cJSON_IsNumber(jtype)) {
        LOGE("ota issue: missing required field(s) in data: %s\n", payload_json);
        sentino_ota_report_progress(SENTINO_OTA_RES_NO_FILE, SENTINO_OTA_TYPE_FAIL, -1, NULL);
        cJSON_Delete(root);
        return;
    }

    LOGI("ota issue accepted:\n");
    LOGI("  url=%s\n", jurl->valuestring);
    LOGI("  md5=%s\n", jmd5->valuestring);
    LOGI("  size=%d bytes\n", jsize->valueint);
    LOGI("  version=%s\n", jver->valuestring);
    LOGI("  firmwareType=%d\n", jtype->valueint);

    /* TODO: filter on firmwareType — only accept type 2 (standard) for
     * the SoC right now. type 3/4/5/6 are MCU/BLE/Zigbee/Matter sub-firmwares
     * which need a different update path (e.g. SoC-MCU protocol). */

    ota_job_t *job = (ota_job_t *)os_zalloc(sizeof(*job));
    if (!job) {
        LOGE("ota issue: oom allocating job\n");
        sentino_ota_report_progress(SENTINO_OTA_RES_UPDATE_FAIL, SENTINO_OTA_TYPE_FAIL, -1, NULL);
        cJSON_Delete(root);
        return;
    }
    job->url     = strdup_heap(jurl->valuestring);
    job->md5sum  = strdup_heap(jmd5->valuestring);
    job->version = strdup_heap(jver->valuestring);
    cJSON_Delete(root);

    if (!job->url) {
        LOGE("ota issue: oom copying url\n");
        sentino_ota_report_progress(SENTINO_OTA_RES_UPDATE_FAIL, SENTINO_OTA_TYPE_FAIL, -1, NULL);
        ota_job_free(job);
        return;
    }

    s_busy = true;
    beken_thread_t handle = NULL;
    bk_err_t err = rtos_create_thread(&handle,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      "sentino_ota",
                                      (beken_thread_function_t)ota_worker,
                                      4096,
                                      (beken_thread_arg_t)job);
    if (err != kNoErr) {
        LOGE("ota issue: thread create failed (%d)\n", (int)err);
        sentino_ota_report_progress(SENTINO_OTA_RES_UPDATE_FAIL, SENTINO_OTA_TYPE_FAIL, -1, NULL);
        s_busy = false;
        ota_job_free(job);
    }
}

int sentino_ota_report_progress(int resCode,
                                const char *type,
                                int percent,
                                const char *version)
{
    if (!type) return -1;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "resCode", resCode);
    cJSON_AddStringToObject(data, "type", type);
    if (0 == strcmp(type, SENTINO_OTA_TYPE_DOWNLOADING) && percent >= 0) {
        cJSON_AddNumberToObject(data, "percent", percent);
    }
    if (0 == strcmp(type, SENTINO_OTA_TYPE_REPORT) && version) {
        cJSON_AddStringToObject(data, "version", version);
    }

    int rc = sentino_mqtt_publish_event("ota_progress", 0, data);
    cJSON_Delete(data);
    return rc;
}
