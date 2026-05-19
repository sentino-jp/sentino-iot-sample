#ifndef __SENTINO_PROVISION_IMPORT_H__
#define __SENTINO_PROVISION_IMPORT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* sentino_interface — boarding/business → Sentino SDK provisioning bridge.
 *
 * This is the ONLY header business code (boarding_core.c, key_app, etc.)
 * should include for Sentino provisioning. Direct includes of
 * sentino_mqtt.h or sentino_dev_info.h from outside the SDK defeat
 * the layering.
 */

/* Persist provisioning fields received over BLE.
 * After this returns, the next sentino_iot_engine_init() (triggered on
 * WiFi-up by the app event loop) will have everything it needs to connect
 * MQTT.
 *
 * Empty/NULL strings are tolerated — they leave the corresponding NVS
 * field empty.
 *
 * port / ssl_port are the App-sent values straight from the BLE message
 * (ref-ble.md §5.2.2: `port` for plain MQTT, `mqttSslPort` for TLS).
 * Either or both MAY be 0 to mean "App didn't send that field". Engine
 * picks one based on presence — see sentino_iot_engine.c.
 *
 * No port-value magic here: passing 1883 / 8883 / anything else has the
 * same effect — the number is stored and used verbatim. */
void sentino_provision_apply_from_ble(const char *user_id,
                                      const char *asset_id,
                                      const char *broker_url,
                                      uint16_t port,
                                      uint16_t ssl_port);

/* Make sure the device triple is loaded into RAM. Idempotent.
 * Boarding calls this before answering DBEVT_AGORA_DEVICE_ID_REQUEST so
 * the BLE reply has a UUID even if the engine hasn't run yet.
 *
 * In SENTINO_TRIPLE_TEST builds, seeds NVS with the test triple if the
 * triple was empty. In production builds, leaves the device in
 * UNAUTHORIZED state until factory burn-in / dynamic register. */
void sentino_provision_ensure_loaded(void);

/* Returns the device UUID (always non-NULL; empty string if unauthorized).
 * Implicitly calls sentino_provision_ensure_loaded(). */
const char *sentino_provision_get_uuid(void);

/* Returns the device PID (always non-NULL; falls back to the build-time
 * SENTINO_DEFAULT_PID if the triple isn't loaded yet). Used by BLE adv /
 * device.information.get response. */
const char *sentino_provision_get_pid(void);

/* Wipe NVS-backed provisioning info (user_id / asset_id / broker / port).
 * Device triple is preserved. Mirrors the reference firmware's manual-reset
 * semantics — the device falls back to BLE provisioning on next boot. */
void sentino_provision_clear(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_PROVISION_IMPORT_H__ */
