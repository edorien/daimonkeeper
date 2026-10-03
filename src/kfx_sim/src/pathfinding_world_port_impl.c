/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file pathfinding_world_port_impl.c
 *     kfx_sim's PathfindingWorldPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "pathfinding_world_port_impl.h"
#include "map_data.h"
#include "map_columns.h"
#include "slab_data.h"
#include "thing_list.h"
#include "thing_doors.h"
#include "player_data.h"
#include "thing_data.h"
#include "thing_physics.h"
#include "thing_navigate.h"
#include "thing_stats.h"
#include "creature_control.h"
#include "thing_navigate.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Wrappers for the table below: return-type mismatches against the real
// kfx_sim functions (PlayerNumber vs. int64_t, TbBool vs. int64_t) and the
// CreatureControl field reads that have no accessor. Everything else binds
// directly to its real kfx_sim function.
static PlayerNumber pathfinding_world_slabmap_owner(const struct SlabMap *slb)
{
    return (PlayerNumber)slabmap_owner(slb);
}
static TbBool pathfinding_world_thing_in_wall_at(const struct Thing *thing, const struct Coord3d *pos)
{
    return thing_in_wall_at(thing, pos) != 0;
}
static struct Navigation *pathfinding_world_creature_get_navigation(struct Thing *creatng)
{
    return &creature_control_get_from_thing(creatng)->navi;
}
static struct Ariadne *pathfinding_world_creature_get_ariadne_state(struct Thing *creatng)
{
    return &creature_control_get_from_thing(creatng)->arid;
}
static int64_t pathfinding_world_creature_get_max_speed(const struct Thing *creatng)
{
    return creature_control_get_from_thing(creatng)->max_speed;
}
static void pathfinding_world_creature_clear_state_flags_for_wallhug_override(struct Thing *creatng)
{
    struct CreatureControl *cctrl = creature_control_get_from_thing(creatng);
    cctrl->creature_state_flags = 0;
    cctrl->combat_flags = 0;
}

static TbBool pathfinding_world_creature_steps_into_toxic_terrain(struct Thing *thing, const struct Coord3d *pos)
{
    return !flag_is_set(thing->alloc_flags, TAlF_IsControlled)
        && !terrain_toxic_for_creature_at_position(thing, thing->mappos.x.stl.num, thing->mappos.y.stl.num)
        && terrain_toxic_for_creature_at_position(thing, pos->x.stl.num, pos->y.stl.num);
}

const struct PathfindingWorldPort kfx_sim_pathfinding_world_port = {
    .get_map_block_at = &get_map_block_at,
    .get_map_block_at_pos = &get_map_block_at_pos,
    .map_block_is_invalid = &map_block_invalid,
    .get_floor_filled_subtiles_at = &get_floor_filled_subtiles_at,
    .subtile_is_unsafe = &subtile_is_unsafe,
    .get_slabmap_block = &get_slabmap_block,
    .slabmap_block_is_invalid = &slabmap_block_invalid,
    .slabmap_owner = &pathfinding_world_slabmap_owner,
    .is_valid_hug_subtile = &is_valid_hug_subtile,
    .subtile_is_door = &subtile_is_door,
    .thing_in_wall_at = &pathfinding_world_thing_in_wall_at,
    .get_door_for_position = &get_door_for_position,
    .door_is_hidden_to_player = &door_is_hidden_to_player,
    .door_will_open_for_thing = &door_will_open_for_thing,
    .players_are_mutual_allies = &players_are_mutual_allies,
    .thing_is_invalid = &thing_is_invalid,
    .get_thing_height_at = &get_thing_height_at,
    .get_floor_height_under_thing_at = &get_floor_height_under_thing_at,
    .creature_can_travel_over_lava = &creature_can_travel_over_lava,
    .thing_model_name = &thing_model_name,
    .creature_get_navigation = &pathfinding_world_creature_get_navigation,
    .creature_get_ariadne_state = &pathfinding_world_creature_get_ariadne_state,
    .creature_get_max_speed = &pathfinding_world_creature_get_max_speed,
    .creature_clear_state_flags_for_wallhug_override = &pathfinding_world_creature_clear_state_flags_for_wallhug_override,
    .get_slabmap_for_subtile = &get_slabmap_for_subtile,
    .hug_can_move_on = &hug_can_move_on,
    .creature_cannot_move_directly_to = &creature_cannot_move_directly_to,
    .subtile_has_abyss_on_top = &subtile_has_abyss_on_top,
    .creature_steps_into_toxic_terrain = &pathfinding_world_creature_steps_into_toxic_terrain,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
