#include <common/sys_config.h>

#if CONFIG_SENTINO_IOT

#include <string.h>
#include <components/log.h>

#include "cJSON.h"
#include "sentino_command_bus.h"

#define TAG "cmd_bus"
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static sentino_command_executor_t s_executor_cb = NULL;

int sentino_command_bus_register_executor(sentino_command_executor_t cb)
{
    s_executor_cb = cb;
    return 0;
}

static int dispatch_actions(const cJSON *actions)
{
    int dispatched = 0;
    const cJSON *act = NULL;
    cJSON_ArrayForEach(act, actions) {
        const cJSON *exec_node = cJSON_GetObjectItemCaseSensitive(act, "executor");
        const cJSON *params    = cJSON_GetObjectItemCaseSensitive(act, "parameters");
        const cJSON *prio_node = cJSON_GetObjectItemCaseSensitive(act, "priority");

        const char *exec = cJSON_GetStringValue(exec_node);
        if (!exec) {
            LOGW("action missing executor, skip\n");
            continue;
        }
        int prio = cJSON_IsNumber(prio_node) ? (int)prio_node->valuedouble : 0;

        if (s_executor_cb) {
            s_executor_cb(exec, params, prio);
            dispatched++;
        } else {
            LOGW("no executor registered, drop %s\n", exec);
        }
    }
    /* TODO(P1): stable-sort by priority desc before dispatch (StarBuddy
     * semantics). Stub-log dispatch is order-insensitive. */
    return dispatched;
}

int sentino_command_bus_dispatch_content(const cJSON *content)
{
    if (!cJSON_IsObject(content)) {
        LOGW("content not object, type=%d\n", content ? content->type : -1);
        return -1;
    }

    const char *ct = cJSON_GetStringValue(
                         cJSON_GetObjectItemCaseSensitive(content, "type"));
    if (!ct || strcmp(ct, "command") != 0) {
        LOGW("content.type=%s (not 'command')\n", ct ? ct : "(null)");
        return -1;
    }

    const cJSON *actions = cJSON_GetObjectItemCaseSensitive(content, "actions");
    if (!cJSON_IsArray(actions)) {
        LOGW("command without actions array\n");
        return -1;
    }

    const char *cmd_id = cJSON_GetStringValue(
                             cJSON_GetObjectItemCaseSensitive(content, "command_id"));
    LOGW("cmd %s, %d action(s)\n",
         cmd_id ? cmd_id : "?", cJSON_GetArraySize(actions));

    return dispatch_actions(actions);
}

#endif /* CONFIG_SENTINO_IOT */
