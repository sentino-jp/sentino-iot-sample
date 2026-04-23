#include <common/sys_config.h>
#include <components/log.h>
#include <string.h>
#include <os/os.h>

#include "bk_smart_config_sentino_adapter.h"
#include "bk_smart_config.h"
#include "app_event.h"

/* Route everything through the sentino_interface adapter — never include
 * sentino_iot_sdk headers directly from outside the SDK. */
#include "sentino_mqtt_import.h"   /* Sentino_Nfc_Report_Export */
#include "sentino_rtc_export.h"    /* Sentino_Stop_Session_Export */

#define TAG "bk_sconf_sentino"

int bk_sconf_post_nfc_id(uint8_t *nfc_id)
{
    if (!nfc_id) return -1;
    /* nfc_id is always 8 bytes per the NFC stack callback contract. */
    return Sentino_Nfc_Report_Export(nfc_id, 8, 0);
}

void bk_sconf_trans_stop(void)
{
    Sentino_Stop_Session_Export();
}

void agora_ir_mode_config(bool enable)
{
    /* Image recognition mode not yet supported with Sentino. */
    BK_LOGW(TAG, "ir_mode_config: %d (not supported)\n", enable);
}

#if CONFIG_ENABLE_AGORA_DATASTREAM
beken_queue_t datastream_queue = NULL;

int bk_sconf_init_datastream_resource(void)
{
    /* Create the queue so RTC stream messages don't assert on NULL queue.
     * Sentino uses its own message channel, so messages just accumulate
     * and get dropped. */
    if (!datastream_queue) {
        rtos_init_queue(&datastream_queue, "datastream_queue", sizeof(char *), 4);
    }
    return 0;
}
#endif
