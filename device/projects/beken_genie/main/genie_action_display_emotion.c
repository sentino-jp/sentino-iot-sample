#include <common/sys_config.h>

#if CONFIG_SENTINO_IOT && CONFIG_ENABLE_AGORA_DATASTREAM

#include <string.h>
#include <components/log.h>
#include <os/os.h>

#include "cJSON.h"
#include "app_event.h"
#include "sentino_command_router.h"
#include "genie_action_display_emotion.h"

#define TAG "act_emotion"

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
#define IDLE_MS              8000
#define IDLE_DEFAULT_AVI     "/genie_eye.avi"

static beken2_timer_t s_idle_timer;
static bool           s_idle_timer_inited = false;

static void idle_timer_cb(void *larg, void *rarg)
{
    (void)larg; (void)rarg;
    BK_LOGW(TAG, "idle timeout, restoring %s\n", IDLE_DEFAULT_AVI);
    /* Post to app_event worker — lvgl_app_play does a sync cross-core
     * mailbox call that the timer-service thread cannot legally drive
     * (observed: media_major_mailbox ack flag error 97 → idle restore
     * fails). Worker thread is allowed to block on the mailbox, and
     * routing through it also serializes with the emotion change path. */
    app_event_send_msg(APP_EVT_CONVOAI_RESTORE_IDLE_AVI, 0);
}

static void arm_idle_timer(void)
{
    bk_err_t ret;
    if (!s_idle_timer_inited) {
        ret = rtos_init_oneshot_timer(&s_idle_timer, IDLE_MS,
                                      idle_timer_cb, NULL, NULL);
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

/* Handler for `display_emotion` actions. Router dispatched us by name —
 * we just need to inspect parameters and act. LOGW so the line survives
 * release builds (one log per action is low frequency, won't flood). */
static int handler(const cJSON *parameters, int priority)
{
    char *params_str = parameters ? cJSON_PrintUnformatted((cJSON *)parameters) : NULL;
    BK_LOGW(TAG, "priority=%d params=%s\n",
            priority, params_str ? params_str : "{}");
    if (params_str) cJSON_free(params_str);

    const char *et = cJSON_GetStringValue(
                         cJSON_GetObjectItemCaseSensitive(parameters, "emotion_type"));
    if (!et) {
        BK_LOGW(TAG, "missing emotion_type\n");
        return -1;
    }
    for (size_t i = 0; i < sizeof(s_emotion_map)/sizeof(s_emotion_map[0]); i++) {
        if (strcmp(et, s_emotion_map[i].name) == 0) {
            app_event_send_msg(APP_EVT_CONVOAI_CHANGE_LVGL_RESOURCE,
                               (uint32_t)s_emotion_map[i].value);
            arm_idle_timer();
            return 0;
        }
    }
    BK_LOGW(TAG, "unsupported emotion: %s\n", et);
    return -1;
}

void genie_action_display_emotion_register(void)
{
    sentino_command_router_register_action("display_emotion", handler);
}

#endif /* CONFIG_SENTINO_IOT && CONFIG_ENABLE_AGORA_DATASTREAM */
