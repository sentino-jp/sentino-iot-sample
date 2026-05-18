#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <components/log.h>
#include <components/system.h>   /* bk_get_mac, MAC_TYPE_* */
#include <os/mem.h>
#include <os/os.h>

#include "cJSON.h"
#include "sentino_ble_proto.h"
#include "sentino_ble_v1.h"
#include "sentino_dev_info.h"    /* SDK-internal: get_triple + SENTINO_DEFAULT_PID */

#define TAG "ble_proto"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static sentino_ble_proto_ops_t s_ops = {0};
static v1_assembler_t          s_asm = {0};

static void emit_status(int code);
static void emit_response(const char *json_str);
static void dispatch(const char *json_str);

/* ── lifecycle ─────────────────────────────────────────────────── */

void sentino_ble_proto_init(const sentino_ble_proto_ops_t *ops)
{
    if (ops) s_ops = *ops;
    v1_assembler_reset(&s_asm);
}

void sentino_ble_proto_deinit(void)
{
    memset(&s_ops, 0, sizeof(s_ops));
    v1_assembler_reset(&s_asm);
}

void sentino_ble_proto_on_connect(void)    { v1_assembler_reset(&s_asm); }
void sentino_ble_proto_on_disconnect(void) { v1_assembler_reset(&s_asm); }

/* ── inbound: GATT write → V1 assemble → JSON dispatch ─────────── */

void sentino_ble_proto_feed(const uint8_t *bytes, uint16_t len)
{
    char *json = NULL;
    int rc = v1_assembler_feed(&s_asm, bytes, (int)len, &json);
    if (rc == 1 && json) {
        dispatch(json);
        os_free(json);
    } else if (rc < 0) {
        LOGW("V1 packet error\n");
    }
}

/* PID source: SDK-internal getter, never adapter. Loader/seed happens during
 * provision_ensure_loaded() called by the BSP ADV path before BLE answers. */
static const char *get_pid(void)
{
    const sentino_triple_t *t = sentino_dev_info_get_triple();
    return (t && t->Pid[0]) ? t->Pid : SENTINO_DEFAULT_PID;
}

static void handle_device_information_get(void)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "device.information.get.response");
    cJSON *data = cJSON_AddObjectToObject(resp, "data");
    cJSON_AddStringToObject(data, "pid",     get_pid());
    cJSON_AddStringToObject(data, "version", "1.0.3");
    cJSON_AddBoolToObject  (data, "bind",    false);

    uint8_t mac[6] = {0};
    char    buf[20];

    bk_get_mac(mac, MAC_TYPE_BASE);
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(data, "wifi_mac", buf);

    bk_get_mac(mac, MAC_TYPE_BLUETOOTH);
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(data, "ble_mac", buf);

    char *s = cJSON_PrintUnformatted(resp);
    emit_response(s);
    cJSON_free(s);
    cJSON_Delete(resp);
}

static const char *cjson_str(const cJSON *parent, const char *key)
{
    cJSON *o = cJSON_GetObjectItem(parent, key);
    return (o && (o->type & 0xFF) == cJSON_String) ? o->valuestring : NULL;
}

static void handle_thing_network_set(cJSON *root)
{
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) { LOGE("thing.network.set missing 'data'\n"); return; }

    const char *sid     = cjson_str(data, "sid");
    const char *pw      = cjson_str(data, "pw");
    const char *bid     = cjson_str(data, "bid");
    const char *user_id = cjson_str(data, "userId");
    const char *mq      = cjson_str(data, "mq");

    cJSON  *port_obj = cJSON_GetObjectItem(data, "port");
    uint16_t port    = (port_obj && (port_obj->type & 0xFF) == cJSON_Number)
                        ? (uint16_t)port_obj->valueint : 0;

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "thing.network.set.response");
    cJSON_AddNumberToObject(resp, "code", 0);
    char *s = cJSON_PrintUnformatted(resp);
    emit_response(s);
    cJSON_free(s);
    cJSON_Delete(resp);

    if (s_ops.on_network_set) {
        s_ops.on_network_set(sid, pw, user_id, bid, mq, port);
    }
}

static void handle_thing_property_get(void)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "thing.property.get.response");
    cJSON_AddNumberToObject(resp, "code", 0);
    cJSON_AddObjectToObject(resp, "data");
    char *s = cJSON_PrintUnformatted(resp);
    emit_response(s);
    cJSON_free(s);
    cJSON_Delete(resp);
}

static void dispatch(const char *json_str)
{
    LOGI("V1 RX: %s\n", json_str);

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOGE("V1 JSON parse failed\n");
        emit_status(V1_STATUS_JSON_PARSE_FAIL);
        return;
    }
    emit_status(V1_STATUS_JSON_PARSED);

    const char *type = cjson_str(root, "type");
    if (!type) { LOGE("V1 message missing 'type'\n"); goto out; }

    if      (0 == strcmp(type, "device.information.get")) handle_device_information_get();
    else if (0 == strcmp(type, "thing.network.set"))      handle_thing_network_set(root);
    else if (0 == strcmp(type, "thing.network.getwifis")) {
        if (s_ops.on_wifi_scan_request) s_ops.on_wifi_scan_request();
    }
    else if (0 == strcmp(type, "thing.property.get"))     handle_thing_property_get();
    else                                                   LOGW("V1 unknown type: %s\n", type);

out:
    cJSON_Delete(root);
}

/* ── outbound: V1 encode + ops.send_indicate ───────────────────── */

static void emit_response(const char *json_str)
{
    if (!s_ops.send_indicate) { LOGE("no send_indicate ops — drop\n"); return; }

    LOGI("V1 TX: %s\n", json_str);

    int data_len      = (int)strlen(json_str);
    int total_packets = (data_len + V1_MAX_PAYLOAD - 1) / V1_MAX_PAYLOAD;

    for (int sn = 0; sn < total_packets; sn++) {
        int offset    = sn * V1_MAX_PAYLOAD;
        int chunk_len = data_len - offset;
        if (chunk_len > V1_MAX_PAYLOAD) chunk_len = V1_MAX_PAYLOAD;
        int pkt_len = V1_HEADER_SIZE + chunk_len + 1;

        uint8_t pkt[V1_MAX_PACKET];
        pkt[0] = V1_HEAD;
        pkt[1] = V1_TYPE;
        pkt[2] = (sn >> 8) & 0xFF;
        pkt[3] = sn & 0xFF;
        pkt[4] = (total_packets >> 8) & 0xFF;
        pkt[5] = total_packets & 0xFF;
        pkt[6] = (data_len >> 8) & 0xFF;
        pkt[7] = data_len & 0xFF;
        pkt[8] = (uint8_t)chunk_len;
        memcpy(&pkt[V1_HEADER_SIZE], json_str + offset, chunk_len);

        uint32_t crc = 0;
        for (int i = 1; i < pkt_len - 1; i++) crc += pkt[i];
        pkt[pkt_len - 1] = (uint8_t)(crc & 0xFF);

        s_ops.send_indicate(pkt, (uint16_t)pkt_len);

        if (sn < total_packets - 1) rtos_delay_milliseconds(20);
    }
}

static void emit_status(int code)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"code\":%d}", code);
    emit_response(buf);
}

void sentino_ble_proto_emit_status(int code)         { emit_status(code); }
void sentino_ble_proto_emit_response(const char *s)  { if (s) emit_response(s); }

void sentino_ble_proto_emit_wifi_list(const char * const *ssids, int count)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "thing.network.getwifis.response");
    cJSON_AddNumberToObject(resp, "code", 0);
    cJSON *data  = cJSON_AddObjectToObject(resp, "data");
    cJSON *wifis = cJSON_AddArrayToObject(data, "wifis");
    for (int i = 0; i < count; i++) {
        if (!ssids[i] || !ssids[i][0]) continue;
        cJSON *w = cJSON_CreateObject();
        cJSON_AddStringToObject(w, "ssid", ssids[i]);
        cJSON_AddItemToArray(wifis, w);
    }
    char *s = cJSON_PrintUnformatted(resp);
    emit_response(s);
    cJSON_free(s);
    cJSON_Delete(resp);
}
