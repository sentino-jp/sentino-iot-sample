#ifndef __SENTINO_BLE_IMPORT_H__
#define __SENTINO_BLE_IMPORT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* sentino_interface — boarding/business ↔ Sentino BLE bridge.
 *
 * ONLY header BSP + business code should include for the V1 BLE protocol.
 * Direct includes of sentino_ble_proto.h or sentino_ble_v1.h from outside
 * the SDK defeat the layering.
 *
 * Wire-up: BSP creates GATT service, then calls sentino_ble_init() passing:
 *   - indicate_fn: how to send bytes back on the BLE notify char
 *   - wifi_connect_fn: how to start a BK WiFi STA connection
 *   - wifi_scan_fn: how to start a BK WiFi scan
 * Adapter packages these into the proto ops struct + registers cloud-side
 * callbacks for async status code emission. */

typedef void (*sentino_ble_indicate_fn_t)(const uint8_t *data, uint16_t len);
typedef void (*sentino_ble_wifi_connect_fn_t)(const char *sid, const char *pw);
typedef void (*sentino_ble_wifi_scan_fn_t)(void);

void sentino_ble_init(sentino_ble_indicate_fn_t     indicate_fn,
                      sentino_ble_wifi_connect_fn_t wifi_connect_fn,
                      sentino_ble_wifi_scan_fn_t    wifi_scan_fn);
void sentino_ble_deinit(void);

/* BSP forwards GATT events. */
void sentino_ble_on_ble_connect(void);
void sentino_ble_on_ble_disconnect(void);
void sentino_ble_on_ble_write(const uint8_t *bytes, uint16_t len);

/* BSP calls this when WiFi scan completes. Adapter formats JSON and emits
 * via proto. NULL-terminated SSID array is NOT required — pass count. */
void sentino_ble_on_wifi_scan_done(const char * const *ssids, int count);

/* All three async status codes (1006 WiFi / 1703 MQTT / 1801 bind ack) are
 * wired internally to app_event + sentino cbs at init time — no business or
 * BSP call needed. */

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_BLE_IMPORT_H__ */
