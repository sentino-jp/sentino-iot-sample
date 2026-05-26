#include <components/log.h>
#include <modules/wifi.h>

#include "sentino_wifi_import.h"

#define TAG "sentino_wifi"

/* Match aitoypro's RSSI clamping window. RSSI above MAX (=-50) is treated
 * as MAX so 100% caps cleanly; below MIN (=-90) is treated as MIN so
 * quality doesn't go negative. */
#define SENTINO_RSSI_MAX (-50)
#define SENTINO_RSSI_MIN (-90)

int sentino_bsp_query_wifi_signal_quality(uint8_t *level, uint8_t *quality)
{
    if (!level || !quality) return -1;

    wifi_link_status_t link_status = {0};
    if (bk_wifi_sta_get_link_status(&link_status) != BK_OK) {
        return -200;
    }

    int rssi = link_status.rssi;
    if (rssi > SENTINO_RSSI_MAX) rssi = SENTINO_RSSI_MAX;
    if (rssi < SENTINO_RSSI_MIN) rssi = SENTINO_RSSI_MIN;

    if      (rssi > -70) *level = 1;  /* good */
    else if (rssi > -80) *level = 2;  /* mid  */
    else                 *level = 3;  /* poor */

    *quality = (uint8_t)(100 * (rssi - SENTINO_RSSI_MIN) /
                               (SENTINO_RSSI_MAX - SENTINO_RSSI_MIN));

    BK_LOGI(TAG, "rssi=%d level=%u quality=%u\n",
            link_status.rssi, *level, *quality);
    return 0;
}
