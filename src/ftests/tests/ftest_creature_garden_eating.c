// Coverage-driven addition, same family as ftest_creature_temple_prayer.c/
// ftest_creature_lair_healing.c (see docs/Architecture/testing-harness.md
// §7.7): exercises room_garden.c/the CrSt_CreatureToGarden/
// CrSt_CreatureEatingAtGarden hunger flow. Unlike lair (health-gated) and
// temple (anger-gated), process_creature_needs_to_eat() (creature_states.c)
// additionally requires actual food capacity in a real garden room --
// room_create_new_food_at() (room_garden.c) creates it directly rather
// than waiting for a fresh room's natural growth.
#include "ftest_creature_garden_eating.h"

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
#include "creature_control.h"
#include "room_data.h"
#include "room_garden.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// See ftest_creature_temple_prayer.c's comment for why these offsets/
// sizes are what they are.
#define GARDEN_OFFSET_SLB 3
#define GARDEN_SIZE 4

struct ftest_creature_garden_eating__variables
{
    ThingIndex creature_idx;
};
struct ftest_creature_garden_eating__variables ftest_creature_garden_eating__vars = {
    .creature_idx = 0,
};

FTestActionResult ftest_creature_garden_eating_action001__setup(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_garden_eating_action002__wait_for_eating(struct FTestActionArgs* const args);

TbBool ftest_creature_garden_eating_init()
{
    ftest_append_action(ftest_creature_garden_eating_action001__setup, 0, &ftest_creature_garden_eating__vars);
    ftest_append_action(ftest_creature_garden_eating_action002__wait_for_eating, 10, &ftest_creature_garden_eating__vars);

    return true;
}

FTestActionResult ftest_creature_garden_eating_action001__setup(struct FTestActionArgs* const args)
{
    struct ftest_creature_garden_eating__variables* const vars = args->data;

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

    MapSlabCoord garden_slb_x = heart_slb_x + GARDEN_OFFSET_SLB;
    MapSlabCoord garden_slb_y = heart_slb_y;

    if (!ftest_util_replace_slabs(garden_slb_x, garden_slb_y, garden_slb_x + GARDEN_SIZE, garden_slb_y + GARDEN_SIZE, SlbT_GARDEN, PLAYER0))
    {
        FTEST_FAIL_TEST("Failed to build garden room at slab (%" PRId64 ",%" PRId64 ")", (int64_t)(garden_slb_x), (int64_t)(garden_slb_y));
        return FTRs_Go_To_Next_Action;
    }
    set_room_available(PLAYER0, RoK_GARDEN, 1, 1);

    struct Room* garden_room = slab_room_get(garden_slb_x, garden_slb_y);
    if (room_is_invalid(garden_room))
    {
        FTEST_FAIL_TEST("Failed to find the just-built garden room");
        return FTRs_Go_To_Next_Action;
    }
    // A fresh room has zero food capacity -- real gameplay grows it over
    // time (room_grow_food(), room_garden.c); create some directly so
    // process_creature_needs_to_eat()'s
    // find_nearest_room_of_role_for_thing_with_used_capacity() check has
    // something to find immediately.
    MapSubtlCoord food_stl_x = slab_subtile_center(garden_slb_x + 1);
    MapSubtlCoord food_stl_y = slab_subtile_center(garden_slb_y + 1);
    if (!room_create_new_food_at(garden_room, food_stl_x, food_stl_y))
    {
        FTEST_FAIL_TEST("Failed to create food in the garden room");
        return FTRs_Go_To_Next_Action;
    }
    count_food_in_room(garden_room);

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
    // Force the "needs to eat" condition directly
    // (process_creature_needs_to_eat(): hunger_level > crconf->hunger_rate)
    // rather than waiting for hunger to accumulate naturally.
    struct CreatureControl* cctrl = creature_control_get_from_thing(creature);
    cctrl->hunger_level = 20000;

    if (!internal_set_thing_state(creature, CrSt_CreatureDoingNothing))
    {
        FTEST_FAIL_TEST("Failed to set creature to CrSt_CreatureDoingNothing");
        return FTRs_Go_To_Next_Action;
    }

    vars->creature_idx = creature->index;

    ftest_util_move_camera_to_thing(heart, PLAYER0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_garden_eating_action002__wait_for_eating(struct FTestActionArgs* const args)
{
    struct ftest_creature_garden_eating__variables* const vars = args->data;

    struct Thing* creature = thing_get(vars->creature_idx);
    if (thing_is_invalid(creature) || !thing_is_creature(creature))
    {
        FTEST_FAIL_TEST("Creature is no longer present while waiting for it to eat");
        return FTRs_Go_To_Next_Action;
    }

    if (creature->active_state == CrSt_CreatureToGarden
        || creature->active_state == CrSt_CreatureArrivedAtGarden
        || creature->active_state == CrSt_CreatureEatingAtGarden)
    {
        FTESTLOG("Creature reached garden-eating state %" PRId64 " at turn %" PRId64, (int64_t)creature->active_state, (int64_t)(get_gameturn()));
        return FTRs_Go_To_Next_Action;
    }

    // Same turn-budget rationale as ftest_creature_temple_prayer.c/
    // ftest_creature_lair_healing.c: the opportunistic need check only
    // re-fires every 128 turns per creature.
    if (get_gameturn() >= args->intended_start_at_game_turn + 600)
    {
        FTEST_FAIL_TEST("Creature never reached a garden-eating state within the turn budget (active_state=%" PRId64 ")", (int64_t)creature->active_state);
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Repeat_Current_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
