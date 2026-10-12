/**
 * @file api_seat_combatstats.h
 * @brief Combat statistics for keepers played by a person or an agent (docs/newFeatures/llm/03 P0).
 *
 * The combat assistant (docs/newFeatures/llm/) is meant to react to fights faster than an agent that decides every few
 * seconds can. This records what that agent currently manages, so there is a baseline to compare against: how long a
 * creature had between dropping under 35% / 20% health and dying, whether a heal reached it, how long after a fight
 * began the first power was cast, how long after a decision was due the agent's orders arrived, and what Call to Arms
 * and Must Obey cost in upkeep.
 *
 * Read-only: it watches the simulation once per turn and through the power-cast and creature-death script ports, and
 * keeps everything in its own statics. Nothing in the simulation reads it, so it is not synced, saved or replayed. It
 * runs in a local game for every keeper not driven by the built-in AI (the local human and External seats); a load or
 * a new level starts it over. Results: COMBATSTAT: lines in the log, and get_combat_stats in the API.
 */
#ifndef DK_API_SEAT_COMBATSTATS_H
#define DK_API_SEAT_COMBATSTATS_H

#include "globals.h"
#include <json-dom.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Thing;

/** Health thresholds the reaction windows are measured from, in percent of max health. */
#define COMBATSTATS_LOW_PCT 35
#define COMBATSTATS_CRITICAL_PCT 20
/** A creature counts as recovered (a new low episode can start) once back at this percentage. */
#define COMBATSTATS_RECOVERED_PCT 50
/** A death within this many turns of the creature's last fighting turn is a combat death. */
#define COMBATSTATS_COMBAT_DEATH_TURNS 60
/** A fight ends after this many turns with none of the keeper's creatures fighting. */
#define COMBATSTATS_FIGHT_GAP_TURNS 100

/** One creature's low-health episode: when it first fell under each threshold, and the first heal after that. */
struct CombatLowTrack {
    int64_t low_since;      /**< turn it fell under COMBATSTATS_LOW_PCT in this episode, 0 = not low */
    int64_t critical_since; /**< turn it fell under COMBATSTATS_CRITICAL_PCT, 0 = not */
    int64_t healed_turn;    /**< first heal cast on it during the episode, 0 = none */
};

/** Advances a low-health episode by one observation (pure; tested on its own). A creature at or above
 *  COMBATSTATS_RECOVERED_PCT ends its episode. */
void combatstats_update_low(struct CombatLowTrack *t, int64_t health_pct, int64_t turn);
/** Median of `n` values (sorted in place); 0 when n is 0. For an even count, the lower middle value. */
int64_t combatstats_median(int64_t *values, int64_t n);
/** Whether a lasting power charges its upkeep on a turn `turns_since_start` after it was cast, for a power with that
 *  `duration` (process_magic_power_call_to_arms / _must_obey: every `duration` turns, every turn if the duration is
 *  under 1). Never on the turn it was cast: measured (ftest combat_stats), the engine takes only the cast's price then. */
TbBool combatstats_upkeep_due(int64_t turns_since_start, int64_t duration);

/** Once per game turn from api_update_server(). */
void api_seat_combatstats_tick(void);
/** A power was cast (the lua_on_power_cast port, before it takes effect). */
void api_seat_combatstats_note_cast(PlayerNumber plyr_idx, PowerKind pwkind, int64_t level, struct Thing *target);
/** A creature is dying (the lua_on_creature_death port; the thing is still intact). */
void api_seat_combatstats_note_death(struct Thing *thing);
/** The agent's client submitted an order for this seat (a real submit, not a dry run). */
void api_seat_combatstats_note_order(PlayerNumber plyr_idx);
/** The statistics for one keeper, as get_combat_stats returns them. False if the player is not tracked. */
TbBool api_seat_combatstats_build(VALUE *out, PlayerNumber plyr_idx);
/** Forgets everything (tests). */
void api_seat_combatstats_reset(void);

#ifdef __cplusplus
}
#endif

#endif
