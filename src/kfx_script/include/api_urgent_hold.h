/**
 * @file api_urgent_hold.h
 * @brief Time to think, where pausing is fair (docs/plans/llm-bridge-realtime.md, Phase 6).
 *
 * With the policy on (set_decision_policy on_urgent "pause" or "slow"), an urgent decision (severity 1 or 0: a fight, an
 * intruder, a loss, an alarm, the heart attacked) holds the game for the agent -- paused (an agent pause), or slowed to a
 * few turns a second -- until the agent answers it (any accepted order from the seat, or ack_decision), until the agent
 * takes the pause or the speed into its own hands (set_pause, advance_turns, set_game_speed), or until a wall-clock cap
 * passes (60 s by default). This is what an agent playing alone used to do by hand, stepping the game in short runs, now
 * without the round trips. Only where the agent may pause anyway (no human keeper, or the Skirmish page's LLM Pause): a
 * game a human plays is never held for an agent. A hold that interrupts an advance (advance_turns) gives the rest of
 * the advance back when it ends. Session state, not saved: a load or a new level starts without one.
 */
#ifndef DK_API_URGENT_HOLD_H
#define DK_API_URGENT_HOLD_H

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

enum UrgentHoldMode { UHM_None = 0, UHM_Pause, UHM_Slow };

#define URGENT_HOLD_DEFAULT_MS 60000
#define URGENT_HOLD_MIN_MS 1000
#define URGENT_HOLD_MAX_MS 600000
#define URGENT_HOLD_DEFAULT_SLOW_TPS 4

/** The policy (any seat's agent sets it for the game). `cap_ms` is clamped to URGENT_HOLD_MIN_MS..URGENT_HOLD_MAX_MS. */
void api_urgent_hold_set_policy(enum UrgentHoldMode mode, int64_t slow_tps, int64_t cap_ms);
enum UrgentHoldMode api_urgent_hold_mode(void);
const char *api_urgent_hold_mode_name(enum UrgentHoldMode mode);
/** Urgent decision `seq` went to seat `p`: start a hold (or add the seat, and this decision, to the one running). */
void api_urgent_hold_begin(PlayerNumber p, int64_t seq);
/** Seat `p` answered decision `seq` (-1: an answer that does not say which, a raw client's order -- it answers them all;
 *  0: not an answer, e.g. a trigger's order). The seat is answered once its latest urgent decision is; once every seat
 *  the hold waits for is, the game runs again at its own pace. */
void api_urgent_hold_answered(PlayerNumber p, int64_t seq);
/** The agent took the pause or the speed into its own hands: the hold ends, and nothing is restored. */
void api_urgent_hold_cancel(void);
/** The hold ends as if answered (the agent's connection went away): the game runs again at its own pace. */
void api_urgent_hold_end(const char *why);
/** Each poll: the wall-clock cap, and a load (the turn went back) ends a hold. */
void api_urgent_hold_tick(void);
/** Whether a hold waits for seat `p`, its mode and the milliseconds left before the cap. */
TbBool api_urgent_hold_waiting(PlayerNumber p, enum UrgentHoldMode *mode, int64_t *ms_left);
/** Forgets the policy and any hold (tests, a new session). */
void api_urgent_hold_reset(void);

#ifdef __cplusplus
}
#endif

#endif
