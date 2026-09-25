/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file dungeon_availability.c
 *     Callback-registration implementation. See dungeon_availability.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "dungeon_availability.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static TbBool noop_get_bool(PlayerNumber plyr_idx) { return false; }
static TbBool noop_get_bool_powerkind(PlayerNumber plyr_idx, PowerKind pwkind) { return false; }
static TbBool noop_get_bool_long(PlayerNumber plyr_idx, int64_t kind) { return false; }
static void noop_set_creature_availability(PlayerNumber plyr_idx, ThingModel crtr_model, int64_t can_be_avail, int64_t force_avail) {}
static void noop_try_set_backup_heart_idx(PlayerNumber owner, ThingIndex thing_idx) {}
static TbBool noop_set_room_resrchable_and_buildable(PlayerNumber plyr_idx, RoomKind rkind, int64_t resrch, int64_t avail) { return false; }
static TbBool noop_get_bool_roomkind(PlayerNumber plyr_idx, RoomKind rkind) { return false; }
static void noop_set_all_rooms(PlayerNumber plyr_idx) {}
static void noop_set_magic_resrchable(PlayerNumber plyr_idx, PowerKind pwkind, TbBool resrch) {}
static void noop_set_all_magic_resrchable_unchecked(PlayerNumber plyr_idx) {}

static const struct DungeonAvailabilityCallbacks default_dungeon_availability = {
    &noop_get_bool,                          /* player_has_valid_dungeon */
    &noop_get_bool,                          /* player_has_valid_dungeon_with_heart */
    &noop_get_bool,                          /* players_num_dungeon_valid */
    &noop_get_bool,                          /* players_num_dungeon_valid_with_heart */
    &noop_set_creature_availability,
    &noop_try_set_backup_heart_idx,
    &noop_set_room_resrchable_and_buildable,
    &noop_get_bool_roomkind,                 /* get_room_resrchable */
    &noop_set_all_rooms,                     /* set_all_room_resrchable */
    &noop_get_bool_roomkind,                 /* get_room_buildable */
    &noop_set_all_rooms,                     /* set_all_room_buildable_from_resrchable */
    &noop_get_bool_powerkind,                /* get_magic_resrchable */
    &noop_set_magic_resrchable,
    &noop_set_all_magic_resrchable_unchecked,
    &noop_get_bool_powerkind,                /* get_magic_level_gt0 */
    &noop_get_bool_long,                     /* get_trap_placeable */
    &noop_get_bool_long,                     /* get_trap_manufacturable */
    &noop_get_bool_long,                     /* get_trap_built */
    &noop_get_bool_long,                     /* get_door_placeable */
    &noop_get_bool_long,                     /* get_door_manufacturable */
    &noop_get_bool_long,                     /* get_door_built */
};
const struct DungeonAvailabilityCallbacks *dungeon_availability = &default_dungeon_availability;

void set_dungeon_availability_callbacks(const struct DungeonAvailabilityCallbacks *callbacks)
{
    dungeon_availability = callbacks ? callbacks : &default_dungeon_availability;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
