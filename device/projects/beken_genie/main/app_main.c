#include <common/sys_config.h>
#include <components/log.h>
#include <string.h>

#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>
#include "cli.h"
#include "media_service.h"
#include <driver/pwr_clk.h>
#include <driver/pwr_clk.h>
#include <modules/pm.h>
#include <key_app_service.h>
#include "sys_driver.h"
#include "sys_hal.h"
#include <driver/gpio.h>
#include "gpio_driver.h"

#if (CONFIG_SYS_CPU0)
#include "aud_intf.h"
#include "bk_factory_config.h"
#if CONFIG_BK_SMART_CONFIG
#include "bk_genie_comm.h"
#include "bk_smart_config.h"
#endif
#include "motor.h"
#include "audio_engine.h"
#include "video_engine.h"
#include "network_transfer.h"
#elif (CONFIG_SYS_CPU1)
#include "audio_engine.h"
#include "video_engine.h"
#else
#endif

#include "media_app.h"
#include "app_event.h"
#include "countdown.h"
#if CONFIG_SENTINO_IOT && CONFIG_ENABLE_AGORA_DATASTREAM
#include "cJSON.h"
#include "sentino_command_bus.h"
#include "sentino_command_agora_source.h"
#endif
#include <led_blink.h>
#include <common/bk_include.h>
#include "components/bluetooth/bk_dm_bluetooth.h"
#if CONFIG_NET_PAN
#include "bluetooth_storage.h"
#endif
#include "app_main.h"

extern void user_app_main(void);
extern void rtos_set_user_app_entry(beken_thread_function_t entry);
extern int bk_cli_init(void);
extern void bk_set_jtag_mode(uint32_t cpu_id, uint32_t group_id);

#define TAG "GENIE"

#if CONFIG_SENTINO_IOT && CONFIG_ENABLE_AGORA_DATASTREAM
/* Map cloud-side emotion_type strings → app_event.h enums. Names align with
 * EMOTION_* in app_event.h:5-13 (and with the upstream Agora R1 reference
 * in bk_smart_config_agora_adapter.c:43-94). Acts as a whitelist: any
 * emotion_type the cloud sends that's not in this table gets silently
 * skipped before lvgl is touched, side-stepping a known SDK NULL-deref
 * when bk_avi_play_open() is called the first time on a missing file. */
static const struct { const char *name; app_emotion_t value; } s_emotion_map[] = {
    { "happy",     EMOTION_HAPPY     },
    { "sad",       EMOTION_SAD       },
    { "angry",     EMOTION_ANGRY     },
    { "surprised", EMOTION_SURPRISED },
    { "neutral",   EMOTION_NEUTRAL   },
    { "thinking",  EMOTION_THINKING  },
    { "sleepy",    EMOTION_SLEEPY    },
    { "loving",    EMOTION_LOVING    },
    { "curious",   EMOTION_CURIOUS   },
};

/* After dispatching an emotion AVI, the LCD loops it forever (bk_avi_play
 * has no play-once / play-N API). Schedule a one-shot fallback that
 * switches the LCD back to the idle "/genie_eye.avi" after a quiet window.
 * New emotion commands reset the timer. */
#define CONV_AI_IDLE_MS              8000
#define CONV_AI_IDLE_DEFAULT_AVI     "/genie_eye.avi"

static beken2_timer_t s_idle_timer;
static bool           s_idle_timer_inited = false;

static void conv_ai_idle_timer_cb(void *larg, void *rarg)
{
    (void)larg; (void)rarg;
    BK_LOGW(TAG, "emotion idle timeout, restoring %s\n", CONV_AI_IDLE_DEFAULT_AVI);
    /* Post to app_event worker — lvgl_app_play does a sync cross-core
     * mailbox call that the timer-service thread cannot legally drive
     * (observed: media_major_mailbox ack flag error 97 → idle restore
     * fails). Worker thread is allowed to block on the mailbox, and
     * routing through it also serializes with the emotion change path. */
    app_event_send_msg(APP_EVT_CONVOAI_RESTORE_IDLE_AVI, 0);
}

static void conv_ai_arm_idle_timer(void)
{
    bk_err_t ret;
    if (!s_idle_timer_inited) {
        ret = rtos_init_oneshot_timer(&s_idle_timer, CONV_AI_IDLE_MS,
                                      conv_ai_idle_timer_cb, NULL, NULL);
        if (ret != BK_OK) {
            BK_LOGE(TAG, "idle timer init failed: %d (no fallback)\n", ret);
            return;
        }
        s_idle_timer_inited = true;
    } else if (rtos_is_oneshot_timer_running(&s_idle_timer)) {
        rtos_stop_oneshot_timer(&s_idle_timer);
    }
    ret = rtos_start_oneshot_timer(&s_idle_timer);
    if (ret != BK_OK) {
        BK_LOGW(TAG, "idle timer start failed: %d\n", ret);
    }
}

/* Dispatch one cloud-issued action. Always logs at W (LOGI gets stripped in
 * release, and one line per action is low frequency enough not to flood).
 * Currently routes display_emotion → APP_EVT_CONVOAI_CHANGE_LVGL_RESOURCE
 * which the app_event worker resolves to lvgl_app_play(<emotion>.avi).
 * Unknown executor / unknown emotion warns and returns -1; queue keeps
 * draining. */
static int conv_ai_executor_dispatch(const char *executor,
                                     const cJSON *parameters,
                                     int priority)
{
    char *params_str = parameters ? cJSON_PrintUnformatted((cJSON *)parameters) : NULL;
    BK_LOGW(TAG, "action: executor=%s priority=%d params=%s\n",
            executor, priority, params_str ? params_str : "{}");
    if (params_str) cJSON_free(params_str);

    if (strcmp(executor, "display_emotion") == 0) {
        const char *et = cJSON_GetStringValue(
                             cJSON_GetObjectItemCaseSensitive(parameters, "emotion_type"));
        if (!et) {
            BK_LOGW(TAG, "display_emotion missing emotion_type\n");
            return -1;
        }
        for (size_t i = 0; i < sizeof(s_emotion_map)/sizeof(s_emotion_map[0]); i++) {
            if (strcmp(et, s_emotion_map[i].name) == 0) {
                app_event_send_msg(APP_EVT_CONVOAI_CHANGE_LVGL_RESOURCE,
                                   (uint32_t)s_emotion_map[i].value);
                conv_ai_arm_idle_timer();
                return 0;
            }
        }
        BK_LOGW(TAG, "unsupported emotion: %s\n", et);
        return -1;
    }

    BK_LOGW(TAG, "unsupported executor: %s\n", executor);
    return -1;
}
#endif

#ifdef CONFIG_LDO3V3_ENABLE
#ifndef LDO3V3_CTRL_GPIO
#ifdef CONFIG_LDO3V3_CTRL_GPIO
#define LDO3V3_CTRL_GPIO    CONFIG_LDO3V3_CTRL_GPIO
#else
#define LDO3V3_CTRL_GPIO    GPIO_52
#endif
#endif
#endif


#if (CONFIG_SYS_CPU0)
static const uint32_t s_user_value2 = 10;
#if CONFIG_NET_PAN
static const bt_user_storage_t s_bt_factory_storage ={0};
#endif
/* Note: d_stn_prov and d_stn_triple are NOT registered here.
 * factory_config_t's size fields are uint8_t (max 255) — both records exceed
 * that. Wiping d_stn_prov on FACTORY_RESET is handled explicitly by
 * sentino_provision_info_clear() in key_app_service.c, and d_stn_triple is
 * intentionally preserved across resets. Both are read/written directly via
 * bk_*_env_enhance, no registration required. */
const struct factory_config_t s_user_config[] = {
    {"user_key1", (void *)"user_value1", 11, BK_FALSE, 0},
    {"user_key2", (void *)&s_user_value2, 4, BK_TRUE, 4},
#if CONFIG_NET_PAN
    {BT_STORAGE_KEY, (void *)&s_bt_factory_storage, sizeof(s_bt_factory_storage), BK_TRUE, sizeof(s_bt_factory_storage)},
#endif
};
#endif


#if (CONFIG_SYS_CPU0)

static bk_err_t app_force_analog_ldo_gpio_close(void)
{
    /*audio*/
    sys_hal_set_ana_reg18_value(0);
    sys_hal_set_ana_reg19_value(0);
    sys_hal_set_ana_reg20_value(0);
    sys_hal_set_ana_reg21_value(0);
    sys_hal_set_ana_reg27_value(0);
    sys_drv_aud_aud_en(0);
    sys_drv_aud_audbias_en(0);
    sys_drv_apll_en(0);

   	/*usb/psram ldo ctrl at deepsleep last location*/
    /*ldo*/
    gpio_dev_unmap(GPIO_50);
    gpio_dev_unmap(GPIO_52);

    /*UART*/
    gpio_dev_unmap(GPIO_10);
    gpio_dev_unmap(GPIO_11);

    /*I2C*/
    gpio_dev_unmap(GPIO_0);
    gpio_dev_unmap(GPIO_1);

    /*MOTO*/
    gpio_dev_unmap(GPIO_9);

    return 0;
}

static void bk_enter_deepsleep(void)
{
#if CONFIG_GSENSOR_ENABLE
    extern int gsensor_enter_sleep_config();
    gsensor_enter_sleep_config();
    rtos_delay_milliseconds(10);
#endif

    BK_LOGI(TAG,"RESET_SOURCE_FORCE_DEEPSLEEP\r\n");
    bk_key_register_wakeup_source();
    bk_pm_clear_deep_sleep_modules_config(PM_POWER_MODULE_NAME_AUDP);
    bk_pm_clear_deep_sleep_modules_config(PM_POWER_MODULE_NAME_VIDP);
    app_force_analog_ldo_gpio_close();
    bk_pm_sleep_mode_set(PM_MODE_DEEP_SLEEP);
    rtos_delay_milliseconds(10);
}

static void bk_wait_power_on(void)
{
    uint32_t press_time = 0;

    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    do {
        if (bk_gpio_get_input(KEY_GPIO_12) == 0) {
            extern void delay_ms(uint32 num);
            delay_ms(500);
            press_time += 500;

            if (bk_gpio_get_input(KEY_GPIO_12) != 0) {
                break;
            }
        } else {

            break;
        }
    } while (press_time < LONG_RRESS_TIMR);
    GLOBAL_INT_RESTORE();

    if (press_time < LONG_RRESS_TIMR)
    {
        // bk_key_register_wakeup_source();
        bk_enter_deepsleep();
    }
}
#endif

extern int cli_ota_init(void);
#if CONFIG_SENTINO_IOT
#include "app_dp_handler.h"
#endif
void user_app_main(void)
{
#if (CONFIG_SYS_CPU0)

    bk_pm_module_vote_cpu_freq(PM_DEV_ID_AUDIO, PM_CPU_FRQ_240M);

    audio_engine_init();
    voide_engine_init();
    network_transfer_init();
#if CONFIG_SENTINO_IOT
    /* DP set handler — must follow network_transfer_init (which calls
     * sentino_interface_init that owns the SDK callback registry). */
    app_dp_handler_init();
#endif
    cli_ota_init();
#endif

#if (CONFIG_SYS_CPU1)
    voide_engine_init();
#endif
}

int main(void)
{
    if (bk_misc_get_reset_reason() != RESET_SOURCE_FORCE_DEEPSLEEP)
    {
#if (CONFIG_SYS_CPU0)
        rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
#endif
        bk_init();

#if (CONFIG_SYS_CPU0)
#ifdef CONFIG_LDO3V3_ENABLE
        BK_LOG_ON_ERR(gpio_dev_unmap(LDO3V3_CTRL_GPIO));
        bk_gpio_disable_pull(LDO3V3_CTRL_GPIO);
        bk_gpio_enable_output(LDO3V3_CTRL_GPIO);
        bk_gpio_set_output_high(LDO3V3_CTRL_GPIO);
#endif
#endif

#if (CONFIG_SYS_CPU0)
        /*to judgement key is long press or short press; long press exit deepsleep*/
        if(bk_misc_get_reset_reason() == RESET_SOURCE_DEEPPS_GPIO && (bk_gpio_get_wakeup_gpio_id() == KEY_GPIO_12))
        {
            //motor vibration
        #if CONFIG_MOTOR
            motor_open(PWM_MOTOR_CH_3);
        #endif

            bk_wait_power_on();

        #if CONFIG_MOTOR
            motor_close(PWM_MOTOR_CH_3);
        #endif
        }

        bk_regist_factory_user_config((const struct factory_config_t *)&s_user_config,
                                       sizeof(s_user_config)/sizeof(s_user_config[0]));
        bk_factory_init();
#endif

    //led init move before
#if (CONFIG_SYS_CPU0)
        
        //No operation countdown 3 minutes to shut down
        // start_countdown(countdown_ms);
        //led init move before
      #if CONFIG_LED_BLINK
          led_driver_init();
          led_app_set(LED_ON_GREEN,LED_LAST_FOREVER);
      #endif
        
#endif

        media_service_init();

#if (CONFIG_SYS_CPU0)
#if (CONFIG_APP_EVT)
        app_event_init();
#endif
#if (CONFIG_NFC_ENABLE)
        void nfc_get_id_task(void);
        nfc_get_id_task();
#endif
        volume_init();

#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
        extern bk_err_t audio_turn_on(void);
        int ret = audio_turn_on();
        if (ret != BK_OK)
        {
            BK_LOGE(TAG, "%s, %d, audio turn on fail, ret:%d\n", __func__, __LINE__, ret);
        }
#endif
#endif

#if (CONFIG_SYS_CPU1)
        audio_engine_init();
#endif

#if (CONFIG_SYS_CPU0)
        bk_pm_module_vote_boot_cp1_ctrl(PM_BOOT_CP1_MODULE_NAME_AUDP_AUDIO, PM_POWER_MODULE_STATE_ON);

#if CONFIG_BK_BOARDING_SERVICE
        bk_genie_core_init();
#endif
#if CONFIG_BK_SMART_CONFIG
        bk_smart_config_init();
#endif

#if CONFIG_ENABLE_AGORA_DATASTREAM
        bk_sconf_init_datastream_resource();
#if CONFIG_SENTINO_IOT
        /* Consume datastream commands (device_control / _publish_message
         * envelope from cloud). Must follow init_datastream_resource so the
         * queue exists; must precede RTC join so we don't miss the first
         * command. P0 = stub log only. */
        sentino_command_bus_register_executor(conv_ai_executor_dispatch);
        sentino_command_agora_source_init();
#endif
#endif

#if CONFIG_BUTTON
        bk_key_service_init();
#endif

#if CONFIG_BAT_MONITOR
        extern void battery_monitor_init(void);
        battery_monitor_init();
#if CONFIG_SENTINO_IOT
        /* Periodic battery/charge sampler that pushes the read-only DPs
         * to cloud on change. Must follow battery_monitor_init so the
         * driver's xGlobalHandle is open before we sample. */
        extern void app_battery_dp_init(void);
        app_battery_dp_init();
#endif
#endif

#endif

#if CONFIG_USBD_MSC
        extern void msc_storage_init(void);
        msc_storage_init();
#endif
    }
    else
    {
#if (CONFIG_SYS_CPU0)
        bk_init();
        bk_enter_deepsleep();
#endif
    }

#if (CONFIG_SYS_CPU1)
    voide_engine_init();
#endif

    return 0;
}
