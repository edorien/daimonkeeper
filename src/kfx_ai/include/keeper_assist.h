/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file keeper_assist.h
 *     The combat assistant: a keeper's standing orders for fights, carried out by the simulation every few turns
 *     (docs/newFeatures/llm/01-combat-assistant-design.md).
 * @par Purpose:
 *     process_keeper_assists() reads each keeper's rules (kfx_sim_state.keeper_assist, keeper_assist_state.h) and casts
 *     with the engine's own power functions, the way the computer assistants do -- as part of the simulation, so a
 *     replay, a save and a resync all agree. The choice of what to do is keeper_assist_decide(), which sees the world
 *     only through the facts and callbacks it is given, so it can be tested without one.
 */
/******************************************************************************/
#ifndef DK_KEEPER_ASSIST_H
#define DK_KEEPER_ASSIST_H

#include "globals.h"
#include "keeper_assist_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

struct KeeperAssistState;
struct Thing;

/** Most candidate targets looked at in one evaluation. */
#define KEEPER_ASSIST_MAX_CANDIDATES 128

/** Target-set bits a candidate belongs to (1 << enum KeeperAssistTarget). */
#define KEEPER_ASSIST_SET(target) ((uint8_t)(1u << (target)))

/** A creature that a rule could act on, as the world-reading code saw it. */
struct KeeperAssistCandidate {
    ThingIndex thing;
    uint8_t sets;                          /**< KEEPER_ASSIST_SET bits */
    int64_t score;                         /**< unscaled creature score (most_valuable) */
    int64_t facts[KAF_FirstDungeonFact];   /**< the target facts, by enum KeeperAssistFact (KAF_AlliesNear: see below) */
};

/** What only the engine can answer, for keeper_assist_decide(). */
struct KeeperAssistWorld {
    void *ctx;
    /** The rule's power can be cast at the candidate now: castable there, not already under its effect, ... */
    TbBool (*can_target)(void *ctx, const struct KeeperAssistRule *rule, const struct KeeperAssistCandidate *cand);
    /** The candidate's allies_near for that rule's power and level (the creatures of ours its area would hit). */
    int64_t (*allies_near)(void *ctx, const struct KeeperAssistRule *rule, const struct KeeperAssistCandidate *cand);
    TbBool (*power_available)(void *ctx, PowerKind power);
    int64_t (*price)(void *ctx, PowerKind power, int64_t level);
};

/** keeper_assist_decide()'s answer. slot < 0: nothing to do. cand < 0 with a slot: a power_off rule. */
struct KeeperAssistDecision {
    int slot;
    int cand;
    int64_t price;
};

/** The first rule, in slot order, that matches and may act now, and the target it picks. `dungeon_facts` is indexed
 *  by enum KeeperAssistFact (only the dungeon facts are read from it). `casts_allowed` false (a charged cast pending)
 *  lets only power_off rules act. Records in the state why a matching rule could not act (run[].last_refusal); changes
 *  nothing else. */
struct KeeperAssistDecision keeper_assist_decide(struct KeeperAssistState *st, const struct KeeperAssistWorld *world,
    const int64_t *dungeon_facts, const struct KeeperAssistCandidate *cands, int ncands, GameTurn now,
    TbBool casts_allowed);

/** Records a cast a rule made (counters, budget, the target's cooldown, the log). */
void keeper_assist_note_cast(struct KeeperAssistState *st, int slot, ThingIndex target, int64_t cost, GameTurn now);

/** The creatures the assistant would consider for `plyr_idx` right now: its own in battles and the enemies in them it can
 *  see (`want_fights`), and its other creatures (`want_any`). Fills up to KEEPER_ASSIST_MAX_CANDIDATES; returns how many.
 *  What process_keeper_assists() works from; public so the fog and concealment filters can be checked at one instant. */
int keeper_assist_candidates(PlayerNumber plyr_idx, TbBool want_fights, TbBool want_any, struct KeeperAssistCandidate *out);

/** The keeper's dungeon facts (gold, fights, heart health...), indexed by enum KeeperAssistFact. */
void keeper_assist_dungeon_facts(PlayerNumber plyr_idx, int64_t *facts);
/** A creature's target facts, as the assistant sees them (no battle facts: those come from its battle). */
void keeper_assist_fill_candidate(PlayerNumber plyr_idx, const struct Thing *thing, struct KeeperAssistCandidate *cand);
/** Whether every condition of `rule` holds for `cand` (NULL: dungeon facts only). */
TbBool keeper_assist_conds_hold(const struct KeeperAssistRule *rule, const int64_t *dungeon_facts, const struct KeeperAssistCandidate *cand);
/** Whether one of the keeper's live heal rules (a cast of Heal on its creatures) could cast now: the power researched and
 *  the price within its gold, reserve, the rule's budget and the pay day's cap. Reads only. lift_wounded waits for it. */
TbBool keeper_assist_heal_ready(PlayerNumber plyr_idx, const struct KeeperAssistState *st);

/** Once per game turn, from the game loop: every keeper's rules, every `interval` turns. Nothing in a multiplayer
 *  game, nothing for a keeper the built-in AI plays. */
void process_keeper_assists(void);

#ifdef __cplusplus
}
#endif
#endif
