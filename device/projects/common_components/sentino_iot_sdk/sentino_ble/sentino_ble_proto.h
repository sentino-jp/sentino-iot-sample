#ifndef __SENTINO_BLE_PROTO_H__
#define __SENTINO_BLE_PROTO_H__

#include <stdint.h>
#include <stddef.h>

#include "sentino_ble_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sentino BLE protocol layer.
 *
 * Owns: V1 packet (re)assembly + JSON message dispatch + V1 packet emission
 *       + async status code emission.
 *
 * Does NOT own: GATT attribute table, BLE link state, ADV/scan_rsp. Those
 *               are BSP concerns (bk_boarding_service/wifi_boarding_utils.c).
 *
 * Wire-up: BSP creates the GATT service then calls sentino_ble_proto_init()
 *          with ops that inject (a) how to send bytes back on the notify
 *          char, (b) what to do when the phone requests WiFi connect / scan.
 *          BSP forwards every GATT write on 0x2B11 to proto_feed(), and
 *          connect/disconnect events to on_connect/on_disconnect. */

typedef struct {
    /* Send raw bytes via BLE notify char. BSP wraps bk_ble_gatts_send_indicate
     * (captures gatts_if + conn_id + notify_handle). */
    void (*send_indicate)(const uint8_t *data, uint16_t len);

    /* Phone requested WiFi connect via thing.network.set. Adapter impl
     * persists Sentino provision via sentino_provision_apply_from_ble +
     * triggers BK WiFi sta connect through BSP-injected fn. */
    void (*on_network_set)(const char *sid,       const char *pw,
                           const char *user_id,   const char *asset_id,
                           const char *mqtt_url,  uint16_t port);

    /* Phone requested WiFi scan via thing.network.getwifis. Adapter triggers
     * BK scan; results loop back via proto_emit_wifi_list when scan completes. */
    void (*on_wifi_scan_request)(void);
} sentino_ble_proto_ops_t;

/* Wire up ops + reset assembler. Idempotent: latest ops wins. */
void sentino_ble_proto_init(const sentino_ble_proto_ops_t *ops);

/* Drop the ops + reset assembler. Safe to call without prior init. */
void sentino_ble_proto_deinit(void);

/* BSP forwards every GATT write to 0x2B11 here. */
void sentino_ble_proto_feed(const uint8_t *bytes, uint16_t len);

/* BSP signals BLE link state — proto resets reassembly between sessions. */
void sentino_ble_proto_on_connect(void);
void sentino_ble_proto_on_disconnect(void);

/* Push an async status code to the phone: {"code": N}.
 *   1006 = WIFI_CONNECTED   (boarding_core when DHCP IP arrives)
 *   1703 = MQTT_CONNECTED   (Register_Sentino_Cloud_Ready_Cb)
 *   1801 = BIND_SUCCESS     (sentino_mqtt bind ack hook)
 * See sentino_ble_v1.h for the full V1_STATUS_* list. */
void sentino_ble_proto_emit_status(int code);

/* Push a pre-built JSON string to the phone as a V1 response. Caller owns
 * the string. Used by adapter for thing.network.getwifis.response when
 * scan completes. */
void sentino_ble_proto_emit_response(const char *json_str);

/* Convenience wrapper: build {type:"thing.network.getwifis.response",
 * code:0, data:{wifis:[{ssid:"..."}...]}} and emit. */
void sentino_ble_proto_emit_wifi_list(const char * const *ssids, int count);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_BLE_PROTO_H__ */
