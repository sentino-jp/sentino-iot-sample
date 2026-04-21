#ifndef __AGORA_RTC_MAIN_H__
#define __AGORA_RTC_MAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "agora_rtc.h"

/* Session params handed from the IoT control plane (e.g. sentino_iot_engine)
 * to the Agora data plane. */
#define AGORA_CONVOAI_CHANNEL_NAME_SIZE     (64 + 1)

typedef struct {
    char app_id[33];
    char rtc_token[512];
    bool token_enable;
    uint32_t timestamp;
} agora_convoai_configs_resp_t;

/* Channel name + uid are written by the IoT layer before agora_start(). */
extern char               agora_channel_name[AGORA_CONVOAI_CHANNEL_NAME_SIZE];
extern agora_rtc_option_t agora_rtc_option;

bk_err_t agora_start(agora_convoai_configs_resp_t *configs);
bk_err_t agora_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* __AGORA_RTC_MAIN_H__ */
