#ifndef __SENTINO_CONV_AI_COMMAND_H__
#define __SENTINO_CONV_AI_COMMAND_H__

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* sentino_interface — ConvoAI datastream command consumer.
 *
 * Cloud (DragonFlow) emits device-control commands as ConvoAI user messages
 * over the Agora RTC datastream. The Agora callback in agora_rtc.c pushes the
 * raw JSON bytes into `datastream_queue`. This module spawns a worker that
 * pops, parses the envelope (root.payload.content.actions[]), and dispatches
 * each action to a registered executor callback.
 *
 * P0: stub-log only — register a callback that just logs each action to
 * confirm the wire end-to-end. Real LCD / vibration / volume drivers wire
 * up later.
 *
 * Init order (see beken_genie/main/app_main.c around bk_sconf_init_datastream_resource()):
 *   1. bk_sconf_init_datastream_resource()    // creates datastream_queue
 *   2. bk_conv_ai_command_register_executor(...)
 *   3. bk_conv_ai_command_init()              // spawns worker
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
 * Return value is logged but otherwise ignored in P0.
 */
typedef int (*bk_conv_ai_executor_t)(const char *executor,
                                     const cJSON *parameters,
                                     int priority);

/* Register the executor callback. Single global slot — calling again
 * replaces the previous callback. Safe to call before init. */
int bk_conv_ai_command_register_executor(bk_conv_ai_executor_t cb);

/* Spawn the consumer worker. No-op if already running. Requires
 * datastream_queue to have been created by bk_sconf_init_datastream_resource()
 * first, otherwise returns -1 and the worker is not started. */
int bk_conv_ai_command_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_CONV_AI_COMMAND_H__ */
