#include <common/sys_config.h>

#include "app_indicate_state.h"
#include "countdown_app.h"   /* COUNTDOWN_TICKET_* + update_countdown   */
#include "led_app.h"         /* INDICATES_* / WARNING_* + led_blink     */

/* Shared device-status state — single owner. Worker thread is the sole
 * caller of mutators today; the static-fn callers below don't lock
 * because of that. If a producer outside the worker ever needs to mutate,
 * gate everything below with a mutex. */

static uint32_t s_is_standby              = 1;
static uint32_t s_is_joined_agent         = 0;
static uint32_t s_is_network_provisioning = 0;
static uint32_t s_warning_state           = 0;
static uint32_t s_indicates_state         = (1 << INDICATES_POWER_ON);
static uint32_t s_active_tickets          = (1 << COUNTDOWN_TICKET_STANDBY);

static bool     s_skip_countdown_this_tick = false;

/* ── booleans ────────────────────────────────────────────────────────── */

void app_indicate_state_set_standby(bool standby)              { s_is_standby = standby ? 1 : 0; }
bool app_indicate_state_get_standby(void)                       { return s_is_standby != 0; }

void app_indicate_state_set_joined_agent(bool joined)          { s_is_joined_agent = joined ? 1 : 0; }
bool app_indicate_state_get_joined_agent(void)                  { return s_is_joined_agent != 0; }

void app_indicate_state_set_network_provisioning(bool prov)    { s_is_network_provisioning = prov ? 1 : 0; }
bool app_indicate_state_get_network_provisioning(void)          { return s_is_network_provisioning != 0; }

/* ── bitmasks ────────────────────────────────────────────────────────── */

void     app_indicate_state_warning_set(uint32_t mask)         { s_warning_state |= mask; }
void     app_indicate_state_warning_clear(uint32_t mask)       { s_warning_state &= ~mask; }
uint32_t app_indicate_state_warning_get(void)                   { return s_warning_state; }

void     app_indicate_state_indicates_set(uint32_t mask)       { s_indicates_state |= mask; }
void     app_indicate_state_indicates_clear(uint32_t mask)     { s_indicates_state &= ~mask; }
uint32_t app_indicate_state_indicates_get(void)                 { return s_indicates_state; }

void     app_indicate_state_tickets_set(uint32_t mask)         { s_active_tickets |= mask; }
void     app_indicate_state_tickets_clear(uint32_t mask)       { s_active_tickets &= ~mask; }
uint32_t app_indicate_state_tickets_get(void)                   { return s_active_tickets; }

/* ── tick ────────────────────────────────────────────────────────────── */

void app_indicate_state_skip_countdown(void)
{
    s_skip_countdown_this_tick = true;
}

void app_indicate_state_tick(void)
{
#if CONFIG_COUNTDOWN
    if (!s_skip_countdown_this_tick) {
        update_countdown(s_active_tickets);
    }
#endif
#if CONFIG_LED_BLINK
    /* led_blink takes warning by pointer — current API. The function may
     * mutate the *warning_state in some BSP impls; we pass our copy so
     * external mutations don't leak through. (If led_blink is intended
     * to be read-only, this is harmless either way.) */
    uint32_t warning = s_warning_state;
    led_blink(&warning, s_indicates_state);
    s_warning_state = warning;
#endif
    s_skip_countdown_this_tick = false;
}
