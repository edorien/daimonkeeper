/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_hooks.c
 *     Callback-registration implementation. See script_hooks.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "script_hooks.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static void hook_noop_power_cast(PlayerNumber plyr_idx, PowerKind pwkind, int64_t splevel, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct Thing *thing) {}
static void hook_noop_plyr_thing(PlayerNumber plyr_idx, struct Thing *thing) {}
static void hook_noop_thing(struct Thing *thing) {}
static void hook_noop_damage(struct Thing *thing, HitPoints dmg, PlayerNumber dealing_plyr_idx) {}
static void hook_noop_thing_plyr(struct Thing *thing, PlayerNumber plyr_idx) {}
static void hook_noop_slab_kind_change(MapSlabCoord slb_x, MapSlabCoord slb_y, SlabKind old_slab) {}
static void hook_noop_slab_owner_change(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber old_owner) {}
static void hook_noop_room_owner_change(struct Room *room, PlayerNumber old_owner) {}
static void hook_noop_shot_hit(struct Thing *shot, struct Thing *shooter, struct Thing *target, MapSubtlCoord next_stl_x, MapSubtlCoord next_stl_y, bool rebound_hit) {}
static void hook_noop_plyr(PlayerNumber plyr_idx) {}
static int64_t hook_noop_crstate_func(FuncIdx func_idx, struct Thing *thing) { return -1; }
static int64_t hook_noop_shot_hit_thing_func(FuncIdx func_idx, struct Thing *shot, struct Thing *shooter, struct Thing *target, MapSubtlCoord next_stl_x, MapSubtlCoord next_stl_y) { return -1; }
static TbResult hook_noop_luafunc_magic_use_power(FuncIdx func_idx, PlayerNumber plyr_idx, PowerKind pwkind, int64_t splevel, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct Thing *thing, uint64_t allow_flags) { return -1; }
static int64_t hook_noop_luafunc_trap_activation_func(FuncIdx func_idx, struct Thing *trap, struct Thing *creature) { return -1; }
static void hook_noop_luafunc_room_capacity_func(FuncIdx func_idx, struct Room *room) {}
static void hook_noop_api_event(const char *event_name) {}
static void hook_noop_api_event_with_data(const char *event_name, const struct ApiEventData *data, size_t data_count) {}

static void hook_noop_lua_on_game_start(void) {}
static TbBool hook_noop_open_lua_script(LevelNumber lvnum) { return false; }
static TbBool hook_noop_execute_lua_code_from_console(const char *code) { return false; }
static TbBool hook_noop_execute_lua_code_from_script(const char *code) { return false; }
static void hook_noop_generate_lua_types_file(void) {}
static const char *hook_noop_lua_get_serialised_data(size_t *len) { if (len) *len = 0; return NULL; }
static TbBool hook_noop_lua_set_serialised_data(const char *data, size_t len) { return false; }
static void hook_noop_cleanup_serialized_data(void) {}

static const struct ScriptHookCallbacks default_script_hooks = {
    &hook_noop_power_cast,
    &hook_noop_plyr_thing,       /* lua_on_special_box_activate */
    &hook_noop_thing,            /* lua_on_creature_death */
    &hook_noop_thing,            /* lua_on_creature_fell_into_abyss */
    &hook_noop_thing,            /* lua_on_creature_rebirth */
    &hook_noop_thing,            /* lua_on_trap_placed */
    &hook_noop_thing,            /* lua_on_object_destroyed */
    &hook_noop_damage,           /* lua_on_apply_damage_to_thing */
    &hook_noop_thing,            /* lua_on_level_up */
    &hook_noop_thing_plyr,       /* lua_on_pick_up */
    &hook_noop_thing_plyr,       /* lua_on_slap */
    &hook_noop_slab_kind_change,
    &hook_noop_slab_owner_change,
    &hook_noop_room_owner_change,
    &hook_noop_shot_hit,
    &hook_noop_plyr,             /* lua_on_dungeon_destroyed */
    &hook_noop_crstate_func,     /* luafunc_crstate_func */
    &hook_noop_crstate_func,     /* luafunc_thing_update_func (same shape) */
    &hook_noop_shot_hit_thing_func,
    &hook_noop_luafunc_magic_use_power,
    &hook_noop_luafunc_trap_activation_func,
    &hook_noop_luafunc_room_capacity_func,
    &hook_noop_api_event,
    &hook_noop_api_event_with_data,

    &hook_noop_lua_on_game_start,
    &hook_noop_open_lua_script,
    &hook_noop_execute_lua_code_from_console,
    &hook_noop_execute_lua_code_from_script,
    &hook_noop_generate_lua_types_file,
    &hook_noop_lua_get_serialised_data,
    &hook_noop_lua_set_serialised_data,
    &hook_noop_cleanup_serialized_data,
};
const struct ScriptHookCallbacks *script_hooks = &default_script_hooks;

void set_script_hook_callbacks(const struct ScriptHookCallbacks *callbacks)
{
    script_hooks = callbacks ? callbacks : &default_script_hooks;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
