#ifndef __SENTINO_MQTT_DP_H__
#define __SENTINO_MQTT_DP_H__

/* Sentino DP (Data Point) framework — typed Thing-Model objects.
 *
 * Wire format reference:
 *   - cloud→device:  ref-mqtt §5.4 property_set
 *                    issue.data: { "properties": { <identifier>: <value>, ... } }
 *                    device must reply via issue_response with the same
 *                    properties echoed back (§3.4 / §5.4).
 *   - device→cloud:  ref-mqtt §4.6 property_report
 *                    report.data: { "properties": { <identifier>: <value>, ... } }
 *
 * Properties are keyed by string `identifier` (matches the model's
 * properties[].identifier from §4.5). The numeric `dpBusiId` is metadata
 * not used in the property wire format.
 *
 * Public API (typically reached through sentino_mqtt_import.h):
 *   - dp_type_e / dp_obj_t              typed value
 *   - sentino_register_dp_set_cb        register one set-handler
 *   - Sentino_Dp_Report                 single-property device→cloud
 *   - Sentino_Dp_Report_Many            batched device→cloud (one MQTT msg)
 *   - Sentino_Dp_Set_Parse              SDK-internal: parse cloud issue,
 *                                       dispatch one cb per property,
 *                                       emit issue_response (called by
 *                                       engine when code=="property_set") */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DP_TYPE_BOOL = 0,
    DP_TYPE_INT,
    DP_TYPE_FLOAT,
    DP_TYPE_ENUM,
    DP_TYPE_TEXT,
} dp_type_e;

#define DP_IDENTIFIER_MAX  32
#define DP_TEXT_MAX        64

typedef struct {
    char       identifier[DP_IDENTIFIER_MAX];
    dp_type_e  type;
    union {
        bool     b;
        int32_t  i;
        double   f;
        char     str[DP_TEXT_MAX];
    } v;
} dp_obj_t;

/* Business-side handler for cloud→device property_set events. Called
 * once per property in the issue's data.properties map. */
typedef void (*dp_set_cb_t)(const dp_obj_t *dp);

/* SDK-side registration. Idempotent; latest call wins. NULL clears.
 * Adapter (sentino_interface) typically wraps this. */
void sentino_register_dp_set_cb(dp_set_cb_t cb);

/* device → cloud, single property. Returns 0 on success. */
int  Sentino_Dp_Report(const dp_obj_t *dp);

/* device → cloud, batch — one MQTT message with all properties grouped
 * under data.properties. Saves on broker/cloud round-trips when
 * multiple values change together. Returns 0 on success. */
int  Sentino_Dp_Report_Many(const dp_obj_t *dps, size_t count);

/* SDK-internal: parse a cloud-issued property_set message and dispatch
 * each property to the registered callback. After dispatch, emits an
 * issue_response on the issue_response topic with the same properties
 * echoed back per ref-mqtt §5.4. payload_json is the FULL issue message
 * root (with code/id/ts/data fields), not just the data field. */
void Sentino_Dp_Set_Parse(const char *payload_json);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_MQTT_DP_H__ */
