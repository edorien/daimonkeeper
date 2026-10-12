/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_port_impl.c
 *     kfx_script's ScriptPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "script_port_impl.h"
#include "lua_triggers.h"
#include "lua_cfg_funcs.h"
#include "api.h"
#include "api_seat_combatstats.h"
#include "lua_base.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static TbBool lua_script_active_now(void)
{
    return Lvl_script != NULL;
}

// Two script-port events also feed the combat statistics (api_seat_combatstats.h), which only watch.
static void on_power_cast(PlayerNumber plyr_idx, PowerKind pwkind, int64_t splevel, MapSubtlCoord stl_x,
    MapSubtlCoord stl_y, struct Thing *thing)
{
    api_seat_combatstats_note_cast(plyr_idx, pwkind, splevel, thing);
    lua_on_power_cast(plyr_idx, pwkind, splevel, stl_x, stl_y, thing);
}

static void on_creature_death(struct Thing *crtng)
{
    api_seat_combatstats_note_death(crtng);
    lua_on_creature_death(crtng);
}


const struct ScriptPort kfx_script_port = {
    .lua_on_power_cast = &on_power_cast,
    .lua_on_special_box_activate = &lua_on_special_box_activate,
    .lua_on_creature_death = &on_creature_death,
    .lua_on_creature_fell_into_abyss = &lua_on_creature_fell_into_abyss,
    .lua_on_creature_rebirth = &lua_on_creature_rebirth,
    .lua_on_trap_placed = &lua_on_trap_placed,
    .lua_on_object_destroyed = &lua_on_object_destroyed,
    .lua_on_apply_damage_to_thing = &lua_on_apply_damage_to_thing,
    .lua_on_level_up = &lua_on_level_up,
    .lua_on_pick_up = &lua_on_pick_up,
    .lua_on_slap = &lua_on_slap,
    .lua_on_slab_kind_change = &lua_on_slab_kind_change,
    .lua_on_slab_owner_change = &lua_on_slab_owner_change,
    .lua_on_room_owner_change = &lua_on_room_owner_change,
    .lua_on_shot_hit = &lua_on_shot_hit,
    .lua_on_dungeon_destroyed = &lua_on_dungeon_destroyed,
    .luafunc_crstate_func = &luafunc_crstate_func,
    .luafunc_thing_update_func = &luafunc_thing_update_func,
    .luafunc_shot_hit_thing_func = &luafunc_shot_hit_thing_func,
    .luafunc_magic_use_power = &luafunc_magic_use_power,
    .luafunc_trap_activation_func = &luafunc_trap_activation_func,
    .luafunc_room_capacity_func = &luafunc_room_capacity_func,
    .api_event = &api_event,
    .api_event_with_data = &api_event_with_data,
    .lua_on_game_start = &lua_on_game_start,
    .open_lua_script = &open_lua_script,
    .execute_lua_code_from_console = &execute_lua_code_from_console,
    .execute_lua_code_from_script = &execute_lua_code_from_script,
    .generate_lua_types_file = &generate_lua_types_file,
    .lua_get_serialised_data = &lua_get_serialised_data,
    .lua_set_serialised_data = &lua_set_serialised_data,
    .cleanup_serialized_data = &cleanup_serialized_data,
    .lua_on_chatmsg = &lua_on_chatmsg,
    .lua_script_active = &lua_script_active_now,
    .lua_resync_export = &lua_resync_export,
    .lua_resync_import = &lua_resync_import,
    .lua_set_random_seed = &lua_set_random_seed,
    .get_lua_function_idx = &get_function_idx,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
