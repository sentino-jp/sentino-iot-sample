#ifndef __GENIE_ACTION_VOLUME_SET_H__
#define __GENIE_ACTION_VOLUME_SET_H__

#ifdef __cplusplus
extern "C" {
#endif

/* AI agent action that bridges to the `volume_set` DP.
 *
 * Wire (Sentino device command schema):
 *   { "executor": "volume_set",
 *     "parameters": { "value": <int 0-10> },
 *     "priority": <int> }
 *
 * Per plan §6 "重叠 executor 合流": this handler does NOT touch hardware
 * directly. It builds a dp_obj_t and hands it to app_dp_apply_set() —
 * the same entry point cloud-side property_set uses. Result: AI-driven
 * volume changes flow through the existing cloud_to_local mapping +
 * actuator + property_report echo, so the cloud's view of the DP stays
 * accurate and the hardware code is not duplicated.
 *
 * Naming convention (see sentino_command_router.h):
 *   DP-overlap actions → executor name == DP identifier verbatim
 *                        parameters.value == new DP value
 *   So executor "volume_set" matches DP_ID_VOLUME_SET (not "set_volume",
 *   which would force a rename layer).
 */

void genie_action_volume_set_register(void);

#ifdef __cplusplus
}
#endif
#endif /* __GENIE_ACTION_VOLUME_SET_H__ */
