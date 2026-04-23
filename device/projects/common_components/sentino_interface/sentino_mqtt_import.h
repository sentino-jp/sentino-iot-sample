#ifndef __SENTINO_MQTT_IMPORT_H__
#define __SENTINO_MQTT_IMPORT_H__

/* sentino_interface — cloud → business import callbacks for MQTT events.
 *
 * Today this is just the DP set handler. Future: bind/info ack hooks,
 * OTA progress, ping/pong, custom event codes — all centralized here so
 * business code never includes the SDK headers directly.
 *
 * Re-exports dp_obj_t / dp_type_e from the SDK so business code can
 * #include just this one header. */

#include "sentino_mqtt_dp.h"   /* dp_obj_t, dp_type_e, dp_set_cb_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Register the business-side handler invoked when the cloud issues
 * property_set. Latest call wins (NULL clears). Wraps the SDK's
 * sentino_register_dp_set_cb so business code never depends on the
 * SDK header directly — just on this adapter header. */
void Register_Sentino_Dp_Set_Cb(dp_set_cb_t cb);

/* Convenience: report a single typed DP value to cloud. Wraps
 * Sentino_Dp_Report. Same naming convention as the eventual
 * sentino_mqtt_export.h family. */
int  Sentino_Dp_Report_Export(const dp_obj_t *dp);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_MQTT_IMPORT_H__ */
