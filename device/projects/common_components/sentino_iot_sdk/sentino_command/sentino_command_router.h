#ifndef __SENTINO_COMMAND_ROUTER_H__
#define __SENTINO_COMMAND_ROUTER_H__

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* sentino_iot_sdk/sentino_command — name-keyed action router on top of bus.
 *
 * The bus (sentino_command_bus) takes a single executor callback that
 * sees every action regardless of name. Most consumers want a different
 * pattern: "I have N actions, each with its own handler, dispatched by
 * the action's executor name." This router provides that pattern as a
 * reusable SDK piece so projects don't reinvent the strcmp ladder.
 *
 * Wiring (typical app_main):
 *   sentino_command_router_init();                              // hooks into bus
 *   my_action_<foo>_register();                                 // each action self-registers
 *   my_action_<bar>_register();
 *   sentino_command_agora_source_init();                        // start the source(s)
 *
 * Each action's own .c file calls sentino_command_router_register_action()
 * to wire its name → handler. Order of registers doesn't matter; bus
 * traffic before init just gets dropped at the bus (no executor wired).
 */

/* Per-action handler signature. `executor` name is NOT passed — the router
 * has already dispatched by name, so the handler knows what it is.
 *
 * `parameters` — the action's "parameters" object. May be NULL. Owned by
 *                the parser; do NOT cJSON_Delete it. cJSON_Duplicate to
 *                outlive the call.
 * `priority`   — the action's "priority" field, 0 if missing.
 *
 * Return value is logged but otherwise ignored. */
typedef int (*sentino_command_action_handler_t)(const cJSON *parameters,
                                                int priority);

/* Hook the router into sentino_command_bus as its executor callback.
 * Idempotent — calling again is a no-op. Safe to call before or after
 * register_action() calls. */
int sentino_command_router_init(void);

/* Register a per-action handler. `name` must remain valid for the lifetime
 * of the program (typically a string literal or static const).
 *
 * Returns 0 on success, -1 on full table or NULL inputs. Duplicate names
 * are accepted but first-registered wins on dispatch — caller's
 * responsibility to avoid duplicates. */
int sentino_command_router_register_action(const char *name,
                                           sentino_command_action_handler_t cb);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_COMMAND_ROUTER_H__ */
