/**
 * @file seat_reflexes.h
 * @brief An External seat's hand reflexes (docs/plans/llm-bridge-realtime.md, Phase 5): the combat assistant's hand rules,
 *        carried out by the seat with its own hand.
 *
 * The combat assistant's rules live in the simulation (keeper_assist_state.h), and its cast rules run there. Its hand
 * rules -- evacuate_diggers, lift_wounded, reinforce -- are an agent's only: they move creatures with the keeper's hand, the
 * hand the agent's own orders use, so the seat carries them out, as urgent pick_up_and_drop verbs in its queue's front
 * lane (external_seat.h). A firing goes through the same one-packet-a-turn hand as any order: nothing a human's hand could
 * not do, only sooner. A human player's assistant never holds these rules (config_combat_assist refuses them), and none
 * run in a multiplayer game.
 *
 *  - evacuate_diggers: the keeper's diggers within `radius` slabs of a visible enemy fighter are lifted to the rule's
 *    destination (the agent's safe spot: imps run back to defend an attacked heart, so it is best a pocket they cannot
 *    walk out of). Diggers already near the destination stay.
 *  - lift_wounded: its fighters in a fight under the rule's health (35% by default) are lifted out, to the destination or
 *    else a lair -- only when no heal rule of the assistant could heal them now (none set, the power not there, or its
 *    reserve, budget or the pay day's cap spent).
 *  - reinforce: a fight the keeper is losing or only matching (force_ratio 80 or more by default, or the rule's own
 *    conditions) gets healthy fighters from elsewhere dropped on its ground beside it -- each at least `min_match`% of the
 *    strongest enemy's force there (no level 1 fly against a level 10 knight), and only enough of them to turn the fight:
 *    if all it could send would not, it sends none (fighters fed in one by one are lost one by one).
 *
 * One firing at a time per seat (the hand carries one trip), at most once per `interval` turns (the assistant's), each rule
 * after its own cooldown. The seat keeps what they did (seat_reflexes_log) and why a matching rule held back (its refusal),
 * for the view. Session state, not saved.
 */
#ifndef DK_SEAT_REFLEXES_H
#define DK_SEAT_REFLEXES_H

#include "globals.h"
#include "bflib_netsp.h"
#include "keeper_assist_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SEAT_REFLEX_LOG 16

/** One firing (or refusal, verb_id 0) of a hand rule. */
struct SeatReflexAction {
    int64_t turn;
    uint8_t slot;
    uint8_t action;              /**< enum KeeperAssistAction */
    uint8_t count;               /**< creatures moved */
    int64_t verb_id;             /**< the pick_up_and_drop it queued */
    int64_t x, y;                /**< where they were dropped (subtiles), or the room (x = room id, y = -1) */
    ThingIndex things[KEEPER_ASSIST_HAND_MAX];
};

/** Once per simulated turn for an External seat, before its queue promotes a verb (extseat_tick). */
void seat_reflexes_tick(NetUserId user, PlayerNumber plyr_idx);
/** The latest firings, oldest first; returns how many. */
int64_t seat_reflexes_log(NetUserId user, struct SeatReflexAction *out, int64_t max);
/** Why a hand rule last held back although something matched it ("HEAL_READY", "NO_MATCH", "WOULD_NOT_TURN_IT",
 *  "NO_DROP_SPOT", "NO_LAIR", or the queue's refusal), NULL when it did not. */
const char *seat_reflexes_refusal(NetUserId user, int slot);
void seat_reflexes_forget(NetUserId user);
void seat_reflexes_reset(void);

#ifdef __cplusplus
}
#endif

#endif
