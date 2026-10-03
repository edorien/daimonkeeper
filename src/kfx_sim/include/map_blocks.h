/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_blocks.h
 *     Header file for map_blocks.c.
 * @par Purpose:
 *     Map blocks support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_MAP_BLOCKS_H
#define DK_MAP_BLOCKS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

struct Thing;
struct Map;

#pragma pack()
/******************************************************************************/
TbBool block_has_diggable_side(MapSlabCoord slb_x, MapSlabCoord slb_y);
int64_t block_count_diggable_sides(MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool tag_blocks_for_digging_in_area(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx);
TbBool untag_blocks_for_digging_in_area(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx);
void all_players_untag_blocks_for_digging_in_area(MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool subtile_is_diggable_at_diagonal_angle(struct Thing *thing, int64_t angle, MapSubtlCoord stl_x, MapSubtlCoord stl_y);

#define place_slab_type_on_map(nslab, stl_x, stl_y, owner, keep_blocks_around) place_slab_type_on_map_f(nslab, stl_x, stl_y, owner, keep_blocks_around, __func__)
void place_slab_type_on_map_f(SlabKind nslab, MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber owner, unsigned char a5,const char *func_name);
void update_wibble_on_surrounding_slabs(MapSlabCoord slb_x, MapSlabCoord slb_y);
void mine_out_block(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx);
TbBool dig_has_revealed_area(MapSubtlCoord rev_stl_x, MapSubtlCoord rev_stl_y, PlayerNumber plyr_idx);
void dig_out_block(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx);
void neutralise_enemy_block(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber domn_plyr_idx);
void check_map_explored(struct Thing* creatng, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
TbBool set_slab_explored(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
void set_slab_explored_flags(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
int64_t torch_flags_for_slab(MapSlabCoord slb_x, MapSlabCoord slb_y);
void update_floor_and_ceiling_heights_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y,
    MapSubtlCoord *floor_height, MapSubtlCoord *ceiling_height);
TbBool point_in_map_is_solid(const struct Coord3d *pos);
void project_point_to_wall_on_angle(const struct Coord3d *pos1, struct Coord3d *pos2, int64_t angle_xy, int64_t angle_z, int64_t distance, int64_t num_steps);
TbBool point_in_map_is_solid_ignoring_door(const struct Coord3d *pos, const struct Thing *doortng);
int64_t get_point_in_map_solid_flags_ignoring_door(const struct Coord3d *pos, const struct Thing *doortng);
int64_t get_point_in_map_solid_flags_ignoring_own_door(const struct Coord3d *pos, PlayerNumber plyr_idx);
SlabKind alter_rock_style(SlabKind slbkind, MapSlabCoord tgslb_x, MapSlabCoord tgslb_y, PlayerNumber owner);
void create_dirt_rubble_for_dug_slab(MapSlabCoord slb_x, MapSlabCoord slb_y);
void create_dirt_rubble_for_dug_block(MapSubtlCoord stl_x, MapSubtlCoord stl_y, MapSubtlCoord stl_height, PlayerNumber owner);
void place_and_process_pretty_wall_slab(struct Thing *creatng, MapSlabCoord slb_x, MapSlabCoord slb_y);
void place_animating_slab_type_on_map(SlabKind slbkind, char ani_frame, MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber owner);
SlabKind choose_pretty_type(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
void pretty_map_remove_flags_and_update(MapSlabCoord slb_x, MapSlabCoord slb_y);
void fill_in_reinforced_corners(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);

void set_explored_around(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber plyr_idx);
int64_t element_top_face_texture(struct Map *map);
void reinit_tagged_blocks_for_player(PlayerNumber plyr_idx);
void initialise_map_collides(void);
void initialise_map_health(void);
void place_single_slab_type_on_map(SlabKind slbkind, MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber plyr_idx);
/** Whether the fortified wall at (wall_x, wall_y) is built with a face toward its neighbour (nb_x, nb_y): the rule its
 *  columns are chosen by (get_against), so it holds whether or not the neighbour has been seen -- a keeper watching the
 *  wall can tell open ground (or another keeper's wall) lies behind it. False if the slab is not a fortified wall. */
TbBool wall_slab_shows_face_toward(MapSlabCoord wall_x, MapSlabCoord wall_y, MapSlabCoord nb_x, MapSlabCoord nb_y);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
