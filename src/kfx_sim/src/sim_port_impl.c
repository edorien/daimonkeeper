/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sim_port_impl.c
 *     kfx_sim's SimPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "sim_port_impl.h"
#include "kfx_sim_state.h"
#include "thing_doors.h"
#include "thing_traps.h"
#include "room_library.h"
#include "thing_objects.h"
#include "room_data.h"
#include "slab_data.h"
#include "lvl_filesdk1.h"
#include "thing_stats.h"
#include "thing_creature.h"
#include "thing_list.h"
#include "creature_instances.h"
#include "creature_states_mood.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Wrappers registered with ports/sim_port.h's SimPort; config_rules.c
// reads the current level's map dimensions, owned by kfx_sim_state.h.
static int64_t config_reload_get_map_subtiles_x(void)
{
    return kfx_sim_state.map_subtiles_x;
}

static int64_t config_reload_get_map_subtiles_y(void)
{
    return kfx_sim_state.map_subtiles_y;
}

// Wrapper registered with ports/sim_port.h's SimPort; struct Dungeon
// is a kfx_sim-owned type.
static unsigned char get_player_color_idx_wrapper(PlayerNumber plyr_idx)
{
    return get_player_color_idx(plyr_idx);
}

// Wrappers registered with ports/sim_port.h's SimPort; kfx_sim_state
// is a kfx_sim-owned global.
static struct SlabSet *get_slabset_array(void)
{
    return kfx_sim_state.slabset;
}
static int64_t *get_slabset_num_ptr(void)
{
    return &kfx_sim_state.slabset_num;
}
static struct SlabObj *get_slabobjs_array(void)
{
    return kfx_sim_state.slabobjs;
}
static int64_t *get_slabobjs_idx_array(void)
{
    return kfx_sim_state.slabobjs_idx;
}
static int64_t *get_slabobjs_num_ptr(void)
{
    return &kfx_sim_state.slabobjs_num;
}
static void set_block_health(int64_t idx, int64_t val)
{
    kfx_sim_state.block_health[idx] = val;
}

// Wrapper registered with ports/sim_port.h's SimPort (see
// docs/refactor/todo/check-layering-symbol-level-blind-spot.md);
// level_strings is a kfx_sim array, so it needs a getter to be passed
// through a callback table.
static char **get_level_strings(void) { return level_strings; }


const struct SimPort kfx_sim_port = {
    .update_all_door_stats = &update_all_door_stats,
    .update_all_trap_draws_of_model = &update_all_trap_draws_of_model,
    .add_research_to_all_players = &add_research_to_all_players,
    .clear_research_for_all_players = &clear_research_for_all_players,
    .get_map_subtiles_x = &config_reload_get_map_subtiles_x,
    .get_map_subtiles_y = &config_reload_get_map_subtiles_y,
    .set_call_to_arms_graphics = &set_call_to_arms_graphics,
    .get_player_color_idx = &get_player_color_idx_wrapper,
    .get_slabset_array = &get_slabset_array,
    .get_slabset_num_ptr = &get_slabset_num_ptr,
    .get_slabobjs_array = &get_slabobjs_array,
    .get_slabobjs_idx_array = &get_slabobjs_idx_array,
    .get_slabobjs_num_ptr = &get_slabobjs_num_ptr,
    .set_block_health = &set_block_health,
    .reinitialise_rooms_of_kind = &reinitialise_rooms_of_kind,
    .recalculate_effeciency_for_rooms_of_kind = &recalculate_effeciency_for_rooms_of_kind,
    .find_and_load_lif_files = &find_and_load_lif_files,
    .find_and_load_lof_files = &find_and_load_lof_files,
    .thing_class_and_model_name = &thing_class_and_model_name,
    .remove_creature_lair = &remove_creature_lair,
    .update_creature_health_to_max = &update_creature_health_to_max,
    .update_relative_creature_health = &update_relative_creature_health,
    .do_to_players_all_creatures_of_model = &do_to_players_all_creatures_of_model,
    .do_to_all_things_of_class_and_model = &do_to_all_things_of_class_and_model,
    .recalculate_all_creature_digger_lists = &recalculate_all_creature_digger_lists,
    .update_speed_of_player_creatures_of_model = &update_speed_of_player_creatures_of_model,
    .creature_increase_available_instances = &creature_increase_available_instances,
    .process_job_stress_and_going_postal = &process_job_stress_and_going_postal,
    .setup_excess_creatures_to_leave_or_die = &setup_excess_creatures_to_leave_or_die,
    .get_level_strings = &get_level_strings,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
