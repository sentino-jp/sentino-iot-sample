#include <stdio.h>
#include <string.h>
#include <components/log.h>

#include "cJSON.h"
#include "sentino_mqtt_dp.h"
#include "sentino_mqtt.h"   /* publish_event / publish_issue_response */

#define TAG "sentino_dp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static dp_set_cb_t s_dp_set_cb = NULL;

void sentino_register_dp_set_cb(dp_set_cb_t cb)
{
    s_dp_set_cb = cb;
}

/* Add one dp_obj_t into a cJSON properties object. */
static void dp_to_json_property(cJSON *props, const dp_obj_t *dp)
{
    switch (dp->type) {
        case DP_TYPE_BOOL:
            cJSON_AddBoolToObject(props, dp->identifier, dp->v.b);
            break;
        case DP_TYPE_INT:
        case DP_TYPE_ENUM:
            cJSON_AddNumberToObject(props, dp->identifier, dp->v.i);
            break;
        case DP_TYPE_FLOAT:
            cJSON_AddNumberToObject(props, dp->identifier, dp->v.f);
            break;
        case DP_TYPE_TEXT:
            cJSON_AddStringToObject(props, dp->identifier, dp->v.str);
            break;
        default:
            LOGE("unknown dp_type=%d for identifier=%s", dp->type, dp->identifier);
            break;
    }
}

int Sentino_Dp_Report(const dp_obj_t *dp)
{
    if (!dp || !dp->identifier[0]) return -1;
    return Sentino_Dp_Report_Many(dp, 1);
}

int Sentino_Dp_Report_Many(const dp_obj_t *dps, size_t count)
{
    if (!dps || count == 0) return -1;

    cJSON *data  = cJSON_CreateObject();
    cJSON *props = cJSON_AddObjectToObject(data, "properties");
    for (size_t i = 0; i < count; i++) {
        if (dps[i].identifier[0]) dp_to_json_property(props, &dps[i]);
    }

    int ret = sentino_mqtt_publish_event("property_report", 0, data);
    cJSON_Delete(data);
    return ret;
}

/* Pull a typed dp_obj_t out of one cJSON property entry. Returns true on
 * success. We accept whatever JSON type the cloud sent — the registered
 * business handler is the one that knows the schema and can reject
 * mismatches. */
static bool json_to_dp(const char *identifier, cJSON *jval, dp_obj_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->identifier, identifier, DP_IDENTIFIER_MAX - 1);

    if (cJSON_IsBool(jval)) {
        out->type = DP_TYPE_BOOL;
        out->v.b  = cJSON_IsTrue(jval);
    } else if (cJSON_IsNumber(jval)) {
        /* cJSON doesn't distinguish int/float in the type tag — pick by
         * value. Callers wanting strict typing should consult the model. */
        if (jval->valuedouble == (double)jval->valueint) {
            out->type = DP_TYPE_INT;
            out->v.i  = jval->valueint;
        } else {
            out->type = DP_TYPE_FLOAT;
            out->v.f  = jval->valuedouble;
        }
    } else if (cJSON_IsString(jval) && jval->valuestring) {
        out->type = DP_TYPE_TEXT;
        strncpy(out->v.str, jval->valuestring, DP_TEXT_MAX - 1);
    } else {
        return false;
    }
    return true;
}

void Sentino_Dp_Set_Parse(const char *payload_json)
{
    if (!payload_json || !*payload_json) {
        LOGW("Dp_Set_Parse: empty payload");
        return;
    }

    cJSON *root = cJSON_Parse(payload_json);
    if (!root) {
        LOGE("Dp_Set_Parse: JSON parse failed");
        return;
    }

    /* Drill: root.data.properties (ref-mqtt §5.4). */
    cJSON *data  = cJSON_GetObjectItem(root, "data");
    cJSON *props = data ? cJSON_GetObjectItem(data, "properties") : NULL;
    cJSON *jid   = cJSON_GetObjectItem(root, "id");
    const char *issue_id = (cJSON_IsString(jid) && jid->valuestring) ? jid->valuestring : "";

    if (!cJSON_IsObject(props)) {
        LOGE("Dp_Set_Parse: missing data.properties");
        sentino_mqtt_publish_issue_response(issue_id, "property_set", -1,
                                            "missing data.properties", NULL);
        cJSON_Delete(root);
        return;
    }

    /* Dispatch each property to the business cb. We also build the echo
     * payload for issue_response — per §5.4 the device replies with the
     * same properties it just applied. */
    cJSON *reply_data  = cJSON_CreateObject();
    cJSON *reply_props = cJSON_AddObjectToObject(reply_data, "properties");

    cJSON *child = NULL;
    cJSON_ArrayForEach(child, props) {
        if (!child->string) continue;
        dp_obj_t dp;
        if (!json_to_dp(child->string, child, &dp)) {
            LOGW("Dp_Set_Parse: skip unsupported value for %s", child->string);
            continue;
        }
        if (s_dp_set_cb) s_dp_set_cb(&dp);
        else             LOGW("Dp_Set_Parse: no handler, dropping %s", dp.identifier);
        dp_to_json_property(reply_props, &dp);
    }

    sentino_mqtt_publish_issue_response(issue_id, "property_set", 0, "success", reply_data);
    cJSON_Delete(reply_data);
    cJSON_Delete(root);
}
