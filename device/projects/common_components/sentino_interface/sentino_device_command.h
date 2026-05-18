#ifndef __SENTINO_DEVICE_COMMAND_H__
#define __SENTINO_DEVICE_COMMAND_H__

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* sentino_interface — Sentino device-control protocol command consumer.
 *
 * Sentino cloud emits device-control commands (LCD / vibration / volume /
 * etc.) embedded in Agora ConvoAI user-message datastream payloads. The
 * Agora callback in agora_rtc.c pushes the raw JSON bytes into
 * `datastream_queue`. This module spawns a worker that pops, parses the
 * envelope (root.payload.content.actions[]), and dispatches each action
 * to a registered executor callback.
 *
 * The Agora datastream is just transport — the action protocol is
 * Sentino's. Naming reflects the protocol owner, not the transport.
 *
 * Init order (see beken_genie/main/app_main.c around bk_sconf_init_datastream_resource()):
 *   1. bk_sconf_init_datastream_resource()                  // creates datastream_queue
 *   2. sentino_device_command_register_executor(...)
 *   3. sentino_device_command_init()                        // spawns worker
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
typedef int (*sentino_device_command_executor_t)(const char *executor,
                                                 const cJSON *parameters,
                                                 int priority);

/* Register the executor callback. Single global slot — calling again
 * replaces the previous callback. Safe to call before init. */
int sentino_device_command_register_executor(sentino_device_command_executor_t cb);

/* Spawn the consumer worker. No-op if already running. Requires
 * datastream_queue to have been created by bk_sconf_init_datastream_resource()
 * first, otherwise returns -1 and the worker is not started. */
int sentino_device_command_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_DEVICE_COMMAND_H__ */
