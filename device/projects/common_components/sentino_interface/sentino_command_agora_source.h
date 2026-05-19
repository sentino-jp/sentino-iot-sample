#ifndef __SENTINO_COMMAND_AGORA_SOURCE_H__
#define __SENTINO_COMMAND_AGORA_SOURCE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* sentino_interface — Agora datastream source for the Sentino command bus.
 *
 * Implements L1 + L2 of the wire stack (see plans/device-command-bus.md):
 *   L1: deframe Agora datastream binary  <msgid>|<idx>|<total>|<b64>
 *   L2: strip ConvoAI publish envelope   {object:"message.user", content:{...}, ...}
 *
 * The stripped Sentino `content` cJSON is handed to sentino_command_bus,
 * which owns the L3 (command schema parsing + executor dispatch).
 *
 * The Agora callback in agora_rtc.c is the producer, pushing raw datastream
 * frames into `datastream_queue` (owned by bk_smart_config_sentino_adapter).
 * This module's worker is the consumer.
 *
 * Init order (see beken_genie/main/app_main.c around bk_sconf_init_datastream_resource()):
 *   1. bk_sconf_init_datastream_resource()            // creates datastream_queue
 *   2. sentino_command_bus_register_executor(...)     // on the bus side
 *   3. sentino_command_agora_source_init()            // spawns this worker
 */

/* Spawn the Agora datastream consumer worker. No-op if already running.
 * Requires datastream_queue to have been created by
 * bk_sconf_init_datastream_resource() first, otherwise returns -1 and the
 * worker is not started. */
int sentino_command_agora_source_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_COMMAND_AGORA_SOURCE_H__ */
