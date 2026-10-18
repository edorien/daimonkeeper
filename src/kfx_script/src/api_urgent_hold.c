/** @file api_urgent_hold.c
 *     The urgent hold (api_urgent_hold.h).
 */
#include "pre_inc.h"
#include "api_urgent_hold.h"

#include "bflib_basics.h"
#include "bflib_datetm.h"
#include "external_seat.h"
#include "game_legacy.h"
#include "kfx_sim_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static enum UrgentHoldMode s_mode = UHM_None;
static int64_t s_slow_tps = URGENT_HOLD_DEFAULT_SLOW_TPS;
static int64_t s_cap_ms = URGENT_HOLD_DEFAULT_MS;

static TbBool s_active = false;
static enum UrgentHoldMode s_active_mode = UHM_None;
static uint32_t s_waiting = 0;            /**< seats (bit per player) the hold waits for */
static TbClockMSec s_started = 0;
static int64_t s_started_turn = 0;
static TbBool s_paused_it = false;        /**< the hold paused the game (so its end resumes it) */
static int64_t s_advance_left = 0;        /**< an advance the hold interrupted: turns to give back */
static int64_t s_prev_tps = 0;            /**< the speed the hold slowed from */
static int64_t s_seq[32];                 /**< per seat: the latest urgent decision the hold waits for */

void api_urgent_hold_set_policy(enum UrgentHoldMode mode, int64_t slow_tps, int64_t cap_ms)
{
    s_mode = mode;
    s_slow_tps = (slow_tps < 1) ? 1 : ((slow_tps > 100) ? 100 : slow_tps);
    s_cap_ms = (cap_ms < URGENT_HOLD_MIN_MS) ? URGENT_HOLD_MIN_MS : ((cap_ms > URGENT_HOLD_MAX_MS) ? URGENT_HOLD_MAX_MS : cap_ms);
}

enum UrgentHoldMode api_urgent_hold_mode(void)
{
    return s_mode;
}

const char *api_urgent_hold_mode_name(enum UrgentHoldMode mode)
{
    switch (mode) {
    case UHM_Pause: return "pause";
    case UHM_Slow: return "slow";
    default: return "none";
    }
}

static void forget(void)
{
    s_active = false;
    s_active_mode = UHM_None;
    s_waiting = 0;
    s_paused_it = false;
    s_advance_left = 0;
    s_prev_tps = 0;
}

void api_urgent_hold_reset(void)
{
    forget();
    s_mode = UHM_None;
    s_slow_tps = URGENT_HOLD_DEFAULT_SLOW_TPS;
    s_cap_ms = URGENT_HOLD_DEFAULT_MS;
}

/** The hold ends: the game runs again as it did before it (an interrupted advance gets its turns back). */
static void release(const char *why)
{
    if (!s_active) return;
    if (s_active_mode == UHM_Pause) {
        if (s_paused_it && flag_is_set(kfx_sim_state.operation_flags, GOF_Paused) && extseat_agent_owns_pause()) {
            if (s_advance_left > 0) extseat_advance(s_advance_left);
            else extseat_resume();
        }
    } else if (s_active_mode == UHM_Slow) {
        // Only if nobody changed the speed meanwhile.
        if ((s_prev_tps > 0) && ((int64_t)kfx_sim_state.turns_per_second == s_slow_tps)) {
            kfx_sim_state.turns_per_second = (int32_t)s_prev_tps;
        }
    }
    JUSTMSG("Urgent hold (%s) ended after %d ms: %s", api_urgent_hold_mode_name(s_active_mode),
            (int)(LbTimerClock() - s_started), why);
    forget();
}

void api_urgent_hold_end(const char *why)
{
    release(why);
}

void api_urgent_hold_begin(PlayerNumber p, int64_t seq)
{
    if ((s_mode == UHM_None) || (p < 0) || (p >= 32)) return;
    if (!extseat_may_pause()) return; // a game a human plays is never held for an agent
    if (s_active) {
        s_waiting |= (1u << p);
        s_seq[p] = seq;
        return;
    }
    if (s_mode == UHM_Pause) {
        const TbBool paused = flag_is_set(kfx_sim_state.operation_flags, GOF_Paused);
        if (paused && !extseat_advancing()) return; // already stopped: the agent has its time
        s_advance_left = extseat_advance_left();
        extseat_pause();
        s_paused_it = true;
    } else {
        if ((int64_t)kfx_sim_state.turns_per_second <= s_slow_tps) return; // already as slow as that
        s_prev_tps = kfx_sim_state.turns_per_second;
        kfx_sim_state.turns_per_second = (int32_t)s_slow_tps;
    }
    s_active = true;
    s_active_mode = s_mode;
    s_waiting = (1u << p);
    s_seq[p] = seq;
    s_started = LbTimerClock();
    s_started_turn = (int64_t)get_gameturn();
    JUSTMSG("Urgent hold (%s) for player %d at turn %d", api_urgent_hold_mode_name(s_mode), (int)p, (int)s_started_turn);
}

void api_urgent_hold_answered(PlayerNumber p, int64_t seq)
{
    if (!s_active || (p < 0) || (p >= 32) || (seq == 0)) return;
    if ((seq > 0) && (seq < s_seq[p])) return; // an answer to an earlier decision: the latest one still waits
    s_waiting &= ~(1u << p);
    if (s_waiting == 0) release("answered");
}

void api_urgent_hold_cancel(void)
{
    if (!s_active) return;
    JUSTMSG("Urgent hold (%s) handed to the agent's own pause/speed", api_urgent_hold_mode_name(s_active_mode));
    forget();
}

void api_urgent_hold_tick(void)
{
    if (!s_active) return;
    if ((int64_t)get_gameturn() < s_started_turn) { forget(); return; } // a load: that game is gone
    if ((int64_t)(LbTimerClock() - s_started) > s_cap_ms) {
        WARNLOG("Urgent hold: no answer within %d ms; the game runs again", (int)s_cap_ms);
        release("the cap passed");
    }
}

TbBool api_urgent_hold_waiting(PlayerNumber p, enum UrgentHoldMode *mode, int64_t *ms_left)
{
    if (!s_active || (p < 0) || (p >= 32) || ((s_waiting & (1u << p)) == 0)) return false;
    if (mode != NULL) *mode = s_active_mode;
    if (ms_left != NULL) {
        const int64_t left = s_cap_ms - (int64_t)(LbTimerClock() - s_started);
        *ms_left = (left < 0) ? 0 : left;
    }
    return true;
}

#ifdef __cplusplus
}
#endif
