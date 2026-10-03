// Coverage-driven addition, same family as ftest_creature_prison_capture.c
// (see docs/Architecture/testing-harness.md §7.7). Targets the "room
// owner vs. creature owner differ" branch in creature_states_tortr.c's
// process_torture_function() (the per-tick handler for CrSt_Torturing,
// the PAINFUL_TORTURE job's continue state): a creature tortured in a
// room it owns itself just idles harmlessly forever (early return before
// any torture-point accumulation), while a creature tortured in an
// *enemy's* room actually accumulates torture points every tick, which
// eventually leads to it being converted, revealing map information, or
// dying. Real DK gameplay reaches PAINFUL_TORTURE two ways: dropping an
// enemy creature into a torture room (generic HUMAN_DROP job-assignment,
// creature_being_dropped()/get_job_for_subtile() in creature_states.c --
// unlike Prison, controlled_creature_drop_thing() has no dedicated
// RoRoF_Torture branch), or a creature without KINKY_TORTURE as a
// primary/secondary job preference being sent there for "painful torture
// only" per config/fxdata/creature.cfg job18's own comment -- which
// applies to owned creatures too, meaning PAINFUL_TORTURE's ownership
// branch is exactly the same->different contrast this test wants,
// without needing a second, differently-configured job.
//
// Rather than simulate a full hand-drop or wait out the job-assignment
// dispatch/cooldown machinery, this test calls the room-entry state
// handler (at_torture_room()) and the per-tick handler
// (process_torture_function()) directly on two creatures already
// standing in the room -- one owned by the room's owner, one not --
// mirroring the direct-call approach ftest_creature_prison_capture.c
// settled on. This is deterministic and avoids relying on
// process_torture_function()'s later RNG-driven break/convert/death
// rolls (which only trigger once accumulated torture points cross
// crconf->torture_break_time, typically hundreds of turns away): a
// single call's accumulated_torture_points delta alone already proves
// which branch ran.
#include "ftest_creature_torture_ownership.h"
#include "player_availability.h"
#include "creature_jobs.h"

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
#include "creature_states_tortr.h"
#include "creature_control.h"
#include "room_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// User-specified size: a real 3x3 room, matching
// ftest_creature_prison_capture.c's PRISON_SIZE.
#define TORTURE_OFFSET_SLB 3
#define TORTURE_SIZE 3

struct ftest_creature_torture_ownership__variables
{
    ThingIndex own_creature_idx;
    ThingIndex enemy_creature_idx;
};
struct ftest_creature_torture_ownership__variables ftest_creature_torture_ownership__vars = {
    .own_creature_idx = 0,
    .enemy_creature_idx = 0,
};

FTestActionResult ftest_creature_torture_ownership_action001__setup(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_torture_ownership_action002__enter_torture(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_torture_ownership_action003__verify(struct FTestActionArgs* const args);

TbBool ftest_creature_torture_ownership_init()
{
    ftest_append_action(ftest_creature_torture_ownership_action001__setup, 0, &ftest_creature_torture_ownership__vars);
    ftest_append_action(ftest_creature_torture_ownership_action002__enter_torture, 5, &ftest_creature_torture_ownership__vars);
    ftest_append_action(ftest_creature_torture_ownership_action003__verify, 1, &ftest_creature_torture_ownership__vars);

    return true;
}

FTestActionResult ftest_creature_torture_ownership_action001__setup(struct FTestActionArgs* const args)
{
    struct ftest_creature_torture_ownership__variables* const vars = args->data;

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

    MapSlabCoord room_slb_x = heart_slb_x + TORTURE_OFFSET_SLB;
    MapSlabCoord room_slb_y = heart_slb_y;

    if (!ftest_util_replace_slabs(room_slb_x, room_slb_y, room_slb_x + TORTURE_SIZE, room_slb_y + TORTURE_SIZE, SlbT_TORTURE, PLAYER0))
    {
        FTEST_FAIL_TEST("Failed to build torture room at slab (%" PRId64 ",%" PRId64 ")", (int64_t)(room_slb_x), (int64_t)(room_slb_y));
        return FTRs_Go_To_Next_Action;
    }
    // map00011's script leaves TORTURE researchable but not buildable
    // (ROOM_AVAILABLE(ALL_PLAYERS,TORTURE,1,0)) -- same wrinkle
    // ftest_creature_prison_capture.c found for PRISON.
    set_room_available(PLAYER0, RoK_TORTURE, 1, 1);

    // Centre of the 3x3 room (offset+1 lands on its middle slab).
    MapSlabCoord room_centre_slb_x = room_slb_x + 1;
    MapSlabCoord room_centre_slb_y = room_slb_y + 1;
    struct Coord3d room_centre_pos;
    set_coords_to_slab_center(&room_centre_pos, room_centre_slb_x, room_centre_slb_y);

    ThingModel orc_model = (ThingModel)creature_model_id("ORC");
    if (orc_model < 1)
    {
        FTEST_FAIL_TEST("Failed to resolve creature model id (ORC)");
        return FTRs_Go_To_Next_Action;
    }
    // Owned by PLAYER0, same as the room -- exercises the
    // room->owner == creatng->owner branch (self-torture, harmless).
    struct Thing* own_creature = ftest_util_create_creature(room_centre_pos.x.val, room_centre_pos.y.val, PLAYER0, 1, orc_model);
    if (thing_is_invalid(own_creature))
    {
        FTEST_FAIL_TEST("Failed to create own creature (ORC) in the torture room");
        return FTRs_Go_To_Next_Action;
    }
    vars->own_creature_idx = own_creature->index;

    ThingModel creature_model = (ThingModel)creature_model_id("BARBARIAN");
    if (creature_model < 1)
    {
        FTEST_FAIL_TEST("Failed to resolve creature model id (BARBARIAN)");
        return FTRs_Go_To_Next_Action;
    }
    // Owned by PLAYER_GOOD (the hero side) -- exercises the
    // room->owner != creatng->owner branch (torture actually progresses).
    struct Thing* enemy_creature = ftest_util_create_creature(room_centre_pos.x.val, room_centre_pos.y.val, PLAYER_GOOD, 1, creature_model);
    if (thing_is_invalid(enemy_creature))
    {
        FTEST_FAIL_TEST("Failed to create enemy creature (BARBARIAN) in the torture room");
        return FTRs_Go_To_Next_Action;
    }
    vars->enemy_creature_idx = enemy_creature->index;

    ftest_util_move_camera_to_thing(heart, PLAYER0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_torture_ownership_action002__enter_torture(struct FTestActionArgs* const args)
{
    struct ftest_creature_torture_ownership__variables* const vars = args->data;

    struct Thing* own_creature = thing_get(vars->own_creature_idx);
    struct Thing* enemy_creature = thing_get(vars->enemy_creature_idx);
    if (thing_is_invalid(own_creature) || !thing_is_creature(own_creature) ||
        thing_is_invalid(enemy_creature) || !thing_is_creature(enemy_creature))
    {
        FTEST_FAIL_TEST("A creature is no longer present before entering torture");
        return FTRs_Go_To_Next_Action;
    }

    // Both creatures are already standing in the room (positioned at its
    // centre in setup), so calling the room-entry state handler directly
    // is equivalent to it firing via the real dispatch after being sent
    // there by a job assignment or a hand drop -- see this file's header
    // comment for why neither of those simulations was used here.
    if (!at_torture_room(own_creature))
    {
        FTEST_FAIL_TEST("at_torture_room() failed for the PLAYER0-owned creature in its own torture room");
        return FTRs_Go_To_Next_Action;
    }
    if (!at_torture_room(enemy_creature))
    {
        FTEST_FAIL_TEST("at_torture_room() failed for the PLAYER_GOOD-owned creature in PLAYER0's torture room");
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_torture_ownership_action003__verify(struct FTestActionArgs* const args)
{
    struct ftest_creature_torture_ownership__variables* const vars = args->data;

    struct Thing* own_creature = thing_get(vars->own_creature_idx);
    struct Thing* enemy_creature = thing_get(vars->enemy_creature_idx);
    if (thing_is_invalid(own_creature) || !thing_is_creature(own_creature) ||
        thing_is_invalid(enemy_creature) || !thing_is_creature(enemy_creature))
    {
        FTEST_FAIL_TEST("A creature is no longer present after entering torture");
        return FTRs_Go_To_Next_Action;
    }

    struct CreatureControl* own_cctrl = creature_control_get_from_thing(own_creature);
    struct CreatureControl* enemy_cctrl = creature_control_get_from_thing(enemy_creature);
    if (own_cctrl->work_room_id == 0 || enemy_cctrl->work_room_id == 0)
    {
        FTEST_FAIL_TEST("A creature was not added to the torture room's work list (own work_room_id=%" PRId64 ", enemy work_room_id=%" PRId64 ")",
            (int64_t)own_cctrl->work_room_id, (int64_t)enemy_cctrl->work_room_id);
        return FTRs_Go_To_Next_Action;
    }

    // at_torture_room() resets accumulated_torture_points to 0 for both.
    int64_t own_points_before = own_cctrl->tortured.accumulated_torture_points;
    int64_t enemy_points_before = enemy_cctrl->tortured.accumulated_torture_points;

    process_torture_function(own_creature);
    process_torture_function(enemy_creature);

    int64_t own_points_after = own_cctrl->tortured.accumulated_torture_points;
    int64_t enemy_points_after = enemy_cctrl->tortured.accumulated_torture_points;

    // This is the ownership-differs behaviour itself: process_torture_
    // function() returns early (before update_torture_points()) when
    // room->owner == creatng->owner, so a same-owner "torture victim" is
    // really just standing there harmlessly.
    if (own_points_after != own_points_before)
    {
        FTEST_FAIL_TEST("PLAYER0-owned creature's torture points changed in its own room (before=%" PRId64 ", after=%" PRId64 ") -- expected the same-owner early return to skip update_torture_points()",
            (int64_t)(own_points_before), (int64_t)(own_points_after));
        return FTRs_Go_To_Next_Action;
    }
    // The enemy-owned creature falls through to update_torture_points(),
    // which unconditionally adds a positive amount (TORTURE_ACCUM_FAC *
    // room->efficiency, both always > 0 for a real, available room).
    if (enemy_points_after <= enemy_points_before)
    {
        FTEST_FAIL_TEST("PLAYER_GOOD-owned creature's torture points did not increase in PLAYER0's room (before=%" PRId64 ", after=%" PRId64 ") -- expected update_torture_points() to have run",
            (int64_t)(enemy_points_before), (int64_t)(enemy_points_after));
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("Ownership-differs branch confirmed at turn %" PRId64 ": own-room victim's torture points stayed at %" PRId64 ", enemy victim's rose from %" PRId64 " to %" PRId64,
        (int64_t)(get_gameturn()), (int64_t)(own_points_after), (int64_t)(enemy_points_before), (int64_t)(enemy_points_after));
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
