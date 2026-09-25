// Coverage-driven addition, same family as ftest_creature_temple_prayer.c
// (see docs/Architecture/testing-harness.md §7.7): creature_states_lair.c
// exercises the "hurt creature heals by sleeping in its lair" flow.
// Unlike temple prayer, this doesn't need a job assignment at all --
// creature_requires_healing() (creature_states_lair.c) gates purely on
// thing->health vs a config threshold, so damaging the creature directly
// forces the need, the same way ftest_creature_combat_power_hand.c nerfs
// an enemy's health to force a fast, deterministic outcome.
#include "ftest_creature_lair_healing.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "config_terrain.h"
#include "player_instances.h"
#include "dungeon_data.h"
#include "creature_states.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// See ftest_creature_temple_prayer.c's comment for why these offsets/
// sizes are what they are (probed empirically against a real install --
// CLAIMED floor only extends to about +-2 slabs from the heart's own
// room on this level, and a room carve needs to start adjacent to
// already-owned ground).
#define LAIR_OFFSET_SLB 3
#define LAIR_SIZE 4

struct ftest_creature_lair_healing__variables
{
    ThingIndex creature_idx;
};
struct ftest_creature_lair_healing__variables ftest_creature_lair_healing__vars = {
    .creature_idx = 0,
};

FTestActionResult ftest_creature_lair_healing_action001__setup(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_lair_healing_action002__wait_for_sleep(struct FTestActionArgs* const args);

TbBool ftest_creature_lair_healing_init()
{
    ftest_append_action(ftest_creature_lair_healing_action001__setup, 0, &ftest_creature_lair_healing__vars);
    ftest_append_action(ftest_creature_lair_healing_action002__wait_for_sleep, 10, &ftest_creature_lair_healing__vars);

    return true;
}

FTestActionResult ftest_creature_lair_healing_action001__setup(struct FTestActionArgs* const args)
{
    struct ftest_creature_lair_healing__variables* const vars = args->data;

    ftest_util_reveal_map(PLAYER0);

    struct Dungeon* dungeon = get_players_dungeon(get_player(PLAYER0));
    struct Thing* heart = thing_get(dungeon->dnheart_idx);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Failed to find PLAYER0's dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    MapSlabCoord heart_slb_x = subtile_slab(heart->mappos.x.stl.num);
    MapSlabCoord heart_slb_y = subtile_slab(heart->mappos.y.stl.num);

    MapSlabCoord lair_slb_x = heart_slb_x + LAIR_OFFSET_SLB;
    MapSlabCoord lair_slb_y = heart_slb_y;

    if (!ftest_util_replace_slabs(lair_slb_x, lair_slb_y, lair_slb_x + LAIR_SIZE, lair_slb_y + LAIR_SIZE, SlbT_LAIR, PLAYER0))
    {
        FTEST_FAIL_TEST("Failed to build lair room at slab (%" PRId64 ",%" PRId64 ")", (int64_t)(lair_slb_x), (int64_t)(lair_slb_y));
        return FTRs_Go_To_Next_Action;
    }
    // map00011's own script already has LAIR fully available
    // (ROOM_AVAILABLE(ALL_PLAYERS,LAIR,1,1)), unlike TEMPLE -- set
    // defensively anyway so this test doesn't silently depend on that.
    set_room_available(PLAYER0, RoK_LAIR, 1, 1);

    ThingModel creature_model = (ThingModel)creature_model_id("ORC");
    if (creature_model < 1)
    {
        FTEST_FAIL_TEST("Failed to resolve creature model id (ORC)");
        return FTRs_Go_To_Next_Action;
    }
    struct Coord3d spawn_pos;
    set_coords_to_slab_center(&spawn_pos, heart_slb_x - 2, heart_slb_y);

    struct Thing* creature = ftest_util_create_creature(spawn_pos.x.val, spawn_pos.y.val, PLAYER0, 9, creature_model);
    if (thing_is_invalid(creature))
    {
        FTEST_FAIL_TEST("Failed to create creature (ORC)");
        return FTRs_Go_To_Next_Action;
    }
    // Force the "needs healing" condition directly
    // (creature_requires_healing(): thing->health <= heal_requirement *
    // max_health / 256) rather than waiting for real combat damage.
    creature->health = 1;

    if (!internal_set_thing_state(creature, CrSt_CreatureDoingNothing))
    {
        FTEST_FAIL_TEST("Failed to set creature to CrSt_CreatureDoingNothing");
        return FTRs_Go_To_Next_Action;
    }

    vars->creature_idx = creature->index;

    ftest_util_move_camera_to_thing(heart, PLAYER0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_lair_healing_action002__wait_for_sleep(struct FTestActionArgs* const args)
{
    struct ftest_creature_lair_healing__variables* const vars = args->data;

    struct Thing* creature = thing_get(vars->creature_idx);
    if (thing_is_invalid(creature) || !thing_is_creature(creature))
    {
        FTEST_FAIL_TEST("Creature is no longer present while waiting for it to sleep");
        return FTRs_Go_To_Next_Action;
    }

    if (creature->active_state == CrSt_CreatureSleep || creature->active_state == CrSt_AtLairToSleep)
    {
        FTESTLOG("Creature reached lair-sleep state %" PRId64 " at turn %" PRId64, (int64_t)creature->active_state, (int64_t)(get_gameturn()));
        return FTRs_Go_To_Next_Action;
    }

    // Same turn-budget rationale as ftest_creature_temple_prayer.c: the
    // opportunistic healing-sleep check (process_creature_needs_to_heal,
    // creature_states.c) only re-fires every 128 turns per creature.
    if (get_gameturn() >= args->intended_start_at_game_turn + 600)
    {
        FTEST_FAIL_TEST("Creature never reached CrSt_CreatureSleep/CrSt_AtLairToSleep within the turn budget (active_state=%" PRId64 ")", (int64_t)creature->active_state);
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Repeat_Current_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
