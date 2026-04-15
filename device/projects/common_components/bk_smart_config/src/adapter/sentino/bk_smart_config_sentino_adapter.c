#include <common/sys_config.h>
#include <components/log.h>
#include <string.h>
#include <os/os.h>
#include "bk_smart_config_sentino_adapter.h"
#include "bk_smart_config.h"
#include "app_event.h"
#include "sentino_mqtt.h"

#define TAG "bk_sconf_sentino"

extern void sentino_convoai_engine_stop(void);

int bk_sconf_post_nfc_id(uint8_t *nfc_id)
{
    if (nfc_id) {
        sentino_mqtt_publish_nfc_report(nfc_id, 8, 0);
    }
    return 0;
}

void bk_sconf_trans_stop(void)
{
    sentino_convoai_engine_stop();
    sentino_mqtt_disconnect();
}

void agora_ir_mode_config(bool enable)
{
    /* Image recognition mode not yet supported with Sentino */
    BK_LOGW(TAG, "ir_mode_config: %d (not supported)\n", enable);
}

#if CONFIG_ENABLE_AGORA_DATASTREAM
#include <os/os.h>
beken_queue_t datastream_queue = NULL;

int bk_sconf_init_datastream_resource(void)
{
    /* Create the queue so RTC stream messages don't assert on NULL queue.
     * Sentino uses its own message channel, so messages just accumulate and get dropped. */
    if (!datastream_queue) {
        rtos_init_queue(&datastream_queue, "datastream_queue", sizeof(char *), 4);
    }
    return 0;
}
#endif
