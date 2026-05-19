#include <common/sys_config.h>

#if CONFIG_SENTINO_IOT

#include <string.h>
#include <components/log.h>

#include "cJSON.h"
#include "sentino_command_bus.h"
#include "sentino_command_router.h"

#define TAG "cmd_router"
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Fixed-size table — predictable, no alloc. 16 is well above today's
 * single action; bump if a project genuinely needs more. */
#define MAX_ACTIONS 16

static struct {
    const char                       *name;
    sentino_command_action_handler_t  cb;
} s_actions[MAX_ACTIONS];

static int  s_action_count = 0;
static bool s_initialized  = false;

/* Bus executor callback: linear-scan the action table by name. The table
 * is tiny in practice (single digits) so strcmp ladder beats hashing. */
static int router_dispatch(const char *executor,
                           const cJSON *parameters,
                           int priority)
{
    for (int i = 0; i < s_action_count; i++) {
        if (strcmp(executor, s_actions[i].name) == 0) {
            return s_actions[i].cb(parameters, priority);
        }
    }
    LOGW("no handler for executor=%s\n", executor);
    return -1;
}

int sentino_command_router_init(void)
{
    if (s_initialized) {
        return 0;
    }
    sentino_command_bus_register_executor(router_dispatch);
    s_initialized = true;
    return 0;
}

int sentino_command_router_register_action(const char *name,
                                           sentino_command_action_handler_t cb)
{
    if (!name || !cb) {
        return -1;
    }
    if (s_action_count >= MAX_ACTIONS) {
        LOGE("action table full (%d), dropped %s\n", MAX_ACTIONS, name);
        return -1;
    }
    s_actions[s_action_count].name = name;
    s_actions[s_action_count].cb   = cb;
    s_action_count++;
    return 0;
}

#endif /* CONFIG_SENTINO_IOT */
