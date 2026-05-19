#ifndef __SENTINO_COMMAND_BUS_H__
#define __SENTINO_COMMAND_BUS_H__

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* sentino_iot_sdk/sentino_command — Sentino device command bus.
 *
 * Sentino device command schema (L3 in plans/device-command-bus.md):
 *   { "type":"command", "command_id":"...", "actions":[
 *       {"executor":"<name>", "parameters":{...}, "priority":N}, ...
 *   ]}
 *
 * Source-agnostic: the caller (Agora source today, MCP / others tomorrow)
 * is responsible for delivering the already-stripped Sentino `content`
 * cJSON node. The bus parses the command schema and dispatches each
 * action to a registered executor callback.
 *
 * Single global executor slot. Init order:
 *   1. sentino_command_bus_register_executor(cb)
 *   2. one or more sources call sentino_command_bus_dispatch_content() per inbound msg
 */

/* Executor callback. Invoked once per action in a command envelope.
 *
 * `executor`   — action's "executor" field, e.g. "lcd", "vibration", "volume".
 *                Always non-NULL when called.
 * `parameters` — the action's "parameters" object. May be NULL if the cloud
 *                sent an action with no parameters. The cJSON node is owned
 *                by the parser; do NOT cJSON_Delete it. If you need to
 *                outlive this call, cJSON_Duplicate it.
 * `priority`   — the action's "priority" field, 0 if missing.
 *
 * Return value is logged but otherwise ignored.
 */
typedef int (*sentino_command_executor_t)(const char *executor,
                                          const cJSON *parameters,
                                          int priority);

/* Register the executor callback. Single global slot — calling again
 * replaces the previous callback. Safe to call any time. */
int sentino_command_bus_register_executor(sentino_command_executor_t cb);

/* Dispatch a Sentino command envelope.
 *
 * `content` — the Sentino content cJSON node (already stripped of any
 *             transport / outer envelope by the source). Expected shape:
 *               { "type":"command", "command_id":"...", "actions":[...] }
 *             Non-command types are logged and dropped.
 *
 * Returns the number of actions dispatched, or negative on parse error.
 * Caller retains ownership of `content`. */
int sentino_command_bus_dispatch_content(const cJSON *content);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_COMMAND_BUS_H__ */
