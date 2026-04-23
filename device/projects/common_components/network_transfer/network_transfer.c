#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include "network_transfer.h"
#include "cli.h"

#define TAG "ntws"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#if CONFIG_AGORA_IOT_SDK
extern int agora_rtc_cli_init(void);
#elif CONFIG_VOLC_RTC_EN
extern int byte_rtc_cli_init(void);
#endif

#if CONFIG_SENTINO_IOT
#include "sentino_iot/sentino_iot_engine.h"
/* Forward-decl to avoid pulling sentino_interface into network_transfer's
 * REQUIRES (would create cycle: sentino_interface already requires
 * network_transfer for SDK headers). */
extern void sentino_interface_init(void);
#endif

int network_transfer_init(void)
{
    #if CONFIG_AGORA_IOT_SDK
    agora_rtc_cli_init();
    #elif CONFIG_VOLC_RTC_EN
    byte_rtc_cli_init();
    #endif

    #if CONFIG_SENTINO_IOT
    sentino_iot_init();
    sentino_interface_init();   /* register RTC handoff callbacks */
    #endif

    return BK_OK;
}
