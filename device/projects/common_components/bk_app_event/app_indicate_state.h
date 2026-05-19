#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* app_indicate_state — single owner of device-status flags + UI tick driver.
 *
 * Before this module existed, app_event_thread carried 6 file-static state
 * variables that 30+ event case bodies mutated by direct bit ops. The
 * sprawl made it impossible to read a single case body and know what the
 * device state would look like afterwards. Centralizing here:
 *
 *   - 3 booleans:  standby / joined_agent / network_provisioning
 *   - 3 bitmasks:  warning / indicates / tickets
 *
 * Handlers (any thread, but in practice only app_event worker) call the
 * setters / clearers / getters here instead of poking bits directly.
 * After running the listener chain for one event, the worker invokes
 * app_indicate_state_tick() which drives LED + countdown from the current
 * state — a single place that knows how state maps to UI.
 *
 * battery / volume cases that historically returned `skip_countdown=true`
 * now call app_indicate_state_skip_countdown() — a one-shot flag the tick
 * consumes and resets each iteration.
 */

/* ── booleans ────────────────────────────────────────────────────────── */
void app_indicate_state_set_standby(bool standby);
bool app_indicate_state_get_standby(void);

void app_indicate_state_set_joined_agent(bool joined);
bool app_indicate_state_get_joined_agent(void);

void app_indicate_state_set_network_provisioning(bool prov);
bool app_indicate_state_get_network_provisioning(void);

/* ── bitmasks (set/clear allow batch updates via |-mask) ─────────────── */
void     app_indicate_state_warning_set(uint32_t mask);
void     app_indicate_state_warning_clear(uint32_t mask);
uint32_t app_indicate_state_warning_get(void);

void     app_indicate_state_indicates_set(uint32_t mask);
void     app_indicate_state_indicates_clear(uint32_t mask);
uint32_t app_indicate_state_indicates_get(void);

void     app_indicate_state_tickets_set(uint32_t mask);
void     app_indicate_state_tickets_clear(uint32_t mask);
uint32_t app_indicate_state_tickets_get(void);

/* ── tick control ────────────────────────────────────────────────────── */

/* Mark "skip the countdown update for this iteration". Consumed + reset
 * by the next app_indicate_state_tick() call. Replaces the previous
 * `return true` skip-signal from battery / volume handlers. */
void app_indicate_state_skip_countdown(void);

/* Drive countdown + LED from current state. Called by app_event worker
 * after every listener chain dispatch. Idempotent — safe to call when
 * state hasn't changed. */
void app_indicate_state_tick(void);

#ifdef __cplusplus
}
#endif
