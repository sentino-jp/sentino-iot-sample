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
 */

/* Genie LCD emotion vocabulary. Lives here (business header) — NOT in
 * bk_app_event/, which is a generic event subsystem and must not carry
 * product-specific vocabulary. Other projects (smart lock, thermostat)
 * forking this firmware don't have emotions and should not be forced
 * to import this enum. */
typedef enum {
    EMOTION_HAPPY = 0,
    EMOTION_SAD,
    EMOTION_ANGRY,
    EMOTION_SURPRISED,
    EMOTION_NEUTRAL,
    EMOTION_THINKING,
    EMOTION_SLEEPY,
    EMOTION_LOVING,
    EMOTION_CURIOUS,
} app_emotion_t;

/* Translate an emotion to its AVI filename on the SD/flash VFS. Returns
 * a static const string (.rodata) — safe to pass cross-thread as an
 * app_event message param (pointer outlives the event). */
static inline const char *app_emotion_2_avi_file(app_emotion_t emotion)
{
    switch (emotion) {
        case EMOTION_HAPPY:     return "/happy.avi";
        case EMOTION_SAD:       return "/sad.avi";
        case EMOTION_ANGRY:     return "/angry.avi";
        case EMOTION_SURPRISED: return "/surprise.avi";
        case EMOTION_NEUTRAL:   return "/neutral.avi";
        case EMOTION_THINKING:  return "/thinking.avi";
        case EMOTION_SLEEPY:    return "/sleepy.avi";
        case EMOTION_LOVING:    return "/love.avi";
        case EMOTION_CURIOUS:   return "/curious.avi";
        default:                break;
    }
    return "/neutral.avi";
}

/* Register this action with the Sentino command router. Idempotent at the
 * router level — calling twice produces a duplicate table entry, first
 * wins on dispatch, so don't. */
void genie_action_display_emotion_register(void);

#ifdef __cplusplus
}
#endif
#endif /* __GENIE_ACTION_DISPLAY_EMOTION_H__ */
