#include "ftest_bug_pathing_pillar_circling.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "bflib_planar.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "player_instances.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// A hero tunneller digging towards player 0's dungeon heart on keeporig level 1, with a pillar in its way. Two
// sub-tests, one per action (the same action, given different variables):
//  1. a dirt pillar column standing on a hard floor slab, in the tunnel's line. A solid column on a floor slab has
//     the floor's flags, so the tunnelling collision took it for something to dig, found nothing to dig and left the
//     tunneller facing it until its stuck check pushed it away, 150 turns later (refactor pass 4, P4-F12);
//  2. the pillar in earth north of the dungeon. By the time the tunneller gets there the keeper's imps have walled
//     the dungeon in, and its only way in is a gold seam: tunnellers didn't dig gold, so the tunneller circled the
//     walls for ever (P4-F12: they dig gold now; going round a dungeon it can't get into is fine for a hero).
// Each passes when the tunneller gets to the heart within the turn limit, and its stuck check never had to push it.

// example of test variables wraped in a struct, this prevents variable name collisions with other tests, allowing you to name your variables how you like!
struct ftest_bug_pathing_pillar_circling__variables
{
    const MapSlabCoord slb_x_tunneler_start;
    const MapSlabCoord slb_y_tunneler_start;

    const MapSlabCoord slb_x_pillar;
    const MapSlabCoord slb_y_pillar;

    TbBool is_tunneler_setup;
    struct Thing* tunneler;

    unsigned char pillar_base_slab_type;

    GameTurnDelta turn_limit;
    uint64_t most_still_turns;
};
struct ftest_bug_pathing_pillar_circling__variables ftest_bug_pathing_pillar_circling__vars = {
    .slb_x_tunneler_start = 66,
    .slb_y_tunneler_start = 43,

    .slb_x_pillar = 57,
    .slb_y_pillar = 43,

    .is_tunneler_setup = false,
    .tunneler = NULL,

    .pillar_base_slab_type = SlbT_ROCK_FLOOR,

    .turn_limit = 1500,
    .most_still_turns = 0,
};
struct ftest_bug_pathing_pillar_circling__variables ftest_bug_pathing_pillar_circling__vars2 = {
    .slb_x_tunneler_start = 42,
    .slb_y_tunneler_start = 56,

    .slb_x_pillar = 42,
    .slb_y_pillar = 49,

    .is_tunneler_setup = false,
    .tunneler = NULL,

    .pillar_base_slab_type = SlbT_EARTH,

    .turn_limit = 3000,
    .most_still_turns = 0,
};



// forward declarations - tests
FTestActionResult ftest_bug_pathing_pillar_circling_action001__tunneler_dig_towards_pillar_test(struct FTestActionArgs* const args);

TbBool ftest_bug_pathing_pillar_circling_init()
{
    // this test will showcase multiple sub-tests, one sub-test per action
    // passing of variables to actions through void* (ftest_bug_pathing_pillar_circling__vars) allows some flexibility here
    ftest_bug_pathing_pillar_circling__vars.is_tunneler_setup = false;
    ftest_bug_pathing_pillar_circling__vars.most_still_turns = 0;
    ftest_bug_pathing_pillar_circling__vars2.is_tunneler_setup = false;
    ftest_bug_pathing_pillar_circling__vars2.most_still_turns = 0;

    ftest_append_action(ftest_bug_pathing_pillar_circling_action001__tunneler_dig_towards_pillar_test, 20, &ftest_bug_pathing_pillar_circling__vars);
    ftest_append_action(ftest_bug_pathing_pillar_circling_action001__tunneler_dig_towards_pillar_test, 20, &ftest_bug_pathing_pillar_circling__vars2);

    return true;
}

/**
 * @brief This action will be a sub-test, it will setup the situation of a single tunneler digging towards you, and check it gets to your heart past the pillar
 */
FTestActionResult ftest_bug_pathing_pillar_circling_action001__tunneler_dig_towards_pillar_test(struct FTestActionArgs* const args)
{
    // to make the test variable names shorter, use a pointer!
    // in this case we are grabbing the data from the argument, allowing different action setups!
    struct ftest_bug_pathing_pillar_circling__variables* const vars = args->data;

    ftest_util_reveal_map(PLAYER0);

    // example stage001 of using an argument variable to support multiple test stages inside of a single test action
    if(!vars->is_tunneler_setup)
    {
        // clear starting position for tunneler
        ftest_util_replace_slabs(vars->slb_x_tunneler_start, vars->slb_y_tunneler_start, vars->slb_x_tunneler_start, vars->slb_y_tunneler_start, SlbT_PATH, PLAYER_NEUTRAL);

        // place pillar/column in the way
        {
            ftest_util_replace_slab_columns(vars->slb_x_pillar, vars->slb_y_pillar, PLAYER_NEUTRAL, vars->pillar_base_slab_type, 26, 26, 26
                                                                                                                               , 26, 01, 26
                                                                                                                               , 26, 26, 26); // 01 - dirt pillar, 26 - path
        }

        struct Coord3d tunneler_pos;
        set_coords_to_slab_center(&tunneler_pos, vars->slb_x_tunneler_start, vars->slb_y_tunneler_start);

        // create tunneler
        vars->tunneler = create_owned_special_digger(tunneler_pos.x.val, tunneler_pos.y.val, PLAYER_GOOD);
        if(thing_is_invalid(vars->tunneler))
        {
            FTEST_FAIL_TEST("Failed to create tunneler");
            return FTRs_Go_To_Next_Action;
        }

        vars->is_tunneler_setup = true;
        return FTRs_Repeat_Current_Action;
    }

    // example stage002 of using an argument variable to support multiple test stages inside of a single test action
    if(!thing_exists(vars->tunneler) || !thing_is_creature(vars->tunneler))
    {
        FTEST_FAIL_TEST("Expected tunneler but it didn't exist?");
        return FTRs_Go_To_Next_Action;
    }

    // snap camera to tunneler
    ftest_util_move_camera_to_thing(vars->tunneler, PLAYER0);

    // the stuck check pushes a tunneller that has stood still, not digging, for 150 turns: it should never have to
    struct CreatureControl* cctrl = creature_control_get_from_thing(vars->tunneler);
    if (cctrl->party.tunnel_still_turns > vars->most_still_turns)
        vars->most_still_turns = cctrl->party.tunnel_still_turns;

    const struct Thing* heart = get_player_soul_container(PLAYER0);
    const GameTurnDelta turns = get_gameturn() - args->actual_started_at_game_turn;
    const TbBool at_heart = thing_exists(heart) && (get_chessboard_distance(&vars->tunneler->mappos, &heart->mappos) <= 2 * COORD_PER_SLB);
    if (!at_heart)
    {
        if (turns < vars->turn_limit)
            return FTRs_Repeat_Current_Action;
        FTEST_FAIL_TEST("The tunneller didn't get to the heart in %" PRId64 " turns: it is at slab (%" PRId64 ",%" PRId64 ")",
            (int64_t)vars->turn_limit, (int64_t)subtile_slab(vars->tunneler->mappos.x.stl.num), (int64_t)subtile_slab(vars->tunneler->mappos.y.stl.num));
        return FTRs_Go_To_Next_Action;
    }
    FTESTLOG("The tunneller got past the pillar at slab (%" PRId64 ",%" PRId64 ") to the heart in %" PRId64 " turns; it stood still at most %" PRIu64 " turns",
        (int64_t)vars->slb_x_pillar, (int64_t)vars->slb_y_pillar, (int64_t)turns, vars->most_still_turns);
    if (vars->most_still_turns >= 150)
    {
        FTEST_FAIL_TEST("The tunneller's stuck check had to push it (it stood still for %" PRIu64 " turns)", vars->most_still_turns);
        return FTRs_Go_To_Next_Action;
    }

    vars->tunneler = kill_creature(vars->tunneler, INVALID_THING, PLAYER0, CrDed_Default);
    if(thing_is_invalid(vars->tunneler))
    {
        FTEST_FAIL_TEST("Failed to cleanup tunneler on map");
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Go_To_Next_Action; //proceed to next test action
}

#ifdef __cplusplus
}
#endif

#endif
