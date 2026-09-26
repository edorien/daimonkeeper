/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_hooks.h
 *     Header file for script_hooks.c.
 * @par Purpose:
 *     Callback-registration interface letting kfx_sim and kfx_game notify
 *     the Lua scripting layer and the external HTTP API of in-game events,
 *     without including lua_triggers.h/lua_cfg_funcs.h/lua_base.h/api.h
 *     directly (those are kfx_script/app_entry layer, above both). See
 *     docs/refactor/stage-06-kfx-sim.md and
 *     docs/refactor/stage-09-kfx-game.md.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_SCRIPT_HOOKS_H
#define DK_SCRIPT_HOOKS_H

#include "bflib_basics.h"
#include "globals.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;
struct Room;

// Mirrors api.h's enum ApiEventDataType/struct ApiEventData (kfx_script,
// above kfx_sim) -- kfx_sim's actionpt.c is the lowest-ranked real
// producer of event data payloads, so the small vocabulary type lives
// here alongside the callback that consumes it, same "moved to the
// lowest layer that needs it" treatment as this codebase's other small
// enums (e.g. globals.h's MessageTypes).
enum ApiEventDataType {
    API_EVENT_DATA_INT32,
    API_EVENT_DATA_UINT32,
    API_EVENT_DATA_INT64,
    API_EVENT_DATA_UINT64,
    API_EVENT_DATA_FLOAT,
    API_EVENT_DATA_DOUBLE,
    API_EVENT_DATA_BOOL,
    API_EVENT_DATA_STRING,
};

struct ApiEventData {
    const char *name;
    enum ApiEventDataType type;
    union {
        int64_t int32_value;
        uint64_t uint32_value;
        int64_t int64_value;
        uint64_t uint64_value;
        double float_value;
        double double_value;
        bool bool_value;
        const char *string_value;
    } value;
};

struct ScriptHookCallbacks {
    /* lua_triggers.h event notifications actually consumed inside kfx_sim */
    void (*lua_on_power_cast)(PlayerNumber plyr_idx, PowerKind pwkind, int64_t splevel, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct Thing *thing);
    void (*lua_on_special_box_activate)(PlayerNumber plyr_idx, struct Thing *cratetng);
    void (*lua_on_creature_death)(struct Thing *crtng);
    void (*lua_on_creature_fell_into_abyss)(struct Thing *crtng);
    void (*lua_on_creature_rebirth)(struct Thing *crtng);
    void (*lua_on_trap_placed)(struct Thing *traptng);
    void (*lua_on_object_destroyed)(struct Thing *objtng);
    void (*lua_on_apply_damage_to_thing)(struct Thing *thing, HitPoints dmg, PlayerNumber dealing_plyr_idx);
    void (*lua_on_level_up)(struct Thing *thing);
    void (*lua_on_pick_up)(struct Thing *thing, PlayerNumber plyr_idx);
    void (*lua_on_slap)(struct Thing *thing, PlayerNumber plyr_idx);
    void (*lua_on_slab_kind_change)(MapSlabCoord slb_x, MapSlabCoord slb_y, SlabKind old_slab);
    void (*lua_on_slab_owner_change)(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber old_owner);
    void (*lua_on_room_owner_change)(struct Room *room, PlayerNumber old_owner);
    void (*lua_on_shot_hit)(struct Thing *shot, struct Thing *shooter, struct Thing *target, MapSubtlCoord next_stl_x, MapSubtlCoord next_stl_y, bool rebound_hit);
    void (*lua_on_dungeon_destroyed)(PlayerNumber plyr_idx);

    /* lua_cfg_funcs.h lua-registered function dispatch (negative-index convention) */
    int64_t (*luafunc_crstate_func)(FuncIdx func_idx, struct Thing *thing);
    int64_t (*luafunc_thing_update_func)(FuncIdx func_idx, struct Thing *thing);
    int64_t (*luafunc_shot_hit_thing_func)(FuncIdx func_idx, struct Thing *shot, struct Thing *shooter, struct Thing *target, MapSubtlCoord next_stl_x, MapSubtlCoord next_stl_y);
    TbResult (*luafunc_magic_use_power)(FuncIdx func_idx, PlayerNumber plyr_idx, PowerKind pwkind, int64_t splevel, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct Thing *thing, uint64_t allow_flags);
    int64_t (*luafunc_trap_activation_func)(FuncIdx func_idx, struct Thing *trap, struct Thing *creature);
    void (*luafunc_room_capacity_func)(FuncIdx func_idx, struct Room *room);

    /* api.h external HTTP API notification */
    void (*api_event)(const char *event_name);
    void (*api_event_with_data)(const char *event_name, const struct ApiEventData *data, size_t data_count);

    /* lua_triggers.h/lua_base.h -- kfx_game's script-lifecycle needs
       (stage 9, docs/refactor/stage-09-kfx-game.md), same struct rather
       than a separate one since it's the same "callbacks into kfx_script"
       shape as everything else here. */
    void (*lua_on_game_start)(void);
    TbBool (*open_lua_script)(LevelNumber lvnum);
    TbBool (*execute_lua_code_from_console)(const char *code);
    TbBool (*execute_lua_code_from_script)(const char *code);
    void (*generate_lua_types_file)(void);
    const char *(*lua_get_serialised_data)(size_t *len);
    TbBool (*lua_set_serialised_data)(const char *data, size_t len);
    void (*cleanup_serialized_data)(void);
};
void set_script_hook_callbacks(const struct ScriptHookCallbacks *callbacks);
extern const struct ScriptHookCallbacks *script_hooks;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
