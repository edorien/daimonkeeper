// Coverage-driven addition, same family as ftest_creature_training.c/
// ftest_creature_guard_post.c (see docs/Architecture/testing-harness.md
// §7.7): creature_states_barck.c was at 0.0% line coverage. Same general
// job_assigned dispatch as TRAIN/GUARD (creature_doing_nothing(),
// creature_states.c), and map00011's script already leaves BARRACKS
// fully available (ROOM_AVAILABLE(ALL_PLAYERS,BARRACKS,1,1)).
#include "ftest_creature_barracks.h"
#include "player_availability.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config.h"
#include "config_creature.h"
#include "config_terrain.h"
#include "player_instances.h"
#include "dungeon_data.h"
#include "creature_jobs.h"
#include "creature_states.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// See ftest_creature_temple_prayer.c's comment for why these offsets/
// sizes are what they are.
#define BARRACKS_OFFSET_SLB 3
#define BARRACKS_SIZE 4

struct ftest_creature_barracks__variables
{
    ThingIndex creature_idx;
};
struct ftest_creature_barracks__variables ftest_creature_barracks__vars = {
    .creature_idx = 0,
};

FTestActionResult ftest_creature_barracks_action001__setup(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_barracks_action002__wait_for_barracking(struct FTestActionArgs* const args);

TbBool ftest_creature_barracks_init()
{
    ftest_append_action(ftest_creature_barracks_action001__setup, 0, &ftest_creature_barracks__vars);
    ftest_append_action(ftest_creature_barracks_action002__wait_for_barracking, 10, &ftest_creature_barracks__vars);

    return true;
}

FTestActionResult ftest_creature_barracks_action001__setup(struct FTestActionArgs* const args)
{
    struct ftest_creature_barracks__variables* const vars = args->data;

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

    MapSlabCoord room_slb_x = heart_slb_x + BARRACKS_OFFSET_SLB;
    MapSlabCoord room_slb_y = heart_slb_y;

    if (!ftest_util_replace_slabs(room_slb_x, room_slb_y, room_slb_x + BARRACKS_SIZE, room_slb_y + BARRACKS_SIZE, SlbT_BARRACKS, PLAYER0))
    {
        FTEST_FAIL_TEST("Failed to build barracks room at slab (%" PRId64 ",%" PRId64 ")", (int64_t)(room_slb_x), (int64_t)(room_slb_y));
        return FTRs_Go_To_Next_Action;
    }
    set_room_available(PLAYER0, RoK_BARRACKS, 1, 1);

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

    CreatureJob barrack_job = (CreatureJob)get_id(creaturejob_desc, "BARRACK");
    if ((int64_t)barrack_job == -1)
    {
        FTEST_FAIL_TEST("Failed to resolve BARRACK job id");
        return FTRs_Go_To_Next_Action;
    }
    if (!set_creature_assigned_job(creature, barrack_job))
    {
        FTEST_FAIL_TEST("Failed to assign BARRACK job to creature");
        return FTRs_Go_To_Next_Action;
    }
    if (!internal_set_thing_state(creature, CrSt_CreatureDoingNothing))
    {
        FTEST_FAIL_TEST("Failed to set creature to CrSt_CreatureDoingNothing");
        return FTRs_Go_To_Next_Action;
    }

    vars->creature_idx = creature->index;

    ftest_util_move_camera_to_thing(heart, PLAYER0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_barracks_action002__wait_for_barracking(struct FTestActionArgs* const args)
{
    struct ftest_creature_barracks__variables* const vars = args->data;

    struct Thing* creature = thing_get(vars->creature_idx);
    if (thing_is_invalid(creature) || !thing_is_creature(creature))
    {
        FTEST_FAIL_TEST("Creature is no longer present while waiting for it to barrack");
        return FTRs_Go_To_Next_Action;
    }

    if (creature->active_state == CrSt_AtBarrackRoom || creature->active_state == CrSt_Barracking)
    {
        FTESTLOG("Creature reached barrack state %" PRId64 " at turn %" PRId64, (int64_t)creature->active_state, (int64_t)(get_gameturn()));
        return FTRs_Go_To_Next_Action;
    }

    // Same turn-budget rationale as ftest_creature_temple_prayer.c: the
    // opportunistic job_assigned check (creature_doing_nothing(),
    // creature_states.c) only re-fires every 128 turns per creature.
    if (get_gameturn() >= args->intended_start_at_game_turn + 600)
    {
        FTEST_FAIL_TEST("Creature never reached CrSt_AtBarrackRoom/CrSt_Barracking within the turn budget (active_state=%" PRId64 ")", (int64_t)creature->active_state);
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Repeat_Current_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
