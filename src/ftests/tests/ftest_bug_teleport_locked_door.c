// A teleport destination behind a locked door of the caster's own player must not be chosen: the reachability check
// has to count locked doors as walls. The control run has the same room and door unlocked and teleports into the room.
#include "ftest_bug_teleport_locked_door.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_trapdoor.h"
#include "creature_instances.h"
#include "game_legacy.h"
#include "player_data.h"
#include "room_data.h"
#include "room_list.h"
#include "thing_creature.h"
#include "thing_doors.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// An island in the level's rock corner: corridor, door, then a torture room.
#define CORRIDOR_X0 1
#define ISLAND_Y 81
#define DOOR_X 4
#define ROOM_X0 5
#define ROOM_X1 7
#define ROOM_Y0 80
#define ROOM_Y1 82
#define TELEPORT_TARGET_X 2
#define TELEPORT_TARGET_Y 82
#define DESTINATION_TORTURE 4

static TbBool door_locked = false;
static struct Thing* teleporter = NULL;

FTestActionResult ftest_bug_teleport_locked_door_action001__setup(struct FTestActionArgs* const args);
FTestActionResult ftest_bug_teleport_locked_door_action002__teleport(struct FTestActionArgs* const args);
FTestActionResult ftest_bug_teleport_locked_door_action003__check(struct FTestActionArgs* const args);

TbBool ftest_bug_teleport_locked_door__locked_init()
{
    door_locked = true;
    ftest_append_action(ftest_bug_teleport_locked_door_action001__setup,    0,  NULL);
    ftest_append_action(ftest_bug_teleport_locked_door_action002__teleport, 3,  NULL);
    ftest_append_action(ftest_bug_teleport_locked_door_action003__check,    60, NULL);
    return true;
}

TbBool ftest_bug_teleport_locked_door__unlocked_init()
{
    door_locked = false;
    ftest_append_action(ftest_bug_teleport_locked_door_action001__setup,    0,  NULL);
    ftest_append_action(ftest_bug_teleport_locked_door_action002__teleport, 3,  NULL);
    ftest_append_action(ftest_bug_teleport_locked_door_action003__check,    60, NULL);
    return true;
}

static TbBool thing_is_in_island_room(const struct Thing* thing)
{
    const MapSlabCoord slb_x = subtile_slab(thing->mappos.x.stl.num);
    const MapSlabCoord slb_y = subtile_slab(thing->mappos.y.stl.num);
    return (slb_x >= ROOM_X0) && (slb_x <= ROOM_X1) && (slb_y >= ROOM_Y0) && (slb_y <= ROOM_Y1);
}

static TbBool thing_is_in_island_corridor(const struct Thing* thing)
{
    const MapSlabCoord slb_x = subtile_slab(thing->mappos.x.stl.num);
    const MapSlabCoord slb_y = subtile_slab(thing->mappos.y.stl.num);
    return (slb_x >= CORRIDOR_X0) && (slb_x <= DOOR_X) && (slb_y == ISLAND_Y);
}

FTestActionResult ftest_bug_teleport_locked_door_action001__setup(struct FTestActionArgs* const args)
{
    const PlayerNumber owner = my_player_number;
    teleporter = NULL;

    if (count_player_rooms_of_type(owner, RoK_TORTURE) != 0)
    {
        FTEST_FAIL_TEST("Player %" PRId64 " already has a torture room, the destination would be ambiguous", (int64_t)owner);
        return FTRs_Go_To_Next_Action;
    }

    ftest_util_replace_slabs(CORRIDOR_X0, ISLAND_Y, DOOR_X, ISLAND_Y, SlbT_CLAIMED, owner);
    ftest_util_replace_slabs(ROOM_X0, ROOM_Y0, ROOM_X1, ROOM_Y1, SlbT_TORTURE, owner);

    struct Coord3d door_pos;
    set_coords_to_slab_center(&door_pos, DOOR_X, ISLAND_Y);
    const char orient = find_door_angle(door_pos.x.stl.num, door_pos.y.stl.num, owner);
    struct Thing* door = create_door(&door_pos, (ThingModel)door_model_id("WOOD"), orient, owner, door_locked);
    if (thing_is_invalid(door))
    {
        FTEST_FAIL_TEST("Failed to create the door at slab (%" PRId64 ",%" PRId64 ")", (int64_t)DOOR_X, (int64_t)ISLAND_Y);
        return FTRs_Go_To_Next_Action;
    }

    if (count_player_rooms_of_type(owner, RoK_TORTURE) != 1)
    {
        FTEST_FAIL_TEST("Expected exactly one torture room on the island, found %" PRId64, (int64_t)count_player_rooms_of_type(owner, RoK_TORTURE));
        return FTRs_Go_To_Next_Action;
    }

    struct Coord3d pos;
    set_coords_to_slab_center(&pos, CORRIDOR_X0 + 1, ISLAND_Y);
    teleporter = ftest_util_create_creature(pos.x.val, pos.y.val, owner, 1, (ThingModel)creature_model_id("TROLL"));
    if (thing_is_invalid(teleporter))
    {
        FTEST_FAIL_TEST("Failed to create the creature in the corridor");
        teleporter = NULL;
    }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_bug_teleport_locked_door_action002__teleport(struct FTestActionArgs* const args)
{
    if (teleporter == NULL)
        return FTRs_Go_To_Next_Action;

    struct UserState* ustate = get_player_user_state(get_player(teleporter->owner));
    if (user_state_invalid(ustate))
    {
        FTEST_FAIL_TEST("No user state for player %" PRId64, (int64_t)teleporter->owner);
        return FTRs_Go_To_Next_Action;
    }
    ustate->teleport_destination = DESTINATION_TORTURE;
    ustate->nearest_teleport = false;

    // The spell only picks a destination when the position it was aimed at is inside a wall
    struct Coord3d target;
    set_coords_to_slab_center(&target, TELEPORT_TARGET_X, TELEPORT_TARGET_Y);
    const struct InstanceInfo* inst_inf = creature_instance_info_get(CrInst_TELEPORT);
    creature_cast_spell(teleporter, (SpellKind)inst_inf->func_params[0], 1, target.x.stl.num, target.y.stl.num);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_bug_teleport_locked_door_action003__check(struct FTestActionArgs* const args)
{
    if (teleporter == NULL)
        return FTRs_Go_To_Next_Action;

    if (thing_is_in_island_corridor(teleporter))
    {
        FTEST_FAIL_TEST("The creature never teleported");
        return FTRs_Go_To_Next_Action;
    }

    const TbBool in_room = thing_is_in_island_room(teleporter);
    FTESTLOG("Door %s, creature ended at slab (%" PRId64 ",%" PRId64 ")", door_locked ? "locked" : "unlocked",
        (int64_t)subtile_slab(teleporter->mappos.x.stl.num), (int64_t)subtile_slab(teleporter->mappos.y.stl.num));
    if (door_locked && in_room)
    {
        FTEST_FAIL_TEST("The creature teleported into a room sealed by its own locked door");
    }
    else if (!door_locked && !in_room)
    {
        FTEST_FAIL_TEST("The creature did not teleport into the room behind the unlocked door");
    }
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif
