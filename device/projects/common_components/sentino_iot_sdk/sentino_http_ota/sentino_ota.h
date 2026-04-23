#ifndef __SENTINO_OTA_H__
#define __SENTINO_OTA_H__

/* Sentino OTA — HTTP firmware download triggered by MQTT issue.
 *
 * Wire format reference: ref-mqtt.md §5.2 (cloud→device "ota" issue) and
 * §4.7 (device→cloud "ota_progress" event).
 *
 * Issue payload (parsed by sentino_ota_handle_issue):
 *   {
 *     "url":          "https://...",      // download URL
 *     "md5sum":       "36eb59...",        // file MD5
 *     "fileSize":     708482,              // bytes
 *     "version":      "1.2.0",             // target version
 *     "firmwareType": 2,                   // 1=factory 2=standard 3=mcu ...
 *     "silence":      false,
 *     "subUuids":     [...],               // gateway sub-devices (optional)
 *     "options": { "stable": "usr" }       // partition: usr/recovery/kernel
 *   }
 *
 * Progress event (sentino_ota_report_progress emits these to cloud):
 *   { "resCode": 0, "type": "downloading", "percent": 80 }
 *   { "resCode": 0, "type": "burning" }
 *   { "resCode": 0, "type": "report", "version": "1.2.0", ... }
 *   { "resCode": <-1..-7>, "type": "fail" }
 *
 * SKELETON: parsing + logging only. The actual HTTP download, MD5 check,
 * dual-bank flash write, and reboot are TODO. Hook this in and the
 * pipeline can flesh out one stage at a time without changing the
 * surface this module exposes. */

#ifdef __cplusplus
extern "C" {
#endif

/* ota_progress.type values per ref-mqtt.md §4.7 */
#define SENTINO_OTA_TYPE_DOWNLOADING  "downloading"
#define SENTINO_OTA_TYPE_BURNING      "burning"
#define SENTINO_OTA_TYPE_REPORT       "report"
#define SENTINO_OTA_TYPE_FAIL         "fail"

/* ota_progress.resCode values per ref-mqtt.md §4.7 */
#define SENTINO_OTA_RES_OK            0
#define SENTINO_OTA_RES_DL_TIMEOUT   (-1)
#define SENTINO_OTA_RES_NO_FILE      (-2)
#define SENTINO_OTA_RES_SIG_EXPIRED  (-3)
#define SENTINO_OTA_RES_MD5_MISMATCH (-4)
#define SENTINO_OTA_RES_UPDATE_FAIL  (-5)
#define SENTINO_OTA_RES_UPDATE_TIMEOUT (-6)
#define SENTINO_OTA_RES_BUSY         (-7)

/* Called by sentino_iot_engine when MQTT issues code=="ota". Parses the
 * payload, kicks off download. Idempotent: a second issue while a download
 * is in progress should report SENTINO_OTA_RES_BUSY (TODO). */
void sentino_ota_handle_issue(const char *payload_json);

/* Emit an ota_progress report event. percent is only used when type ==
 * SENTINO_OTA_TYPE_DOWNLOADING; pass <0 otherwise. version may be NULL
 * (only filled for type=="report"). Returns 0 on publish success. */
int sentino_ota_report_progress(int resCode,
                                const char *type,
                                int percent,
                                const char *version);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_OTA_H__ */
