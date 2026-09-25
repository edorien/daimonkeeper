/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file dungeon_availability.h
 *     Header file for dungeon_availability.c.
 * @par Purpose:
 *     Callback-registration interface letting config_creature.c/
 *     config_magic.c/config_objects.c/config_trapdoor.c (kfx_config)
 *     touch struct Dungeon's per-kind availability/build-state fields
 *     (creature_allowed, magic_resrchable, mnfct_info fields,
 *     dnheart_idx, ...) without including kfx_sim's dungeon_data.h
 *     directly. See docs/refactor/stage-13-enforce-and-document.md.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_DUNGEON_AVAILABILITY_H
#define DK_DUNGEON_AVAILABILITY_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

struct DungeonAvailabilityCallbacks {
    // Generic dungeon-validity checks, replacing the repeated
    // get_dungeon+dungeon_invalid / get_players_num_dungeon+
    // dungeon_invalid[+player_has_heart] preambles scattered across
    // config_creature.c/config_magic.c/config_trapdoor.c. Callers keep
    // their own ERRORLOG/ERRORDBG messages -- these return only the
    // boolean verdict, so log text stays byte-identical to before.
    TbBool (*player_has_valid_dungeon)(PlayerNumber plyr_idx);              /* get_dungeon-based, no heart check */
    TbBool (*player_has_valid_dungeon_with_heart)(PlayerNumber plyr_idx);   /* get_dungeon-based, + player_has_heart */
    TbBool (*players_num_dungeon_valid)(PlayerNumber plyr_idx);             /* get_players_num_dungeon-based, no heart check */
    TbBool (*players_num_dungeon_valid_with_heart)(PlayerNumber plyr_idx);  /* get_players_num_dungeon-based, + player_has_heart */

    /* config_creature.c -- creature_allowed[]/creature_force_enabled[] */
    void (*set_creature_availability)(PlayerNumber plyr_idx, ThingModel crtr_model, int64_t can_be_avail, int64_t force_avail);

    /* config_objects.c -- dnheart_idx/backup_heart_idx */
    void (*try_set_backup_heart_idx)(PlayerNumber owner, ThingIndex thing_idx);

    /* config_terrain.c -- room_resrchable[]/room_buildable[]; returns
       the final buildable-bit state so the caller knows whether to
       notify the player's computer AI. */
    TbBool (*set_room_resrchable_and_buildable)(PlayerNumber plyr_idx, RoomKind rkind, int64_t resrch, int64_t avail);
    TbBool (*get_room_resrchable)(PlayerNumber plyr_idx, RoomKind rkind);
    void (*set_all_room_resrchable)(PlayerNumber plyr_idx);
    TbBool (*get_room_buildable)(PlayerNumber plyr_idx, RoomKind rkind);
    void (*set_all_room_buildable_from_resrchable)(PlayerNumber plyr_idx);

    /* config_magic.c -- magic_resrchable[]/magic_level[] */
    TbBool (*get_magic_resrchable)(PlayerNumber plyr_idx, PowerKind pwkind);
    void (*set_magic_resrchable)(PlayerNumber plyr_idx, PowerKind pwkind, TbBool resrch);
    void (*set_all_magic_resrchable_unchecked)(PlayerNumber plyr_idx); /* mirrors make_all_powers_researchable()'s lack of validation */
    TbBool (*get_magic_level_gt0)(PlayerNumber plyr_idx, PowerKind pwkind);

    /* config_trapdoor.c -- mnfct_info.trap_/door_ fields (each entry
       does its own full validation, including the heart check where
       the original function had one, since each is used exactly once) */
    TbBool (*get_trap_placeable)(PlayerNumber plyr_idx, int64_t tngmodel);
    TbBool (*get_trap_manufacturable)(PlayerNumber plyr_idx, int64_t tngmodel);
    TbBool (*get_trap_built)(PlayerNumber plyr_idx, int64_t tngmodel);
    TbBool (*get_door_placeable)(PlayerNumber plyr_idx, int64_t door_idx);
    TbBool (*get_door_manufacturable)(PlayerNumber plyr_idx, int64_t door_idx);
    TbBool (*get_door_built)(PlayerNumber plyr_idx, int64_t door_idx);
};
void set_dungeon_availability_callbacks(const struct DungeonAvailabilityCallbacks *callbacks);
extern const struct DungeonAvailabilityCallbacks *dungeon_availability;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
