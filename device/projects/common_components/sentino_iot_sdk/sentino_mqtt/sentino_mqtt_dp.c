#include <stdio.h>
#include <string.h>
#include <components/log.h>

#include "cJSON.h"
#include "sentino_mqtt_dp.h"
#include "sentino_mqtt.h"   /* sentino_mqtt_publish_property */

#define TAG "sentino_dp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static dp_set_cb_t s_dp_set_cb = NULL;

void sentino_register_dp_set_cb(dp_set_cb_t cb)
{
    s_dp_set_cb = cb;
}

int Sentino_Dp_Report(const dp_obj_t *dp)
{
    if (!dp) return -1;

    char key[16];
    char val[DP_TEXT_MAX + 1];
    snprintf(key, sizeof(key), "%u", dp->dpid);

    switch (dp->type) {
        case DP_TYPE_BOOL:
            snprintf(val, sizeof(val), "%s", dp->v.b ? "true" : "false");
            break;
        case DP_TYPE_INT:
        case DP_TYPE_ENUM:
            snprintf(val, sizeof(val), "%ld", (long)dp->v.i);
            break;
        case DP_TYPE_TEXT:
            snprintf(val, sizeof(val), "%s", dp->v.str);
            break;
        default:
            LOGE("Sentino_Dp_Report: unknown type %d for dpid %u\n", dp->type, dp->dpid);
            return -1;
    }

    return sentino_mqtt_publish_property(key, val);
}

void Sentino_Dp_Set_Parse(const char *payload_json)
{
    if (!payload_json || !*payload_json) {
        LOGW("Dp_Set_Parse: empty payload\n");
        return;
    }
    if (!s_dp_set_cb) {
        LOGW("Dp_Set_Parse: no handler registered, dropping: %s\n", payload_json);
        return;
    }

    cJSON *root = cJSON_Parse(payload_json);
    if (!root) {
        LOGE("Dp_Set_Parse: JSON parse failed\n");
        return;
    }

    /* Skeleton format: { "dpid": <int>, "value": <bool|int|str> }
     * The Sentino cloud schema for property_set will likely be a
     * name→value map; extend here when locked down. */
    cJSON *jdpid = cJSON_GetObjectItem(root, "dpid");
    cJSON *jval  = cJSON_GetObjectItem(root, "value");
    if (!cJSON_IsNumber(jdpid) || !jval) {
        LOGE("Dp_Set_Parse: missing dpid/value: %s\n", payload_json);
        cJSON_Delete(root);
        return;
    }

    dp_obj_t dp = {0};
    dp.dpid = (uint16_t)jdpid->valueint;

    if (cJSON_IsBool(jval)) {
        dp.type = DP_TYPE_BOOL;
        dp.v.b  = cJSON_IsTrue(jval);
    } else if (cJSON_IsNumber(jval)) {
        dp.type = DP_TYPE_INT;
        dp.v.i  = jval->valueint;
    } else if (cJSON_IsString(jval) && jval->valuestring) {
        dp.type = DP_TYPE_TEXT;
        strncpy(dp.v.str, jval->valuestring, DP_TEXT_MAX - 1);
    } else {
        LOGE("Dp_Set_Parse: unsupported value type for dpid %u\n", dp.dpid);
        cJSON_Delete(root);
        return;
    }

    s_dp_set_cb(&dp);
    cJSON_Delete(root);
}
