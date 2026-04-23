#ifndef __APP_DP_HANDLER_H__
#define __APP_DP_HANDLER_H__

/* Business-side DP (Thing-Model) handler.
 *
 * Owns the product's identifier set and the on-property-set switch.
 * Registers itself with sentino_interface at boot so cloud→device
 * property_set messages land here as typed dp_obj_t.
 *
 * For now this is a skeleton — the switch logs and reflects values back
 * via the issue_response that Sentino_Dp_Set_Parse already emits. Wire
 * the real volume/switch/charge actuators when needed. */

#ifdef __cplusplus
extern "C" {
#endif

/* Identifier strings match BUILD_GUIDE.md Thing-Model and the cloud
 * model registered against this PID. The cloud sends these as keys in
 * data.properties (ref-mqtt §5.4), so they must match exactly. */
#define DP_ID_SWITCH                "switch"
#define DP_ID_BATTERY_PERCENTAGE    "battery_percentage"
#define DP_ID_VOLUME_SET            "volume_set"
#define DP_ID_CHARGE_STATUS         "charge_status"

/* Register the handler with sentino_interface. Idempotent.
 * Call once from user_app_main() AFTER network_transfer_init() (which
 * pulls in sentino_interface_init). */
void app_dp_handler_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_DP_HANDLER_H__ */
