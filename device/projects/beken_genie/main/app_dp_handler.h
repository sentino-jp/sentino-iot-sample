#ifndef __APP_DP_HANDLER_H__
#define __APP_DP_HANDLER_H__

/* Business-side DP (Thing-Model) handler.
 *
 * Owns the product's DPID enum and the on-property-set switch. Registers
 * itself with sentino_interface at boot so cloud→device property_set
 * messages land here as typed dp_obj_t.
 *
 * For now this is a skeleton — the switch logs and reflects values back.
 * Wire the real volume/switch/charge actions when the cloud schema for
 * property_set is locked down. */

#ifdef __cplusplus
extern "C" {
#endif

/* DPIDs match the BUILD_GUIDE.md Thing-Model table. Don't renumber:
 * cloud config and schema lock these in. */
enum {
    DPID_SWITCH             = 1,   /* bool */
    DPID_BATTERY_PERCENT    = 2,   /* int  */
    DPID_VOLUME_SET         = 3,   /* int  0-10 */
    DPID_CHARGE_STATUS      = 4,   /* enum */
};

/* Register the handler with sentino_interface. Idempotent.
 * Call once from user_app_main() AFTER network_transfer_init() (which
 * pulls in sentino_interface_init). */
void app_dp_handler_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_DP_HANDLER_H__ */
