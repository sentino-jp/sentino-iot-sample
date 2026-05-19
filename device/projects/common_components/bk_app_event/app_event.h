#pragma once

/* Generic event subsystem — NO product-specific vocabulary here. Business
 * enums (e.g. EMOTION_*) live in the project's own headers, e.g.
 * beken_genie/main/genie_action_display_emotion.h. */

typedef enum
{
    APP_EVT_ASR_WAKEUP = 0,
    APP_EVT_ASR_STANDBY,
    APP_EVT_IR_MODE_SWITCH,
    APP_EVT_NETWORK_PROVISIONING,
    APP_EVT_NETWORK_PROVISIONING_SUCCESS,
    APP_EVT_NETWORK_PROVISIONING_FAIL,
    APP_EVT_RECONNECT_NETWORK,
    APP_EVT_RECONNECT_NETWORK_SUCCESS,
    APP_EVT_RECONNECT_NETWORK_FAIL,
    APP_EVT_RTC_REJOIN_SUCCESS,
    APP_EVT_RTC_CONNECTION_LOST,
    APP_EVT_AGENT_JOINED,
    APP_EVT_AGENT_OFFLINE,
    APP_EVT_AGENT_START_FAIL,
    APP_EVT_AGENT_DEVICE_REMOVE,
    APP_EVT_CLOSE_BLUETOOTH,
    APP_EVT_LOW_VOLTAGE,
    APP_EVT_CHARGING,
    APP_EVT_SHUTDOWN_LOW_BATTERY,
    APP_EVT_OTA_START,
    APP_EVT_OTA_SUCCESS,
    APP_EVT_OTA_FAIL,
    APP_EVT_SYNC_FLASH,

    APP_EVT_POWER_ON,

    APP_EVT_CONVOAI_OTA_CHECK,
    APP_EVT_CONVOAI_CONFIG_LOADING,
    APP_EVT_CONVOAI_START_TIMER_EXPIRE,
    APP_EVT_CONVOAI_EXIT,
    /* Play an AVI on the LCD via lvgl. msg.param is (uintptr_t)(const char *)
     * filename — usually a .rodata string literal so cross-thread is safe.
     * Producer translates whatever business vocabulary (emotion, etc.) to a
     * filename before sending; worker just calls lvgl_app_play(filename). */
    APP_EVT_CONVOAI_PLAY_AVI,
    APP_EVT_CONVOAI_RESTORE_IDLE_AVI,
    APP_EVT_SMART_CONFIG_START,

    /* Sentino MQTT (re)connected. Engine registers a handler via
     * app_event_register_handler() to run publish_bind → publish_info →
     * cloud_ready_cb in this worker context. Posted from the mqtts reader
     * task — handler must own the heavy work, not the dispatcher. */
    APP_EVT_CLOUD_CONNECTED,

    /* Local volume changed (physical key). param = new local level
     * (NOT cloud-mapped; converted in app_dp_handler before report).
     * Posted from key_thread; handled in app_event worker → DP report. */
    APP_EVT_VOLUME_CHANGED,
} app_evt_type_t;

void app_event_init(void);
bk_err_t app_event_send_msg(uint32_t event, uint32_t param);

typedef struct
{
    beken_thread_t thread;
    beken_queue_t queue;
} app_evt_info_t;

typedef struct
{
    app_evt_type_t event;
    uint32_t param;
} app_evt_msg_t;

typedef void (*app_event_callback_t)(app_evt_msg_t *msg, void *user_data);
typedef struct app_event_handler {
    app_evt_type_t            event_type;
    app_event_callback_t      callback;
    void                     *user_data;
    int                       priority;     /* lower runs first */
    struct app_event_handler *next;
} app_event_handler_t;

/* Listener execution order is by priority — the worker iterates the chain
 * in ascending priority order, so register with the right band:
 *
 *   APP_EVT_PRIORITY_STATE    (0)   — app_indicate_state mutations: must
 *                                     run BEFORE anything that reads state
 *                                     (LED, UI, prompt) so the snapshot
 *                                     they see reflects the new event.
 *   APP_EVT_PRIORITY_BUSINESS (100) — hardware / subsystem effects:
 *                                     bk_pm vote, wifi mode, sentino engine
 *                                     start/stop, lvgl init/deinit, etc.
 *   APP_EVT_PRIORITY_UI       (200) — user-facing late effects: prompt
 *                                     tone, lvgl swap. Reads state; runs
 *                                     after business so audio/screen
 *                                     reflects both. */
#define APP_EVT_PRIORITY_STATE       0
#define APP_EVT_PRIORITY_BUSINESS    100
#define APP_EVT_PRIORITY_UI          200


int app_event_register_handler(app_evt_type_t event_type,
                               app_event_callback_t callback,
                               void *user_data,
                               int priority);
