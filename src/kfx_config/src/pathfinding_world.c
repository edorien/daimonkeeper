/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file pathfinding_world.c
 *     Callback-registration implementation. See pathfinding_world.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "pathfinding_world.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static MapSubtlCoord noop_get_map_size_x(void) { return 0; }
static MapSubtlCoord noop_get_map_size_y(void) { return 0; }
static struct Map *noop_get_map_block_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return NULL; }
static struct Map *noop_get_map_block_at_pos(SubtlCodedCoords stl_num) { return NULL; }
static unsigned char noop_map_block_flags(const struct Map *mapblk) { return 0; }
static TbBool noop_map_block_is_invalid(const struct Map *mapblk) { return true; }
static int64_t noop_get_floor_filled_subtiles_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return 0; }
static TbBool noop_subtile_is_unsafe(MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return false; }
static struct SlabMap *noop_get_slabmap_block(MapSlabCoord slb_x, MapSlabCoord slb_y) { return NULL; }
static SlabKind noop_slabmap_block_kind(const struct SlabMap *slb) { return 0; }
static TbBool noop_slabmap_block_is_invalid(const struct SlabMap *slb) { return true; }
static PlayerNumber noop_slabmap_owner(const struct SlabMap *slb) { return 0; }
static TbBool noop_is_valid_hug_subtile(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx) { return false; }
static TbBool noop_subtile_is_door(MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return false; }
static TbBool noop_thing_in_wall_at(const struct Thing *thing, const struct Coord3d *pos) { return false; }
static struct Thing *noop_get_door_for_position(MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return NULL; }
static TbBool noop_door_is_hidden_to_player(struct Thing *doortng, PlayerNumber plyr_idx) { return false; }
static TbBool noop_door_will_open_for_thing(const struct Thing *doortng, const struct Thing *creatng) { return false; }
static TbBool noop_door_is_locked(const struct Thing *doortng) { return false; }
static TbBool noop_players_are_mutual_allies(PlayerNumber plyr1_idx, PlayerNumber plyr2_idx) { return false; }
static int64_t noop_thing_is_invalid(const struct Thing *thing) { return true; }
static PlayerNumber noop_thing_get_owner(const struct Thing *thing) { return 0; }
static int64_t noop_get_thing_height_at(const struct Thing *thing, const struct Coord3d *pos) { return 0; }
static int64_t noop_get_floor_height_under_thing_at(const struct Thing *thing, const struct Coord3d *pos) { return 0; }
static TbBool noop_creature_can_travel_over_lava(const struct Thing *creatng) { return false; }
static TbBool noop_thing_is_flying(const struct Thing *thing) { return false; }
static const char *noop_thing_model_name(const struct Thing *thing) { return ""; }
static struct Coord3d noop_thing_get_position(const struct Thing *thing) { struct Coord3d pos = {0}; return pos; }
static void noop_thing_set_position(struct Thing *thing, const struct Coord3d *pos) {}
static int64_t noop_thing_get_move_angle(const struct Thing *thing) { return 0; }
static void noop_thing_set_move_angle(struct Thing *thing, int64_t angle) {}
static int64_t noop_thing_get_index(const struct Thing *thing) { return 0; }
static int64_t noop_thing_get_clipbox_size(const struct Thing *thing) { return 0; }
static struct Navigation *noop_creature_get_navigation(struct Thing *creatng) { return NULL; }
static struct Ariadne *noop_creature_get_ariadne_state(struct Thing *creatng) { return NULL; }
static int64_t noop_creature_get_max_speed(const struct Thing *creatng) { return 0; }
static void noop_creature_clear_state_flags_for_wallhug_override(struct Thing *creatng) {}
static SubtlCodedCoords noop_get_subtile_number(MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return 0; }
static MapSubtlCoord noop_stl_num_decode_x(SubtlCodedCoords stl_num) { return 0; }
static MapSubtlCoord noop_stl_num_decode_y(SubtlCodedCoords stl_num) { return 0; }
static MapSubtlCoord noop_stl_slab_center_subtile(MapSubtlCoord stl_v) { return 0; }
static struct SlabMap *noop_get_slabmap_for_subtile(MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return NULL; }
static TbBool noop_hug_can_move_on(struct Thing *creatng, MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return false; }
static TbBool noop_cross_x_boundary_first(const struct Coord3d *pos1, const struct Coord3d *pos2) { return false; }
static TbBool noop_cross_y_boundary_first(const struct Coord3d *pos1, const struct Coord3d *pos2) { return false; }
static struct Around noop_get_small_around(SmallAroundIndex n) { struct Around a = {0}; return a; }
static SmallAroundIndex noop_get_small_around_length(void) { return 0; }
static SmallAroundIndex noop_small_around_index_in_direction(int64_t srcpos_x, int64_t srcpos_y, int64_t dstpos_x, int64_t dstpos_y) { return 0; }
static MapSubtlCoord noop_get_map_size_z(void) { return 0; }
static TbBool noop_creature_cannot_move_directly_to(struct Thing *thing, struct Coord3d *pos) { return true; }
static int64_t noop_get_owner_player_navigating(void) { return -1; }
static void noop_set_owner_player_navigating(int64_t plyr_idx) {}
static int64_t noop_get_nav_thing_can_travel_over_lava(void) { return 0; }
static void noop_set_nav_thing_can_travel_over_lava(int64_t can_travel) {}
static int64_t noop_get_nav_thing_is_flying(void) { return 0; }
static void noop_set_nav_thing_is_flying(int64_t is_flying) {}
static TbBool noop_subtile_has_abyss_on_top(MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return false; }
static TbBool noop_creature_steps_into_toxic_terrain(struct Thing *thing, const struct Coord3d *pos) { return false; }

static const struct PathfindingWorldCallbacks default_pathfinding_world = {
    &noop_get_map_size_x,
    &noop_get_map_size_y,
    &noop_get_map_block_at,
    &noop_get_map_block_at_pos,
    &noop_map_block_flags,
    &noop_map_block_is_invalid,
    &noop_get_floor_filled_subtiles_at,
    &noop_subtile_is_unsafe,
    &noop_get_slabmap_block,
    &noop_slabmap_block_kind,
    &noop_slabmap_block_is_invalid,
    &noop_slabmap_owner,
    &noop_is_valid_hug_subtile,
    &noop_subtile_is_door,
    &noop_thing_in_wall_at,
    &noop_get_door_for_position,
    &noop_door_is_hidden_to_player,
    &noop_door_will_open_for_thing,
    &noop_door_is_locked,
    &noop_players_are_mutual_allies,
    &noop_thing_is_invalid,
    &noop_thing_get_owner,
    &noop_get_thing_height_at,
    &noop_get_floor_height_under_thing_at,
    &noop_creature_can_travel_over_lava,
    &noop_thing_is_flying,
    &noop_thing_model_name,
    &noop_thing_get_position,
    &noop_thing_set_position,
    &noop_thing_get_move_angle,
    &noop_thing_set_move_angle,
    &noop_thing_get_index,
    &noop_thing_get_clipbox_size,
    &noop_creature_get_navigation,
    &noop_creature_get_ariadne_state,
    &noop_creature_get_max_speed,
    &noop_creature_clear_state_flags_for_wallhug_override,
    &noop_get_subtile_number,
    &noop_stl_num_decode_x,
    &noop_stl_num_decode_y,
    &noop_stl_slab_center_subtile,
    &noop_get_slabmap_for_subtile,
    &noop_hug_can_move_on,
    &noop_cross_x_boundary_first,
    &noop_cross_y_boundary_first,
    &noop_get_small_around,
    &noop_get_small_around_length,
    &noop_small_around_index_in_direction,
    &noop_get_map_size_z,
    &noop_creature_cannot_move_directly_to,
    &noop_get_owner_player_navigating,
    &noop_set_owner_player_navigating,
    &noop_get_nav_thing_can_travel_over_lava,
    &noop_set_nav_thing_can_travel_over_lava,
    &noop_get_nav_thing_is_flying,
    &noop_set_nav_thing_is_flying,
    &noop_subtile_has_abyss_on_top,
    &noop_creature_steps_into_toxic_terrain,
};
const struct PathfindingWorldCallbacks *pathfinding_world = &default_pathfinding_world;

void set_pathfinding_world_callbacks(const struct PathfindingWorldCallbacks *callbacks)
{
    pathfinding_world = callbacks ? callbacks : &default_pathfinding_world;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
