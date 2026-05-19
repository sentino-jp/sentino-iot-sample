#ifndef __GENIE_ACTION_DISPLAY_EMOTION_H__
#define __GENIE_ACTION_DISPLAY_EMOTION_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Reference action handler for the `display_emotion` command in the genie
 * toy demo. Plays a matching LCD AVI via app_event mailbox, then auto-
 * restores the idle eye after a quiet window.
 *
 * This is one example of a Sentino command action. Customers fork the
 * genie project will likely have different actions (smart-lock unlock,
 * thermostat set, ...). Pattern:
 *   - One .{c,h} per action under beken_<project>/main/
 *   - Each exposes <name>_register() that calls
 *     sentino_command_router_register_action("<executor>", handler)
 *   - app_main wires them up after sentino_command_router_init()
 *
 * The cloud-side `display_emotion` parameters schema:
 *   { "emotion_type": "happy" | "sad" | ... }
 * Names align with EMOTION_* in bk_app_event/app_event.h.
 */

/* Register this action with the Sentino command router. Idempotent at the
 * router level — calling twice produces a duplicate table entry, first
 * wins on dispatch, so don't. */
void genie_action_display_emotion_register(void);

#ifdef __cplusplus
}
#endif
#endif /* __GENIE_ACTION_DISPLAY_EMOTION_H__ */
