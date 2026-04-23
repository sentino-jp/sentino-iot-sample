#ifndef __SENTINO_MQTT_DP_H__
#define __SENTINO_MQTT_DP_H__

/* Sentino DP (Data Point) framework — typed Thing-Model objects.
 *
 * MQTT signalling carries cloud-side property mutations as JSON. Today
 * the engine just logs them (sentino_issue_handler property_set). This
 * file provides a typed shape so business code can register one
 * dispatcher and switch on dpid + type instead of grepping JSON in N
 * places. Mirrors the rino_iot_sdk Dp_Obj_t pattern.
 *
 * Public API (typically reached through sentino_mqtt_import.h):
 *   - dp_type_e / dp_obj_t              typed value
 *   - sentino_register_dp_set_cb        register one set-handler
 *   - Sentino_Dp_Report                 SDK→cloud (publish report)
 *   - Sentino_Dp_Set_Parse              SDK-internal: cloud→SDK parse +
 *                                       dispatch (called by engine when
 *                                       MQTT issues code=="property_set")
 *
 * NOTE (skeleton): Sentino's exact property_set wire format is not yet
 * locked down. The current parser handles the simple shape
 *   { "dpid": <int>, "value": <bool|int|str> }
 * which is what the validation CLI emits. When the real cloud schema
 * is finalized (likely a {"<key>": <value>, ...} map keyed by name),
 * extend Sentino_Dp_Set_Parse to translate name→dpid via a registered
 * Thing-Model schema. Existing callers don't need to change. */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DP_TYPE_BOOL = 0,
    DP_TYPE_INT,
    DP_TYPE_ENUM,
    DP_TYPE_TEXT,
} dp_type_e;

#define DP_TEXT_MAX     64

typedef struct {
    uint16_t   dpid;
    dp_type_e  type;
    union {
        bool     b;
        int32_t  i;
        char     str[DP_TEXT_MAX];
    } v;
} dp_obj_t;

/* Business-side handler for cloud→device property_set events. */
typedef void (*dp_set_cb_t)(const dp_obj_t *dp);

/* SDK-side registration. Idempotent; latest call wins. NULL clears.
 * Adapter (sentino_interface) typically wraps this. */
void sentino_register_dp_set_cb(dp_set_cb_t cb);

/* Business → cloud: report a single DP value. Returns 0 on success.
 * Bridges to sentino_mqtt_publish_property() under the hood — same wire
 * format as the legacy ad-hoc API, just typed. */
int  Sentino_Dp_Report(const dp_obj_t *dp);

/* SDK-internal: parse cloud-issued property_set payload and invoke the
 * registered callback. Called by sentino_issue_handler. Safe with NULL
 * or malformed input — logs and returns. */
void Sentino_Dp_Set_Parse(const char *payload_json);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_MQTT_DP_H__ */
