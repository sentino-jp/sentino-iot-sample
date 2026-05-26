#ifndef __SENTINO_WIFI_IMPORT_H__
#define __SENTINO_WIFI_IMPORT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BK-specific WiFi telemetry adapter for the Sentino SDK.
 *
 * Lifted from bk7258aitoypro Bsp_Wifi_Load_Signal_Level_Quality. The SDK
 * has no compile-time dep on BK WiFi APIs — this file is the only place
 * <modules/wifi.h> bleeds into the Sentino layer.
 *
 * Registered into the SDK at boot via sentino_engine_register_wifi_signal_query.
 * The SDK calls it when cloud issues `ping` (ref-mqtt §5.3). */

/* Read current WiFi STA RSSI and translate to {level, quality}.
 *   level   = 1 (good, rssi > -70)
 *             2 (mid,  rssi > -80)
 *             3 (poor)
 *   quality = 0~100 percentage over [-90, -50] range
 * Returns 0 on success, non-zero when the link status query fails
 * (e.g. STA not connected). */
int sentino_bsp_query_wifi_signal_quality(uint8_t *level, uint8_t *quality);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_WIFI_IMPORT_H__ */
