#include <common/sys_config.h>
#include <components/log.h>
#include <modules/wifi.h>
#include <components/netif.h>
#include <components/event.h>
#include <string.h>

#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <modules/pm.h>

#include "app_event.h"
#include "media_app.h"
#include "led_app.h"
#include "countdown_app.h"
#include "components/bluetooth/bk_dm_bluetooth.h"
#include "boarding_service.h"
#include "bk_factory_config.h"
#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
#include "aud_intf.h"
#include "aud_intf_types.h"
#endif
#if CONFIG_PROMPT_TONE_SOURCE_ARRAY
#include "prompt_tone.h"
#endif

#include "bk_smart_config.h"

#include "bat_monitor.h"
#include "bk_ota_private.h"

#if (CONFIG_A2DP_SINK_DEMO || CONFIG_HFP_HF_DEMO)
#include "app_audio_arbiter.h"
#endif
#if (CONFIG_SYS_CPU0 && CONFIG_LINGXIN_AI_EN)
#include "voice_chat_machine.h"
#endif
#if (CONFIG_SYS_CPU0 && CONFIG_BK_WSS_TRANS)
#include "bk_wss.h"
#endif

#define TAG "app_evt"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

static app_event_handler_t *s_event_handlers = NULL;
static beken_mutex_t s_event_mutex = NULL;

#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
static prompt_tone_url_info_t s_event_prompt_tone_info = {0};

#if CONFIG_PROMPT_TONE_SOURCE_VFS
/* Path strings: same identifier name across all codecs, only the file
 * extension changes. Picked at compile time via TONE_EXT to collapse the
 * previous 3× duplication (one block per CODEC_{MP3,WAV,PCM}). */
#if CONFIG_PROMPT_TONE_CODEC_MP3
#define TONE_EXT ".mp3"
#elif CONFIG_PROMPT_TONE_CODEC_WAV
#define TONE_EXT ".wav"
#elif CONFIG_PROMPT_TONE_CODEC_PCM
#define TONE_EXT ".pcm"
#endif
#define TONE_PATH(NAME) "/" NAME "_16k_mono_16bit_en" TONE_EXT

static char asr_wakeup_prompt_tone_path[]                = TONE_PATH("asr_wakeup");
static char asr_standby_prompt_tone_path[]               = TONE_PATH("asr_standby");
static char network_provision_prompt_tone_path[]         = TONE_PATH("network_provision");
static char network_provision_success_prompt_tone_path[] = TONE_PATH("network_provision_success");
static char network_provision_fail_prompt_tone_path[]    = TONE_PATH("network_provision_fail");
static char reconnect_network_prompt_tone_path[]         = TONE_PATH("reconnect_network");
static char reconnect_network_success_prompt_tone_path[] = TONE_PATH("reconnect_network_success");
static char reconnect_network_fail_prompt_tone_path[]    = TONE_PATH("reconnect_network_fail");
static char rtc_connection_lost_prompt_tone_path[]       = TONE_PATH("rtc_connection_lost");
/* agent_joined_prompt_tone_path intentionally omitted — the corresponding
 * call in handle_rtc_agent_events is disabled (see APP_EVT_AGENT_JOINED
 * case). Restore both this declaration and the s_prompt_tones[] entry if
 * the prompt is re-enabled. */
static char agent_offline_prompt_tone_path[]             = TONE_PATH("agent_offline");
static char low_voltage_prompt_tone_path[]               = TONE_PATH("low_voltage");
static char ota_update_start_prompt_tone_path[]          = TONE_PATH("ota_update_start");
static char ota_update_success_prompt_tone_path[]        = TONE_PATH("ota_update_success");
static char ota_update_fail_prompt_tone_path[]           = TONE_PATH("ota_update_fail");
static char agent_start_fail_prompt_tone_path[]          = TONE_PATH("agent_start_fail");
#endif  //CONFIG_PROMPT_TONE_SOURCE_VFS
#endif  //CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE

#if (CONFIG_DUAL_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_FONT_DISPLAY)
extern void lvgl_app_init(void);
extern void lvgl_app_deinit(void);
extern uint8_t lvgl_app_init_flag;
extern void lvgl_app_play(char *avi_name);
#endif
extern bk_err_t agora_stop(void);
static app_evt_info_t app_evt_info;

#if CONFIG_SENTINO_IOT
#include "sentino_engine_import.h"
#endif

bk_err_t app_event_send_msg(uint32_t event, uint32_t param)
{
    bk_err_t ret;
    app_evt_msg_t msg;

    msg.event = event;
    msg.param = param;

    ret = rtos_push_to_queue(&app_evt_info.queue, &msg, BEKEN_NO_WAIT);
    if (BK_OK != ret)
    {
        LOGE("%s, %d : %d fail \n", __func__, __LINE__, event);
        return BK_FAIL;
    }

    return BK_OK;
}

void app_event_asr_evt_callback(media_app_evt_type_t event, uint32_t param)
{
    LOGE("asr event callback: %x\n", event);
    //目前演示demo使用按键方式实现ASR功能，解决误唤醒过高问题
    return;

    /*Do not do anything blocking here */

    switch (event)
    {
        case MEDIA_APP_EVT_ASR_WAKEUP_IND:
            app_event_send_msg(APP_EVT_ASR_WAKEUP, 0);
            break;
        case MEDIA_APP_EVT_ASR_STANDBY_IND:
            app_event_send_msg(APP_EVT_ASR_STANDBY, 0);
            break;
    }
}

static uint8_t ota_event_callback(evt_ota event_param)
{

    switch(event_param)
    {
        case EVT_OTA_START:
            app_event_send_msg(APP_EVT_OTA_START, 0);
            break;
        case EVT_OTA_FAIL:
            app_event_send_msg(APP_EVT_OTA_FAIL, 0);
            break;
        case EVT_OTA_SUCCESS:
            app_event_send_msg(APP_EVT_OTA_SUCCESS, 0);
            break;
        default :
            break;
    }
    return 0;
}

static uint8_t battery_event_callback(evt_battery event_param)
{
    switch(event_param)
    {
        case EVT_BATTERY_CHARGING:
            app_event_send_msg(APP_EVT_CHARGING, 0);
            break;
        case EVT_BATTERY_LOW_VOLTAGE:
            app_event_send_msg(APP_EVT_LOW_VOLTAGE, 0);
            break;
        case EVT_SHUTDOWN_LOW_BATTERY:
            app_event_send_msg(APP_EVT_SHUTDOWN_LOW_BATTERY, 0);
            break;
        default :
            break;
    }
    return 0;
}

#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE

/* Per-event prompt tone mapping. Exactly one source is compiled in at a
 * time (VFS or ARRAY, selected by CONFIG_PROMPT_TONE_SOURCE_*). The
 * TONE_ENTRY(EVT, BASE) macro pastes BASE with the right suffix so the
 * table mirrors the variable names declared at the top of this file
 * (VFS: `<BASE>_path[]`) or in prompt_tone.h (ARRAY: `<BASE>_array[]`).
 * Adding an event-with-tone = one line in the table; no switch case. */
#if CONFIG_PROMPT_TONE_SOURCE_VFS
typedef struct {
    app_evt_type_t  evt;
    const char     *path;
} prompt_tone_entry_t;
#define TONE_ENTRY(EVT, BASE)   { (EVT), BASE ## _path }
#elif CONFIG_PROMPT_TONE_SOURCE_ARRAY
typedef struct {
    app_evt_type_t  evt;
    const uint8_t  *array;
    size_t          array_len;
} prompt_tone_entry_t;
#define TONE_ENTRY(EVT, BASE)   { (EVT), BASE ## _array, sizeof(BASE ## _array) }
#endif

static const prompt_tone_entry_t s_prompt_tones[] = {
    TONE_ENTRY(APP_EVT_ASR_WAKEUP,                   asr_wakeup_prompt_tone),
    TONE_ENTRY(APP_EVT_ASR_STANDBY,                  asr_standby_prompt_tone),
    TONE_ENTRY(APP_EVT_NETWORK_PROVISIONING,         network_provision_prompt_tone),
    TONE_ENTRY(APP_EVT_NETWORK_PROVISIONING_SUCCESS, network_provision_success_prompt_tone),
    TONE_ENTRY(APP_EVT_NETWORK_PROVISIONING_FAIL,    network_provision_fail_prompt_tone),
    TONE_ENTRY(APP_EVT_RECONNECT_NETWORK,            reconnect_network_prompt_tone),
    TONE_ENTRY(APP_EVT_RECONNECT_NETWORK_SUCCESS,    reconnect_network_success_prompt_tone),
    TONE_ENTRY(APP_EVT_RECONNECT_NETWORK_FAIL,       reconnect_network_fail_prompt_tone),
    TONE_ENTRY(APP_EVT_RTC_CONNECTION_LOST,          rtc_connection_lost_prompt_tone),
    /* APP_EVT_AGENT_JOINED omitted — see comment by
     * agent_joined_prompt_tone_path declaration. */
    TONE_ENTRY(APP_EVT_AGENT_OFFLINE,                agent_offline_prompt_tone),
    TONE_ENTRY(APP_EVT_LOW_VOLTAGE,                  low_voltage_prompt_tone),
    TONE_ENTRY(APP_EVT_OTA_START,                    ota_update_start_prompt_tone),
    TONE_ENTRY(APP_EVT_OTA_SUCCESS,                  ota_update_success_prompt_tone),
    TONE_ENTRY(APP_EVT_OTA_FAIL,                     ota_update_fail_prompt_tone),
    TONE_ENTRY(APP_EVT_AGENT_START_FAIL,             agent_start_fail_prompt_tone),
};

static bk_err_t app_play_prompt_tone(app_evt_type_t event)
{
    for (size_t i = 0; i < sizeof(s_prompt_tones) / sizeof(s_prompt_tones[0]); i++) {
        if (s_prompt_tones[i].evt != event) {
            continue;
        }
#if CONFIG_PROMPT_TONE_SOURCE_VFS
        s_event_prompt_tone_info.url       = (char *)s_prompt_tones[i].path;
#elif CONFIG_PROMPT_TONE_SOURCE_ARRAY
        s_event_prompt_tone_info.url       = (char *)s_prompt_tones[i].array;
        s_event_prompt_tone_info.total_len = s_prompt_tones[i].array_len;
#endif
        LOGI("[prompt_tone] play evt=%d", (int)event);
        bk_err_t ret = bk_aud_intf_voc_play_prompt_tone(&s_event_prompt_tone_info);
        if (ret != BK_OK) {
            LOGE("%s, %d, play prompt tone fail\n", __func__, __LINE__);
        }
        return ret;
    }

    LOGE("%s, event: %d not supported\n", __func__, (int)event);
    return BK_FAIL;
}
#endif

/* Play the prompt tone for `evt`, respecting the audio arbiter when BT
 * audio is compiled in (A2DP/HFP) — skip if arbiter is busy with non-AI
 * source so a UI tone doesn't preempt music / call audio. Compile-time
 * no-op when CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE is off. Replaces the
 * 7-line #if/#if/if/#endif/call/#endif pattern that was copy-pasted in
 * 11 case bodies of app_event_thread. */
static inline void try_play_prompt_tone(app_evt_type_t evt)
{
#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
#if (CONFIG_A2DP_SINK_DEMO || CONFIG_HFP_HF_DEMO)
    int entry = app_audio_arbiter_get_current_play_entry();
    if (entry != AUDIO_SOURCE_ENTRY_AI && entry != AUDIO_SOURCE_ENTRY_END) {
        return;
    }
#endif
    app_play_prompt_tone(evt);
#else
    (void)evt;
#endif
}

#if CONFIG_OTA_DISPLAY_PICTURE_DEMO
extern bk_err_t audio_turn_on(void);
bk_err_t bk_ota_reponse_state_to_audio(int ota_state)
{
	int ret = BK_FAIL;
	LOGI("%s ota_state :%d\n", __func__,ota_state);
	if(audio_turn_on() != BK_OK)
	{
		LOGE("%s audion turn off :%d\n", __func__);
		return ret;
	}

	switch (ota_state)
	{
        case APP_EVT_OTA_START:
            #if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
            ret = app_play_prompt_tone(APP_EVT_OTA_UPDATE_START);
            #endif
        break;
		case APP_EVT_OTA_SUCCESS : //success
			#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
            ret = app_play_prompt_tone(APP_EVT_OTA_SUCCESS);
			#endif
		break;
		case APP_EVT_OTA_FAIL : //fail
			#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
            ret = app_play_prompt_tone(APP_EVT_OTA_FAIL);
			#endif
		break;
		default:
			break;
	}
	LOGI("%s complete %x\n", __func__, ret);
	return ret;
}
#endif

extern bool image_recognition_mode_enable;

extern void agora_ir_mode_config(bool enable);
extern void prepare_config_network_main(void);

/* ────────────────────────────────────────────────────────────────────
 *  Shared device-status state.
 *
 *  These were thread-local in app_event_thread; promoted to file-static
 *  so the per-domain handlers below can mutate them without long parameter
 *  lists. The worker thread is the sole writer/reader so no locking is
 *  needed. Initial values match the original local-var inits. */

static uint32_t s_is_standby              = 1;
static uint32_t s_is_joined_agent         = 0;
static uint32_t s_is_network_provisioning = 0;
static uint32_t s_warning_state           = 0;
static uint32_t s_indicates_state         = (1 << INDICATES_POWER_ON);
static uint32_t s_active_tickets          = (1 << COUNTDOWN_TICKET_STANDBY);

/* ────────────────────────────────────────────────────────────────────
 *  Domain handlers
 *
 *  app_event_thread used to be a 450-line mega-switch handling 30+ events
 *  inline. Now the switch routes by domain to one of these 7 handlers.
 *  Each handler:
 *    - owns 2–8 related events
 *    - mutates the shared bitmasks above
 *    - calls into the right subsystem(s)
 *    - returns true to ask the worker to skip the countdown update this
 *      iteration (battery / volume cases)
 *  Adding a new event = add a case in the appropriate handler + add an
 *  entry in the worker's routing switch. No 450-line edit needed. */

static bool handle_lifecycle_events(const app_evt_msg_t *msg)
{
    switch (msg->event) {
        case APP_EVT_SMART_CONFIG_START:
            LOGI("APP_EVT_SMART_CONFIG_START\n");
            //prepare_config_network_main();
            break;
        case APP_EVT_CONVOAI_OTA_CHECK:
            LOGI("APP_EVT_CONVOAI_OTA_CHECK\n");
            break;
        case APP_EVT_CONVOAI_CONFIG_LOADING:
            LOGI("APP_EVT_CONVOAI_CONFIG_LOADING\n");
#if CONFIG_SENTINO_IOT
            sentino_engine_init();
#endif
            break;
        case APP_EVT_CONVOAI_START_TIMER_EXPIRE:
            LOGI("APP_EVT_CONVOAI_START_TIMER_EXPIRE\n");
            break;
        case APP_EVT_CONVOAI_EXIT:
            LOGI("APP_EVT_CONVOAI_EXIT\n");
#if CONFIG_SENTINO_IOT
            sentino_engine_stop();
#endif
            break;
        case APP_EVT_CONVOAI_PLAY_AVI:
            LOGI("APP_EVT_CONVOAI_PLAY_AVI. ir_mode=%d\n", image_recognition_mode_enable);
            if (!image_recognition_mode_enable) {
#if (CONFIG_DUAL_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_FONT_DISPLAY)
                /* msg->param is (uintptr_t)(const char *) — producer
                 * (business action handler) owns the business-to-path
                 * translation. Worker stays vocabulary-agnostic. */
                lvgl_app_play((char *)(uintptr_t)msg->param);
#endif
            }
            break;
        case APP_EVT_CONVOAI_RESTORE_IDLE_AVI:
            LOGI("APP_EVT_CONVOAI_RESTORE_IDLE_AVI. ir_mode=%d\n", image_recognition_mode_enable);
            if (!image_recognition_mode_enable) {
#if (CONFIG_DUAL_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_FONT_DISPLAY)
                lvgl_app_play("/genie_eye.avi");
#endif
            }
            break;
#if CONFIG_BK_SMART_CONFIG
        case APP_EVT_IR_MODE_SWITCH:
            LOGI("APP_EVT_IR_MODE_SWITCH\n");
            if (s_is_standby == 0) {
                //bk_sconf_begin_to_switch_ir_mode();
                agora_ir_mode_config(msg->param);
            }
            break;
#endif
        default:
            break;
    }
    return false;
}

static bool handle_asr_events(const app_evt_msg_t *msg)
{
    switch (msg->event) {
        case APP_EVT_ASR_WAKEUP:    /* hi armino */
            s_is_standby = 0;
            s_indicates_state &= ~(1 << INDICATES_STANDBY);
            s_active_tickets  &= ~(1 << COUNTDOWN_TICKET_STANDBY);
            LOGI("APP_EVT_ASR_WAKEUP\n");
            try_play_prompt_tone(APP_EVT_ASR_WAKEUP);
            bk_pm_module_vote_cpu_freq(PM_DEV_ID_AUDIO, PM_CPU_FRQ_480M);
            bk_wifi_sta_pm_disable();
            bk_wifi_set_wifi_media_mode(true);
#if CONFIG_SENTINO_IOT
            sentino_engine_start();
#endif
#if (CONFIG_DUAL_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_FONT_DISPLAY)
            lvgl_app_init();
#endif
            if (!s_is_network_provisioning) {
                led_app_set(LED_OFF_GREEN, 0);
            }
#if (CONFIG_SYS_CPU0 && CONFIG_LINGXIN_AI_EN)
            LOGI("%s line:%d State_Event_Wakeup_Detected\r\n", __func__, __LINE__);
            state_machine_run_event(State_Event_Wakeup_Detected);
#endif
#if (CONFIG_SYS_CPU0 && CONFIG_BK_WSS_TRANS)
            rtc_websocket_rx_data_clean();
#endif
            break;
        case APP_EVT_ASR_STANDBY:   /* byebye armino */
            s_is_standby = 1;
            s_indicates_state |= (1 << INDICATES_STANDBY);
            s_active_tickets  |= (1 << COUNTDOWN_TICKET_STANDBY);
            LOGI("APP_EVT_ASR_STANDBY\n");
#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE && !(CONFIG_A2DP_SINK_DEMO || CONFIG_HFP_HF_DEMO)
            app_play_prompt_tone(APP_EVT_ASR_STANDBY);
#endif
#if (CONFIG_DUAL_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_AVI_PLAY || CONFIG_SINGLE_SCREEN_FONT_DISPLAY)
            lvgl_app_deinit();
#endif
#if CONFIG_SENTINO_IOT
            sentino_engine_stop();
#endif
            bk_wifi_set_wifi_media_mode(false);
            bk_wifi_sta_pm_enable();
            bk_pm_module_vote_cpu_freq(PM_DEV_ID_AUDIO, PM_CPU_FRQ_240M);
            break;
        default:
            break;
    }
    return false;
}

/* Network = provisioning + reconnect. Abnormal events:
 *   PROVISIONING_FAIL / RECONNECT_NETWORK_FAIL / RTC_CONNECTION_LOST /
 *   AGENT_OFFLINE
 * Restore event: AGENT_JOINED — when it comes, all abnormal events
 * are cleared (see handle_rtc_agent_events). */
static bool handle_network_events(const app_evt_msg_t *msg)
{
    switch (msg->event) {
        case APP_EVT_NETWORK_PROVISIONING:
            LOGI("APP_EVT_NETWORK_PROVISIONING\n");
            s_is_network_provisioning = 1;
            /* 优先级最高 */
            s_active_tickets   &= ~(1 << COUNTDOWN_TICKET_NETWORK_ERROR);
            s_active_tickets   |=  (1 << COUNTDOWN_TICKET_PROVISIONING);
            s_indicates_state  |=  (1 << INDICATES_PROVISIONING);
            s_indicates_state  &= ~((1 << INDICATES_AGENT_CONNECT) | (1 << INDICATES_POWER_ON) | (1 << INDICATES_WIFI_RECONNECT));
            s_warning_state    &= ~(HIGH_PRIORITY_WARNING_MASK);
            try_play_prompt_tone(APP_EVT_NETWORK_PROVISIONING);
#if (CONFIG_DUAL_SCREEN_AVI_PLAY)
            media_app_lvgl_switch_ui(LVGL_UI_DISP_IN_TEXT);
#endif
            break;
        case APP_EVT_NETWORK_PROVISIONING_SUCCESS:
            LOGI("APP_EVT_NETWORK_PROVISIONING_SUCCESS\n");
            s_indicates_state &= ~(1 << INDICATES_PROVISIONING);
            s_indicates_state |=  (1 << INDICATES_AGENT_CONNECT);
            s_warning_state   &= ~(1 << WARNING_PROVIOSION_FAIL);
            s_active_tickets  &= ~(1 << COUNTDOWN_TICKET_PROVISIONING);
            try_play_prompt_tone(APP_EVT_NETWORK_PROVISIONING_SUCCESS);
            break;
        case APP_EVT_NETWORK_PROVISIONING_FAIL:
            LOGI("APP_EVT_NETWORK_PROVISIONING_FAIL\n");
            s_active_tickets  &= ~(1 << COUNTDOWN_TICKET_PROVISIONING);
            s_active_tickets  |=  (1 << COUNTDOWN_TICKET_NETWORK_ERROR);
            s_indicates_state &= ~(1 << INDICATES_PROVISIONING);
            s_warning_state   |=  (1 << WARNING_PROVIOSION_FAIL);
            try_play_prompt_tone(APP_EVT_NETWORK_PROVISIONING_FAIL);
            break;
        case APP_EVT_RECONNECT_NETWORK:
            LOGI("APP_EVT_RECONNECT_NETWORK\n");
            s_warning_state   &= ~(1 << WARNING_WIFI_FAIL);
            s_indicates_state |=  (1 << INDICATES_WIFI_RECONNECT);
            s_indicates_state &= ~(1 << INDICATES_POWER_ON);
            try_play_prompt_tone(APP_EVT_RECONNECT_NETWORK);
            break;
        case APP_EVT_RECONNECT_NETWORK_SUCCESS:
            LOGI("APP_EVT_RECONNECT_NETWORK_SUCCESS\n");
            s_active_tickets &= ~(1 << COUNTDOWN_TICKET_PROVISIONING);
            if (s_warning_state & WIFI_FAIL) {
                if (s_is_joined_agent && s_is_standby) {
                    s_indicates_state |= (1 << INDICATES_STANDBY);
                }
            } else {
                s_indicates_state |= (1 << INDICATES_AGENT_CONNECT);
            }
            s_warning_state   &= ~(1 << WARNING_WIFI_FAIL);
            s_indicates_state &= ~(1 << INDICATES_WIFI_RECONNECT);
            try_play_prompt_tone(APP_EVT_RECONNECT_NETWORK_SUCCESS);
            break;
        case APP_EVT_RECONNECT_NETWORK_FAIL:
            LOGI("APP_EVT_RECONNECT_NETWORK_FAIL\n");
            s_active_tickets  |=  (1 << COUNTDOWN_TICKET_NETWORK_ERROR);
            s_warning_state   |=  (1 << WARNING_WIFI_FAIL);
            s_indicates_state &= ~(1 << INDICATES_WIFI_RECONNECT);
            try_play_prompt_tone(APP_EVT_RECONNECT_NETWORK_FAIL);
            break;
        default:
            break;
    }
    return false;
}

static bool handle_rtc_agent_events(const app_evt_msg_t *msg)
{
    switch (msg->event) {
        case APP_EVT_RTC_CONNECTION_LOST:
            LOGI("APP_EVT_RTC_CONNECTION_LOST\n");
            s_warning_state |= (1 << WARNING_RTC_CONNECT_LOST);
            try_play_prompt_tone(APP_EVT_RTC_CONNECTION_LOST);
            break;
        case APP_EVT_RTC_REJOIN_SUCCESS:
            LOGI("APP_EVT_RTC_REJOIN_SUCCESS\n");
            s_warning_state &= ~(1 << WARNING_RTC_CONNECT_LOST);
            if (s_is_joined_agent && s_is_standby) {
                s_indicates_state |= (1 << INDICATES_STANDBY);
            }
            break;
        case APP_EVT_AGENT_JOINED:
            LOGI("APP_EVT_AGENT_JOINED\n");
            s_is_joined_agent = 1;
            s_active_tickets   &= ~(1 << COUNTDOWN_TICKET_NETWORK_ERROR);
            s_indicates_state  &= ~(1 << INDICATES_AGENT_CONNECT);
            s_warning_state    &= ~((1 << WARNING_RTC_CONNECT_LOST) |
                                    (1 << WARNING_AGENT_OFFLINE) |
                                    (1 << WARNING_WIFI_FAIL) |
                                    (1 << WARNING_AGENT_AGENT_START_FAIL));
            s_is_network_provisioning = 0;
            s_indicates_state  &= ~(1 << INDICATES_PROVISIONING);
            if (s_is_standby) {
                s_indicates_state |= (1 << INDICATES_STANDBY);
            }
            /* prompt_tone intentionally disabled here — see omission notes
             * in the s_prompt_tones[] table and path declarations above. */
            break;
        case APP_EVT_AGENT_OFFLINE:
            LOGI("APP_EVT_AGENT_OFFLINE\n");
            s_is_joined_agent = 0;
            s_active_tickets |= (1 << COUNTDOWN_TICKET_NETWORK_ERROR);
            s_warning_state  |= (1 << WARNING_AGENT_OFFLINE);
            try_play_prompt_tone(APP_EVT_AGENT_OFFLINE);
            break;
        case APP_EVT_AGENT_START_FAIL:
            LOGI("APP_EVT_AGENT_START_FAIL\n");
            s_active_tickets |= (1 << COUNTDOWN_TICKET_NETWORK_ERROR);
            s_warning_state  |= (1 << WARNING_AGENT_AGENT_START_FAIL);
            try_play_prompt_tone(APP_EVT_AGENT_START_FAIL);
            break;
        case APP_EVT_AGENT_DEVICE_REMOVE:
            LOGI("APP_EVT_AGENT_DEVICE_REMOVE\n");
#if CONFIG_AGORA_IOT_SDK
            agora_stop();
#endif
            break;
        default:
            break;
    }
    return false;
}

static bool handle_battery_events(const app_evt_msg_t *msg)
{
    switch (msg->event) {
        case APP_EVT_LOW_VOLTAGE:
            LOGI("APP_EVT_LOW_VOLTAGE\n");
            s_warning_state |= (1 << WARNING_LOW_BATTERY);
            try_play_prompt_tone(APP_EVT_LOW_VOLTAGE);
            return true;
        case APP_EVT_CHARGING:
            LOGI("APP_EVT_CHARGING\n");
            s_warning_state &= ~(1 << WARNING_LOW_BATTERY);
            return true;
        case APP_EVT_SHUTDOWN_LOW_BATTERY:
            LOGI("APP_EVT_SHUTDOWN_LOW_BATTERY\n");
            bk_config_sync_flash();
            return true;
        default:
            break;
    }
    return false;
}

static bool handle_ota_events(const app_evt_msg_t *msg)
{
    switch (msg->event) {
        case APP_EVT_OTA_START:
            LOGI("APP_EVT_OTA_START\n");
            s_active_tickets |= (1 << COUNTDOWN_TICKET_OTA);
#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
            app_play_prompt_tone(APP_EVT_OTA_START);
#endif
            break;
        case APP_EVT_OTA_SUCCESS:
            LOGI("APP_EVT_OTA_SUCCESS\n");
            s_active_tickets &= ~(1 << COUNTDOWN_TICKET_OTA);
#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
            app_play_prompt_tone(APP_EVT_OTA_SUCCESS);
#endif
#if CONFIG_OTA_DISPLAY_PICTURE_DEMO
            bk_ota_reponse_state_to_audio(msg->event);
#endif
            break;
        case APP_EVT_OTA_FAIL:
            LOGI("APP_EVT_OTA_FAIL\n");
            s_active_tickets &= ~(1 << COUNTDOWN_TICKET_OTA);
#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
            app_play_prompt_tone(APP_EVT_OTA_FAIL);
#endif
#if CONFIG_OTA_DISPLAY_PICTURE_DEMO
            bk_ota_reponse_state_to_audio(msg->event);
#endif
            break;
        default:
            break;
    }
    return false;
}

static bool handle_misc_events(const app_evt_msg_t *msg)
{
    switch (msg->event) {
        case APP_EVT_CLOSE_BLUETOOTH:
            rtos_delay_milliseconds(100);
#if CONFIG_A2DP_SINK_DEMO || CONFIG_HFP_HF_DEMO
            LOGI("APP_EVT_CLOSE_BLUETOOTH no need to close !!!\n");
#else
            LOGI("APP_EVT_CLOSE_BLUETOOTH\n");
#if CONFIG_BK_BOARDING_SERVICE
            bk_genie_boarding_deinit();
#endif
#if CONFIG_BLUETOOTH
            bk_bluetooth_deinit();
#endif
#endif
            return false;
        case APP_EVT_SYNC_FLASH:
            LOGI("APP_EVT_SYNC_FLASH\n");
            bk_sconf_sync_flash_safely();
            return false;
#if CONFIG_SENTINO_IOT
        case APP_EVT_VOLUME_CHANGED:
            LOGI("APP_EVT_VOLUME_CHANGED local=%u\n", msg->param);
            extern void app_dp_request_volume_report(unsigned char);
            app_dp_request_volume_report((unsigned char)msg->param);
            return true;
#endif
        default:
            break;
    }
    return false;
}

static void app_event_thread(beken_thread_arg_t data)
{
    ota_event_callback_register(ota_event_callback);
#if CONFIG_COUNTDOWN
    update_countdown(s_active_tickets);
#endif
#if CONFIG_BAT_MONITOR
    battery_event_callback_register(battery_event_callback);
#endif
    media_app_asr_evt_register_callback(app_event_asr_evt_callback);

    while (1) {
        app_evt_msg_t msg;
        int ret = rtos_pop_from_queue(&app_evt_info.queue, &msg, BEKEN_WAIT_FOREVER);
        if (ret != BK_OK) {
            continue;
        }

        /* Route by domain; each handler owns 2–8 related events and
         * returns true if the countdown update should be skipped this
         * iteration (battery / volume cases). */
        bool skip_countdown_update = false;
        switch (msg.event) {
            case APP_EVT_ASR_WAKEUP:
            case APP_EVT_ASR_STANDBY:
                skip_countdown_update = handle_asr_events(&msg);
                break;

            case APP_EVT_NETWORK_PROVISIONING:
            case APP_EVT_NETWORK_PROVISIONING_SUCCESS:
            case APP_EVT_NETWORK_PROVISIONING_FAIL:
            case APP_EVT_RECONNECT_NETWORK:
            case APP_EVT_RECONNECT_NETWORK_SUCCESS:
            case APP_EVT_RECONNECT_NETWORK_FAIL:
                skip_countdown_update = handle_network_events(&msg);
                break;

            case APP_EVT_RTC_CONNECTION_LOST:
            case APP_EVT_RTC_REJOIN_SUCCESS:
            case APP_EVT_AGENT_JOINED:
            case APP_EVT_AGENT_OFFLINE:
            case APP_EVT_AGENT_START_FAIL:
            case APP_EVT_AGENT_DEVICE_REMOVE:
                skip_countdown_update = handle_rtc_agent_events(&msg);
                break;

            case APP_EVT_LOW_VOLTAGE:
            case APP_EVT_CHARGING:
            case APP_EVT_SHUTDOWN_LOW_BATTERY:
                skip_countdown_update = handle_battery_events(&msg);
                break;

            case APP_EVT_OTA_START:
            case APP_EVT_OTA_SUCCESS:
            case APP_EVT_OTA_FAIL:
                skip_countdown_update = handle_ota_events(&msg);
                break;

            case APP_EVT_SMART_CONFIG_START:
            case APP_EVT_CONVOAI_OTA_CHECK:
            case APP_EVT_CONVOAI_CONFIG_LOADING:
            case APP_EVT_CONVOAI_START_TIMER_EXPIRE:
            case APP_EVT_CONVOAI_EXIT:
            case APP_EVT_CONVOAI_PLAY_AVI:
            case APP_EVT_CONVOAI_RESTORE_IDLE_AVI:
#if CONFIG_BK_SMART_CONFIG
            case APP_EVT_IR_MODE_SWITCH:
#endif
                skip_countdown_update = handle_lifecycle_events(&msg);
                break;

            case APP_EVT_CLOSE_BLUETOOTH:
            case APP_EVT_SYNC_FLASH:
#if CONFIG_SENTINO_IOT
            case APP_EVT_VOLUME_CHANGED:
#endif
                skip_countdown_update = handle_misc_events(&msg);
                break;

            default:
                break;
        }

        if (!skip_countdown_update) {
#if CONFIG_COUNTDOWN
            update_countdown(s_active_tickets);
#endif
        }

#if CONFIG_LED_BLINK
        led_blink(&s_warning_state, s_indicates_state);
#endif

        /* Listener chain: subsystems that prefer to opt-in per event
         * (vs editing the routing switch above) register a callback via
         * app_event_register_handler. Today only sentino_engine uses it
         * for APP_EVT_CLOUD_CONNECTED. */
        rtos_lock_mutex(&s_event_mutex);
        app_event_handler_t *handler = s_event_handlers;
        while (handler) {
            if (handler->event_type == msg.event) {
                handler->callback(&msg, handler->user_data);
            }
            handler = handler->next;
        }
        rtos_unlock_mutex(&s_event_mutex);
    }

    LOGI("%s, exit\r\n", __func__);
    rtos_delete_thread(NULL);
}

int app_event_register_handler(app_evt_type_t event_type,
                              app_event_callback_t callback,
                              void *user_data)
{
    app_event_handler_t *handler = os_malloc(sizeof(app_event_handler_t));
    if (!handler) return BK_ERR_NO_MEM;

    handler->event_type = event_type;
    handler->callback = callback;
    handler->user_data = user_data;
    handler->next = NULL;

    rtos_lock_mutex(&s_event_mutex);
    handler->next = s_event_handlers;
    s_event_handlers = handler;
    rtos_unlock_mutex(&s_event_mutex);

    return BK_OK;
}

void app_event_init(void)
{
    int ret = BK_FAIL;

    os_memset(&app_evt_info, 0, sizeof(app_evt_info_t));

    ret = rtos_init_queue(&app_evt_info.queue,
                          "ae_queue",
                          sizeof(app_evt_msg_t),
                          15);

    if (ret != BK_OK)
    {
        LOGE("%s, init queue failed\r\n", __func__);
        return;
    }

    ret = rtos_create_thread(&app_evt_info.thread,
                             BEKEN_DEFAULT_WORKER_PRIORITY - 1,
                             "ae_thread",
                             (beken_thread_function_t)app_event_thread,
                             1024 * 5,
                             NULL);

    if (ret != BK_OK)
    {
        LOGE("%s, init thread failed\r\n", __func__);
        return;
    }

    ret = rtos_init_mutex(&s_event_mutex);

    if (ret != BK_OK)
    {
        LOGE("%s, init s_event_mutex failed\r\n", __func__);
        return;
    }

}
