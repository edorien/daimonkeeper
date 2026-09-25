// Coverage-driven addition (see ftest_creature_combat_power_hand.c and
// docs/Architecture/testing-harness.md §7.7): creature_states_pray.c was
// at 0.0% line coverage before this -- no unit test nor any other
// registered ftest ever reaches the temple/prayer state machine
// (at_temple/praying_in_temple/process_temple_function). Builds a TEMPLE
// room next to the player's dungeon heart on map00011 ("Hearth"),
// assigns a creature the TEMPLE_PRAY job, and waits for the real AI to
// walk it there and enter CrSt_AtTemple/CrSt_PrayingInTemple.
#include "ftest_creature_temple_prayer.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config.h"
#include "config_creature.h"
#include "player_instances.h"
#include "dungeon_data.h"
#include "creature_jobs.h"
#include "config_terrain.h"
#include "creature_states.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// TEMPLE needs a real minimum footprint to register as a usable room
// (like every other DK room) -- 4x4 slabs is comfortably above that,
// matching this directory's other room-building tests' margin
// (ftest_util_action__create_and_fill_torture_room uses a caller-supplied
// size with no hard-coded minimum check of its own).
#define TEMPLE_SIZE 4
// Spawn/build a fixed offset from the dungeon heart, not touching it
// directly -- mirrors ftest_creature_combat_power_hand.c's fix for why
// carving fresh ground at an arbitrary, disconnected map location doesn't
// reliably resolve to the requested owner/kind.
#define TEMPLE_OFFSET_SLB 3

struct ftest_creature_temple_prayer__variables
{
    ThingIndex creature_idx;
};
struct ftest_creature_temple_prayer__variables ftest_creature_temple_prayer__vars = {
    .creature_idx = 0,
};

FTestActionResult ftest_creature_temple_prayer_action001__setup(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_temple_prayer_action002__wait_for_prayer(struct FTestActionArgs* const args);

TbBool ftest_creature_temple_prayer_init()
{
    ftest_append_action(ftest_creature_temple_prayer_action001__setup, 0, &ftest_creature_temple_prayer__vars);
    ftest_append_action(ftest_creature_temple_prayer_action002__wait_for_prayer, 10, &ftest_creature_temple_prayer__vars);

    return true;
}

FTestActionResult ftest_creature_temple_prayer_action001__setup(struct FTestActionArgs* const args)
{
    struct ftest_creature_temple_prayer__variables* const vars = args->data;

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

    // Probed empirically (see docs/Architecture/testing-harness.md §7.7):
    // the heart's own 3-wide room occupies roughly [-1,+1], CLAIMED floor
    // extends to about +-2, and anything past that is solid wall on this
    // level -- carving a room needs to start adjacent to already-CLAIMED
    // ground (an arbitrary disconnected location silently resolves to the
    // wrong owner/kind, per ftest_creature_combat_power_hand.c's own fix),
    // and a creature can only be *spawned* on ground that's already open,
    // not on ground a room-carve elsewhere hasn't touched.
    MapSlabCoord temple_slb_x = heart_slb_x + TEMPLE_OFFSET_SLB;
    MapSlabCoord temple_slb_y = heart_slb_y;

    if (!ftest_util_replace_slabs(temple_slb_x, temple_slb_y, temple_slb_x + TEMPLE_SIZE, temple_slb_y + TEMPLE_SIZE, SlbT_TEMPLE, PLAYER0))
    {
        FTEST_FAIL_TEST("Failed to build temple room at slab (%" PRId64 ",%" PRId64 ")", (int64_t)(temple_slb_x), (int64_t)(temple_slb_y));
        return FTRs_Go_To_Next_Action;
    }

    // map00011's own script leaves TEMPLE only researchable, not yet
    // buildable (ROOM_AVAILABLE(ALL_PLAYERS,TEMPLE,1,0)) -- carving the
    // room directly above bypasses the UI's "can the player place this"
    // gate, but creature_can_do_job_for_player()'s job-eligibility check
    // is separate and still respects can_build, so a creature would never
    // actually be allowed to use a room the level considers unresearched.
    // Force it available for real job assignment to work.
    set_room_available(PLAYER0, RoK_TEMPLE, 1, 1);

    ThingModel creature_model = (ThingModel)creature_model_id("ORC");
    if (creature_model < 1)
    {
        FTEST_FAIL_TEST("Failed to resolve creature model id (ORC)");
        return FTRs_Go_To_Next_Action;
    }
    // Spawn on already-CLAIMED ground on the *opposite* side of the heart
    // from the temple (confirmed open via the probe above) -- not
    // symmetric with TEMPLE_OFFSET_SLB, which lands in solid wall on this
    // side (the temple carve above is what turns its own side into real
    // floor; this side was never touched).
    struct Coord3d spawn_pos;
    set_coords_to_slab_center(&spawn_pos, heart_slb_x - 2, heart_slb_y);

    struct Thing* creature = ftest_util_create_creature(spawn_pos.x.val, spawn_pos.y.val, PLAYER0, 9, creature_model);
    if (thing_is_invalid(creature))
    {
        FTEST_FAIL_TEST("Failed to create creature (ORC)");
        return FTRs_Go_To_Next_Action;
    }

    CreatureJob temple_pray_job = (CreatureJob)get_id(creaturejob_desc, "TEMPLE_PRAY");
    if ((int64_t)temple_pray_job == -1)
    {
        FTEST_FAIL_TEST("Failed to resolve TEMPLE_PRAY job id");
        return FTRs_Go_To_Next_Action;
    }
    if (!set_creature_assigned_job(creature, temple_pray_job))
    {
        FTEST_FAIL_TEST("Failed to assign TEMPLE_PRAY job to creature");
        return FTRs_Go_To_Next_Action;
    }
    // The opportunistic "should I go pray" check (creature_states.c) only
    // runs from the generic idle state -- a freshly created creature can
    // start in CrSt_MoveToPosition (its own post-spawn settling) instead,
    // which never reaches that check on its own. Force idle so the job
    // just assigned actually gets picked up.
    if (!internal_set_thing_state(creature, CrSt_CreatureDoingNothing))
    {
        FTEST_FAIL_TEST("Failed to set creature to CrSt_CreatureDoingNothing");
        return FTRs_Go_To_Next_Action;
    }

    vars->creature_idx = creature->index;

    ftest_util_move_camera_to_thing(heart, PLAYER0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_temple_prayer_action002__wait_for_prayer(struct FTestActionArgs* const args)
{
    struct ftest_creature_temple_prayer__variables* const vars = args->data;

    struct Thing* creature = thing_get(vars->creature_idx);
    if (thing_is_invalid(creature) || !thing_is_creature(creature))
    {
        FTEST_FAIL_TEST("Creature is no longer present while waiting for it to pray");
        return FTRs_Go_To_Next_Action;
    }

    if (creature->active_state == CrSt_AtTemple || creature->active_state == CrSt_PrayingInTemple)
    {
        FTESTLOG("Creature reached temple state %" PRId64 " at turn %" PRId64, (int64_t)creature->active_state, (int64_t)(get_gameturn()));
        return FTRs_Go_To_Next_Action;
    }

    // Needs a much larger budget than combat resolution
    // (ftest_creature_combat_power_hand.c: ~90 turns): the opportunistic
    // "should I go pray" check (creature_states.c's anger_process_
    // creature_anger) only re-fires every 128 turns per creature
    // (cctrl->temple_pray_check_turn), and the first one or two attempts
    // may not land while the creature is mid-wander -- confirmed
    // empirically (passed at turn 356 against a 300-turn budget failing
    // and an 800-turn budget succeeding).
    if (get_gameturn() >= args->intended_start_at_game_turn + 600)
    {
        FTEST_FAIL_TEST("Creature never reached CrSt_AtTemple/CrSt_PrayingInTemple within the turn budget (active_state=%" PRId64 ")", (int64_t)creature->active_state);
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Repeat_Current_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
