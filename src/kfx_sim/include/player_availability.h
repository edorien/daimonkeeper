/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_availability.h
 *     Header file for player_availability.c.
 * @par Purpose:
 *     Per-player room/power/trap/door/creature availability. Moved from
 *     kfx_config's config_*.h (docs/refactor-pass2/
 *     stage-05-config-gameplay-to-sim.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PLAYER_AVAILABILITY_H
#define DK_PLAYER_AVAILABILITY_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Rooms
TbBool set_room_available(PlayerNumber plyr_idx, RoomKind roomkind, int64_t resrch, int64_t avail);
TbBool make_available_all_researchable_rooms(PlayerNumber plyr_idx);
TbBool make_all_rooms_researchable(PlayerNumber plyr_idx);
TbBool is_room_available(PlayerNumber plyr_idx, RoomKind roomkind);
TbBool is_room_obtainable(PlayerNumber plyr_idx, RoomKind rkind);
TbBool is_room_of_role_available(PlayerNumber plyr_idx, RoomRole rrole);
RoomKind find_first_available_roomkind_with_role(PlayerNumber plyr_idx, RoomRole rrole);

// Keeper powers
TbBool make_all_powers_researchable(PlayerNumber plyr_idx);
TbBool set_power_available(PlayerNumber plyr_idx, PowerKind spl_idx, int64_t resrch, int64_t avail);
TbBool is_power_available(PlayerNumber plyr_idx, PowerKind spl_idx);
TbBool is_power_obtainable(PlayerNumber plyr_idx, PowerKind pwkind);
TbBool make_available_all_researchable_powers(PlayerNumber plyr_idx);

// Traps and doors
TbBool is_trap_placeable(PlayerNumber plyr_idx, int64_t trap_idx);
TbBool is_trap_buildable(PlayerNumber plyr_idx, int64_t trap_idx);
TbBool is_trap_built(PlayerNumber plyr_idx, int64_t tngmodel);
TbBool is_door_placeable(PlayerNumber plyr_idx, int64_t door_idx);
TbBool is_door_buildable(PlayerNumber plyr_idx, int64_t door_idx);
TbBool is_door_built(PlayerNumber plyr_idx, int64_t door_idx);
TbBool make_available_all_doors(PlayerNumber plyr_idx);
TbBool make_available_all_traps(PlayerNumber plyr_idx);

// Creatures
TbBool set_creature_available(PlayerNumber plyr_idx, ThingModel crtr_model, int64_t can_be_avail, int64_t force_avail);
ThingModel get_players_special_digger_model(PlayerNumber plyr_idx);
ThingModel get_players_spectator_model(PlayerNumber plyr_idx);
void update_players_special_digger_model(PlayerNumber plyr_idx, ThingModel new_dig_model);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
