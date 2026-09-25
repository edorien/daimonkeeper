/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file main.cpp
 * @author KeeperFX Team
 * @date 01 Aug 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "kfxmain.h"
#include "platform/PlatformManager.h"
#include "renderer/RendererManager.h"
#include "frontgui_stylesheet_test.h"
#include "frontgui_screens.h"
#include "frontgui_ingame_parchment.h"
#include "frontgui_style.h"
#include "gui/FrontendImGui.h"
#include "globals.h"
#include "bflib_sprite.h"
#include "thing_data.h"
#include "dungeon_data.h"
#include "dungeon_availability.h"
#include "creature_battle.h"
#include "player_data.h"
#include "actionpt.h"
#include "map_data.h"
#include "net_game.h"
#include "front_landview.h"

#include "bflib_coroutine.h"
#include "bflib_math.h"
#include "bflib_keybrd.h"
#include "bflib_inputctrl.h"
#include "bflib_datetm.h"
#include "bflib_sprfnt.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_sndlib.h"
#include "bflib_cpu.h"
#include "bflib_crash.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "bflib_guibtns.h"
#include "bflib_sound.h"
#include "config_sounds.h"
#include "bflib_mouse.h"
#include "bflib_mshandler.hpp"
#include "bflib_filelst.h"
#include "net_exchange_gameplay.h"
#include "net_lobby.h"
#include "net_matchmaking.h"
#include "net_resync.h"
#include "bflib_planar.h"

#include "ariadne_update.h"
#include "api.h"
#include "custom_sprites.h"
#include "custom_zip.h"
#include "sprite_lookup.h"
#include "matchmaking_config.h"
#include "version.h"
#include "front_simple.h"
#include "frontend.h"
#include "front_network.h"
#include "front_input.h"
#include "net_callbacks.h"
#include "net_main.h"
#include "console_cmd.h"
#include "lua_base.h"
#include "gui_draw.h"
#include "frontmenu_net.h"
#include "gui_parchment.h"
#include "gui_frontmenu.h"
#include "gui_msgs.h"
#include "gui_tooltips.h"
#include "render_overlay.h"
#include "scrcapt.h"
#include "vidmode.h"
#include "kjm_input.h"
#include "packets.h"
#include "config.h"
#include "config_slabsets.h"
#include "config_strings.h"
#include "config_campaigns.h"
#include "config_terrain.h"
#include "config_objects.h"
#include "config_magic.h"
#include "config_creature.h"
#include "config_mods.h"
#include "config_compp.h"
#include "config_effects.h"
#include "config_rules.h"
#include "lua_triggers.h"
#include "lua_cfg_funcs.h"
#include "script_hooks.h"
#include "lvl_script.h"
#include "lvl_script_lib.h"
#include "lvl_filesdk1.h"
#include "thing_list.h"
#include "player_instances.h"
#include "player_utils.h"
#include "config_players.h"
#include "player_computer.h"
#include "timer.h"
#include "game_heap.h"
#include "game_saves.h"
#include "game_campaign_progress.h" // reset_all_campaign_progress -- Phase E, docs/refactor/gui/05-campaign-progress-and-landview.md §3.5
#include "engine_render.h"
#include "cursor_tag.h"
#include "engine_lenses.h"
#include "engine_camera.h"
#include "local_camera.h"
#include "engine_arrays.h"
#include "engine_textures.h"
#include "engine_redraw.h"
#include "front_easter.h"
#include "front_highscore.h"
#include "front_lvlstats.h"
#include "game_callbacks.h"
#include "editor_callbacks.h" // docs/refactor/editor/01-entry-and-editor-session.md
#include "content_tools_callbacks.h" // docs/refactor/editor/fx-plans/03-content-editors-foundation.md §5
#include "content_tools.h"
#include "editor_journal_callbacks.h" // docs/refactor/editor/02-editing-toolbox.md §4
#include "kfx_editor.h"
#include "front_fmvids.h"
#include "thing_stats.h"
#include "thing_physics.h"
#include "thing_creature.h"
#include "thing_objects.h"
#include "thing_effects.h"
#include "thing_doors.h"
#include "thing_traps.h"
#include "room_library.h"
#include "thing_navigate.h"
#include "thing_shots.h"
#include "thing_factory.h"
#include "slab_data.h"
#include "room_data.h"
#include "room_entrance.h"
#include "room_util.h"
#include "map_columns.h"
#include "map_ceiling.h"
#include "map_events.h"
#include "map_utils.h"
#include "map_blocks.h"
#include "creature_control.h"
#include "creature_states.h"
#include "creature_jobs.h"
#include "creature_instances.h"
#include "creature_graphics.h"
#include "creature_states_combt.h"
#include "creature_states_mood.h"
#include "lens_api.h"
#include "light_data.h"
#include "magic_powers.h"
#include "power_process.h"
#include "power_hand.h"
#include "game_merge.h"
#include "gui_topmsg.h"
#include "gui_boxmenu.h"
#include "gui_soundmsgs.h"
#include "gui_frontbtns.h"
#include "frontmenu_ingame_tabs.h"
#include "frontmenu_ingame_evnt.h"
#include "sounds.h"
#include "sim_feedback.h"
#include "pathfinding_world.h"
#include "vidfade.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "game_legacy.h"
#include "room_list.h"
#include "steam_api.hpp"
#include "game_loop.h"
#include "main_game.h"
#include "game_session_loop.h"
#include "game_lifecycle.h"
#include "net_input_lag.h"
#include "moonphase.h"
#include "frontmenu_ingame_map.h"
#include "room_library.h"
#include <cstdint>

#ifdef FUNCTESTING
  #include "ftests/ftest.h"
#endif

#include "kfx_frontend_state.h"
#include "kfx_net_state.h"
#include "kfx_game_state.h"
#include "post_inc.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

// autostart_multiplayer_campaign/autostart_multiplayer_level/
// autostart_multiplayer_users_expected/force_player_num moved into
// kfx_config's struct StartupParameters (start_params) -- see
// config_keeperfx.h and docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md. default_loc_player moved to
// kfx_game/src/main_game.c; turns_per_second to kfx_sim_state; the
// remaining plain globals below moved into their owning library's state
// struct (kfx_frontend_state/kfx_net_state/kfx_game_state/kfx_render_state)
// during the src/ -> src/kfx_* refactor.

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

void init_keeper(void)
{
    SYNCDBG(8,"Starting");
    engine_init();
    init_fp_td_animation_conversion_tables();
    init_colours();
    init_spiral_steps();
    init_key_to_strings();
    // Load configs which may have per-campaign part, and even be modified within a level
    recheck_all_mod_exist();
    init_custom_sprites(SPRITE_LAST_LEVEL);
    load_stats_files();
    check_and_auto_fix_stats();
    init_creature_scores();
    init_top_texture_to_cube_table();
    kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
    kfx_config_state.atmos_sound_frequency = 800;
    poly_pool_end = &poly_pool[sizeof(poly_pool)-128];
    lbDisplay.GlassMap = pixmap.ghost;
    RendererSetDrawColour(kfx_sim_state.colours[15][15][15]);
    kfx_net_state.comp_player_aggressive  = (comp_player_conf.player_assist_default == comp_player_conf.computer_assist_types[0]);
    kfx_net_state.comp_player_defensive   = (comp_player_conf.player_assist_default == comp_player_conf.computer_assist_types[1]);
    kfx_net_state.comp_player_construct   = (comp_player_conf.player_assist_default == comp_player_conf.computer_assist_types[2]);
    kfx_net_state.comp_player_creatrsonly = (comp_player_conf.player_assist_default == comp_player_conf.computer_assist_types[3]);
    kfx_sim_state.creatures_tend_imprison = 0;
    kfx_sim_state.creatures_tend_flee = 0;
    kfx_sim_state.operation_flags |= GOF_ShowPanel;
    kfx_sim_state.view_mode_flags |= (GNFldD_StatusPanelDisplay | GNFldD_RoomFlameProcessing);
    init_censorship();
    SYNCDBG(9,"Finished");
}

/**
 * Initial video setup - loads only most important files to show startup screens.
 */
TbBool initial_setup(void)
{
    SYNCDBG(6,"Starting");
    // setting this will force video mode change, even if previous one is same
    MinimalResolutionSetup = true;
    // Set size of static textures buffer
    game_load_files[1].SLength = max((uint64_t)TEXTURE_BLOCKS_STAT_COUNT_A*block_dimension*block_dimension,(uint64_t)LANDVIEW_MAP_WIDTH*LANDVIEW_MAP_HEIGHT);
    if (LbDataLoadAllV2(game_load_files))
    {
        ERRORLOG("Unable to load game_load_files");
        return false;
    }
    load_pointer_file(0);
    update_screen_mode_data(320, 200);
    clear_game();
    RendererAddDrawFlags(0x4000u);
    return true;
}

// Wrappers registered with bflib_inputctrl.h's InputFocusPredicates (see
// docs/refactor/stage-02-decouple-bflib.md); bflib_inputctrl.cpp can't read
// struct Game directly.
static TbBool is_game_paused(void)
{
    return (kfx_sim_state.operation_flags & GOF_Paused) != 0;
}

static TbBool is_possession_mode_active(void)
{
    return (get_my_player()->view_type == PVT_CreatureContrl) && ((kfx_sim_state.view_mode_flags & GNFldD_CreaturePasngr) == 0);
}

static TbBool is_packet_load_enabled(void)
{
    return kfx_net_state.packet_load_enable != 0;
}

// Wrappers registered with bflib_sndlib.h's SoundStateCallbacks (see
// docs/refactor/stage-13-enforce-and-document.md); bflib_sndlib.cpp/
// sound_manager.cpp can't read struct Game/kfx_*_state directly.
static char *get_music_track(void)
{
    return &kfx_game_state.music_track;
}

static char *get_music_fname(void)
{
    return kfx_game_state.music_fname;
}

static int64_t get_frame_skip(void)
{
    return kfx_net_state.frame_skip;
}

static TbBool get_easter_eggs_enabled(void)
{
    return kfx_sim_state.easter_eggs_enabled;
}

static int64_t get_last_level(void)
{
    return kfx_render_state.last_level;
}

static int64_t get_creature_model_count(void)
{
    return kfx_config_state.conf.crtr_conf.model_count;
}

static struct CreatureSounds *get_creature_sounds(int64_t crmodel)
{
    return &kfx_config_state.conf.crtr_conf.creature_sounds[crmodel];
}

static const struct ModConfigItem *get_mods_after_map(void)
{
    return mods_conf.after_map_item;
}

static int64_t get_mods_after_map_count(void)
{
    return mods_conf.after_map_cnt;
}

static const struct ModConfigItem *get_mods_after_campaign(void)
{
    return mods_conf.after_campaign_item;
}

static int64_t get_mods_after_campaign_count(void)
{
    return mods_conf.after_campaign_cnt;
}

static const struct ModConfigItem *get_mods_after_base(void)
{
    return mods_conf.after_base_item;
}

static int64_t get_mods_after_base_count(void)
{
    return mods_conf.after_base_cnt;
}

static uint32_t *get_sound_random_seed(void)
{
    return &kfx_sim_state.sound_random_seed;
}

static uint32_t *get_unsync_random_seed(void)
{
    return &kfx_sim_state.unsync_random_seed;
}

// Wrapper registered with bflib_sndlib.h's SoundStateCallbacks; config.c's
// creature_desc[] is a plain array, not a function, so it needs a getter
// to be passed through the callback table. prepare_file_path/_mod/_buf,
// prepare_file_fmtpath, creature_code_name, and thing_is_invalid are
// passed by direct reference below -- their real (config.c/
// config_creature.c/thing_data.c) signatures already match the callback
// fields exactly. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
static const struct NamedCommand *get_creature_desc(void)
{
    return creature_desc;
}

// Wrappers registered with matchmaking_config.h's MatchmakingConfigCallbacks;
// config_keeperfx.c's MATCHMAKING_SERVER config var can't reach
// net_matchmaking.h's matchmaking_enabled/matchmaking_ws_url plain
// extern variables directly (kfx_net is above kfx_config).
static void matchmaking_config_set_enabled(TbBool enabled)
{
    matchmaking_enabled = enabled;
}

static const char *matchmaking_config_get_ws_url(void)
{
    return matchmaking_ws_url;
}

// Wrapper registered with config.h's ConfigReloadCallbacks (see
// docs/refactor/stage-13-enforce-and-document.md); lvl_filesdk1.c can't
// reach struct LightsShadows (kfx_game-owned) directly.
static void clear_subtiles_lightness_wrapper(void)
{
    clear_subtiles_lightness(&lish);
}

// Wrappers registered with config.h's ConfigReloadCallbacks; config_creature.c
// needs a handful of struct Thing/struct CreatureControl fields without
// either type visible by value.
static ThingModel config_reload_get_thing_model(const struct Thing *thing)
{
    return thing->model;
}
static ThingClass config_reload_get_thing_class_id(const struct Thing *thing)
{
    return thing->class_id;
}
static PlayerNumber config_reload_get_thing_owner(const struct Thing *thing)
{
    return thing->owner;
}
static uint64_t config_reload_get_thing_creation_turn(const struct Thing *thing)
{
    return thing->creation_turn;
}
static int64_t config_reload_get_thing_index(const struct Thing *thing)
{
    return thing->index;
}
static unsigned char config_reload_get_creature_blood_type(const struct Thing *creatng)
{
    return creature_control_get_from_thing(creatng)->blood_type;
}
static char *config_reload_get_creature_name_buffer(const struct Thing *creatng)
{
    return creature_control_get_from_thing(creatng)->creature_name;
}

// Wrappers registered with config.h's ConfigReloadCallbacks; config_rules.c
// reads the current level's map dimensions, owned by kfx_sim_state.h.
static int64_t config_reload_get_map_subtiles_x(void)
{
    return kfx_sim_state.map_subtiles_x;
}

static int64_t config_reload_get_map_subtiles_y(void)
{
    return kfx_sim_state.map_subtiles_y;
}

// Wrapper registered with render_overlay.h's RenderOverlayCallbacks;
// gui_parchment.c owns parchment_loaded.
static TbBool is_parchment_loaded(void)
{
    return parchment_loaded;
}

// Wrapper registered with sim_feedback.h's SimFeedbackCallbacks;
// engine_lenses.c owns lens_mode.
static unsigned char get_lens_mode(void)
{
    return lens_mode;
}

// Wrapper registered with sim_feedback.h's SimFeedbackCallbacks;
// gui_tooltips.c owns tool_tip_box.
static void hide_tooltip(void)
{
    clear_flag(tool_tip_box.flags, TTip_Visible);
}

// Wrapper registered with sim_feedback.h's SimFeedbackCallbacks;
// frontmenu_ingame_evnt.c owns TimerTurns.
static void set_timer_turns(uint64_t turns)
{
    TimerTurns = turns;
}

// Wrapper registered with custom_zip.h's MapZipCallbacks; resolves the
// already-formatted "mapNNNNN.zip" filename to a full path.
static char *prepare_map_zip_path(LevelNumber lvnum, const char *fname)
{
    return prepare_file_path(get_level_fgroup(lvnum), fname);
}

// Wrapper registered with config.h's ConfigReloadCallbacks; scrcapt.h's
// screenshot_format is a kfx_render-owned global.
static void set_screenshot_format(unsigned char val)
{
    screenshot_format = val;
}

static unsigned char get_screenshot_format(void)
{
    return screenshot_format;
}

// Wrapper registered with config.h's ConfigReloadCallbacks; engine_redraw.h's
// smooth_on is a kfx_render-owned global. Also forced on unconditionally by
// process_cmdline_overrides() when the "-vidsmooth" launch flag was given
// (Clo_VidSmooth) -- see that flag's own comment.
static void set_vid_smooth(TbBool val)
{
    smooth_on = val;
}

// Wrapper registered with config.h's ConfigReloadCallbacks; power_hand.h's
// global_hand_scale is a kfx_sim-owned global.
static void set_hand_scale(double val)
{
    global_hand_scale = val;
}

static double get_hand_scale(void)
{
    return global_hand_scale;
}

// Wrapper registered with config.h's ConfigReloadCallbacks; vidmode.h's
// base_mouse_sensitivity is a kfx_render-owned global with an existing free
// setter (set_base_mouse_sensitivity) but no getter to pair it with.
static int64_t get_base_mouse_sensitivity(void)
{
    return base_mouse_sensitivity;
}

// Wrapper registered with config.h's ConfigReloadCallbacks; struct Room is
// a kfx_sim-owned type, so the ->kind read happens here rather than in
// config_creature.c.
static RoomKind get_room_kind_thing_is_on(const struct Thing *creatng)
{
    struct Room *room = get_room_thing_is_on(creatng);
    if (room_is_invalid(room))
        return RoK_NONE;
    return room->kind;
}

// Wrapper registered with config.h's ConfigReloadCallbacks; struct Dungeon
// is a kfx_sim-owned type.
static unsigned char get_player_color_idx_wrapper(PlayerNumber plyr_idx)
{
    return get_player_color_idx(plyr_idx);
}

// Wrappers registered with config.h's ConfigReloadCallbacks; kfx_sim_state
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
static ThingModel get_player_special_digger(PlayerNumber plyr_idx)
{
    return get_player(plyr_idx)->special_digger;
}
static void set_player_special_digger(PlayerNumber plyr_idx, ThingModel model)
{
    get_player(plyr_idx)->special_digger = model;
}

// Wrappers registered with config.h's ConfigReloadCallbacks (see
// docs/refactor/todo/check-layering-symbol-level-blind-spot.md);
// my_player_number is a plain kfx_sim global, and the *_func_type/
// *_func_commands/level_strings entries are kfx_sim arrays -- both need
// a getter to be passed through a callback table. Everything else this
// batch of ConfigReloadCallbacks fields backs is passed by direct
// reference in config_reload_callbacks_impl below, since their real
// (kfx_sim) signatures already match the callback fields exactly.
static unsigned char get_my_player_number(void) { return my_player_number; }
static const struct NamedCommand *get_computer_process_func_type(void) { return computer_process_func_type; }
static const struct NamedCommand *get_computer_check_func_type(void) { return computer_check_func_type; }
static const struct NamedCommand *get_computer_event_func_type(void) { return computer_event_func_type; }
static const struct NamedCommand *get_computer_event_test_func_type(void) { return computer_event_test_func_type; }
static const struct NamedCommand *get_creature_instances_func_type(void) { return creature_instances_func_type; }
static const struct NamedCommand *get_creature_instances_validate_func_type(void) { return creature_instances_validate_func_type; }
static const struct NamedCommand *get_creature_instances_search_targets_func_type(void) { return creature_instances_search_targets_func_type; }
static const struct NamedCommand *get_creature_job_player_assign_func_type(void) { return creature_job_player_assign_func_type; }
static const struct NamedCommand *get_creature_job_player_check_func_type(void) { return creature_job_player_check_func_type; }
static const struct NamedCommand *get_creature_job_coords_check_func_type(void) { return creature_job_coords_check_func_type; }
static const struct NamedCommand *get_creature_job_coords_assign_func_type(void) { return creature_job_coords_assign_func_type; }
static const struct NamedCommand *get_process_func_commands(void) { return process_func_commands; }
static const struct NamedCommand *get_cleanup_func_commands(void) { return cleanup_func_commands; }
static const struct NamedCommand *get_move_from_slab_func_commands(void) { return move_from_slab_func_commands; }
static const struct NamedCommand *get_move_check_func_commands(void) { return move_check_func_commands; }
static char **get_level_strings(void) { return level_strings; }
static void set_speech_queue_limit(int64_t limit) { g_speech_queue_limit = limit; }

// Wrapper registered with kfx_config's NetCallbacks (net_callbacks.h);
// net_exchange_common.c can't reach kfx_apploop's host_packet_received
// directly. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
static void set_host_packet_received(long double value) { host_packet_received = value; }
static char net_callbacks_get_frontend_alliances(void) { return frontend_alliances; }
static void game_callbacks_set_frontend_alliances(char alliances) { frontend_alliances = alliances; }

// Wrapper registered with kfx_config's RenderOverlayCallbacks
// (render_overlay.h); engine_render.c can't reach kfx_apploop's
// interpolate_time directly. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
static double get_interpolate_time(void) { return interpolate_time; }

// Wrapper registered with kfx_config's SimFeedbackCallbacks
// (sim_feedback.h); map_events.c can't reach kfx_frontend's
// event_button_info[] directly. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
static const struct EventTypeInfo *get_event_button_info(EventKind evkind) { return &event_button_info[evkind]; }

// Wrappers registered with sim_feedback.h's SimFeedbackCallbacks; kfx_net
// (and kfx_game/kfx_frontend/kfx_apploop) own this session state.
static TbBool get_packet_load_enable(void)
{
    return kfx_net_state.packet_load_enable;
}

static PlayerNumber get_local_plyr_idx(void)
{
    return kfx_net_state.local_plyr_idx;
}

static int64_t get_input_lag_turns(void)
{
    return kfx_net_state.input_lag_turns;
}

static void set_active_players_count(int64_t count)
{
    kfx_net_state.active_players_count = count;
}
static int64_t get_isometric_view_zoom_level(void)
{
    return kfx_net_state.packet_save_head.isometric_view_zoom_level;
}
static int64_t get_frontview_zoom_level(void)
{
    return kfx_net_state.packet_save_head.frontview_zoom_level;
}
static TbBool get_player_exists_flag(PlayerNumber plyr_idx)
{
    return flag_is_set(kfx_net_state.packet_save_head.players_exist, to_flag(plyr_idx));
}
static TbBool get_player_comp_flag(PlayerNumber plyr_idx)
{
    return flag_is_set(kfx_net_state.packet_save_head.players_comp, to_flag(plyr_idx));
}
static void increment_active_players_count(void)
{
    kfx_net_state.active_players_count++;
}
static LevelNumber sim_feedback_get_loaded_level_number(void)
{
    return kfx_sim_state.loaded_level_number;
}
static LevelNumber sim_feedback_get_selected_level_number(void)
{
    return get_selected_level_number();
}
static LevelNumber sim_feedback_get_level_number(void)
{
    return get_level_number();
}
static GameTurn sim_feedback_get_play_gameturn(void)
{
    return kfx_game_state.play_gameturn;
}
static int64_t get_intralvl_next_level(void)
{
    return intralvl.next_level;
}
static void clear_intralvl_next_level(void)
{
    intralvl.next_level = 0;
}

// Non-variadic wrapper for struct SimFeedbackCallbacks (see
// docs/refactor/stage-06-kfx-sim.md) -- show_onscreen_msg() itself is
// printf-style, which a plain C function pointer can't express; kfx_sim's
// one call site already formats its own message before calling through.
static TbBool show_onscreen_msg_plain(int64_t nturns, const char *msg)
{
    return show_onscreen_msg(nturns, "%s", msg);
}

// Same reasoning as show_onscreen_msg_plain above -- targeted_message_add()
// is printf-style.
static void targeted_message_add_plain(char msg_type, PlayerNumber plyr_idx, PlayerNumber target_idx, uint64_t timeout, const char *msg)
{
    targeted_message_add(msg_type, plyr_idx, target_idx, timeout, "%s", msg);
}

// Wrapper functions for struct RenderOverlayCallbacks (see
// docs/refactor/stage-07-kfx-render.md) -- bundle multiple gui/frontend
// calls together where engine_redraw.c always invoked them as one unit,
// and provide get/set accessors for globals a plain callback can't
// express directly.
static void render_overlay_load_and_redraw_minimal_overhead_view(void)
{
    load_parchment_file();
    redraw_minimal_overhead_view();
}

static void render_overlay_set_parchment_loaded(int64_t val)
{
    parchment_loaded = val;
}

static TbBool render_overlay_game_is_busy_doing_gui(void)
{
    return game_is_busy_doing_gui() != 0;
}

static TbBool render_overlay_game_is_busy_doing_gui_string_input(void)
{
    return game_is_busy_doing_gui_string_input() != 0;
}

static TbBool sim_feedback_is_left_button_held(void)
{
    return left_button_held;
}

// Wrapper functions for struct PathfindingWorldCallbacks (see
// docs/refactor/stage-06a-ariadne-pathfinding-interface.md, Track 2) --
// return-type mismatches against the real kfx_sim functions (PlayerNumber
// vs. long, TbBool vs. long) and the handful of raw field reads that don't
// have an existing accessor need a thin wrapper; everything else binds
// directly to its real kfx_sim function in the table below.
static MapSubtlCoord pathfinding_world_get_map_size_x(void)
{
    return kfx_sim_state.map_subtiles_x;
}
static MapSubtlCoord pathfinding_world_get_map_size_y(void)
{
    return kfx_sim_state.map_subtiles_y;
}
static unsigned char pathfinding_world_map_block_flags(const struct Map *mapblk)
{
    return mapblk->flags;
}
static SlabKind pathfinding_world_slabmap_block_kind(const struct SlabMap *slb)
{
    return slb->kind;
}
static PlayerNumber pathfinding_world_slabmap_owner(const struct SlabMap *slb)
{
    return (PlayerNumber)slabmap_owner(slb);
}
static TbBool pathfinding_world_thing_in_wall_at(const struct Thing *thing, const struct Coord3d *pos)
{
    return thing_in_wall_at(thing, pos) != 0;
}
static TbBool pathfinding_world_door_is_locked(const struct Thing *doortng)
{
    return doortng->door.is_locked != 0;
}
static PlayerNumber pathfinding_world_thing_get_owner(const struct Thing *thing)
{
    return thing->owner;
}
static struct Coord3d pathfinding_world_thing_get_position(const struct Thing *thing)
{
    return thing->mappos;
}
static void pathfinding_world_thing_set_position(struct Thing *thing, const struct Coord3d *pos)
{
    thing->mappos = *pos;
}
static int64_t pathfinding_world_thing_get_move_angle(const struct Thing *thing)
{
    return thing->move_angle_xy;
}
static void pathfinding_world_thing_set_move_angle(struct Thing *thing, int64_t angle)
{
    thing->move_angle_xy = angle;
}
static int64_t pathfinding_world_thing_get_index(const struct Thing *thing)
{
    return thing->index;
}
static int64_t pathfinding_world_thing_get_clipbox_size(const struct Thing *thing)
{
    return thing->clipbox_size_xy;
}
static struct Navigation *pathfinding_world_creature_get_navigation(struct Thing *creatng)
{
    return &creature_control_get_from_thing(creatng)->navi;
}
static struct Ariadne *pathfinding_world_creature_get_ariadne_state(struct Thing *creatng)
{
    return &creature_control_get_from_thing(creatng)->arid;
}
static TbBool pathfinding_world_creature_steps_into_toxic_terrain(struct Thing *thing, const struct Coord3d *pos)
{
    return !flag_is_set(thing->alloc_flags, TAlF_IsControlled)
        && !terrain_toxic_for_creature_at_position(thing, thing->mappos.x.stl.num, thing->mappos.y.stl.num)
        && terrain_toxic_for_creature_at_position(thing, pos->x.stl.num, pos->y.stl.num);
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
static struct Around pathfinding_world_get_small_around(SmallAroundIndex n)
{
    return small_around[n];
}
static SmallAroundIndex pathfinding_world_get_small_around_length(void)
{
    return SMALL_AROUND_LENGTH;
}
static MapSubtlCoord pathfinding_world_get_map_size_z(void)
{
    return map_subtiles_z;
}
static int64_t pathfinding_world_get_owner_player_navigating(void)
{
    return owner_player_navigating;
}
static void pathfinding_world_set_owner_player_navigating(int64_t plyr_idx)
{
    owner_player_navigating = plyr_idx;
}
static int64_t pathfinding_world_get_nav_thing_can_travel_over_lava(void)
{
    return nav_thing_can_travel_over_lava;
}
static void pathfinding_world_set_nav_thing_can_travel_over_lava(int64_t can_travel)
{
    nav_thing_can_travel_over_lava = can_travel;
}
static int64_t pathfinding_world_get_nav_thing_is_flying(void)
{
    return nav_thing_is_flying;
}
static void pathfinding_world_set_nav_thing_is_flying(int64_t is_flying)
{
    nav_thing_is_flying = is_flying;
}
static TbBool pathfinding_world_thing_is_flying(const struct Thing *thing)
{
    return flag_is_set(thing->movement_flags, TMvF_Flying);
}

static TbBool sim_feedback_is_best_roomspace_key_pressed(void)
{
    return is_game_key_pressed(Gkey_BestRoomSpace, false, true) != 0;
}

static TbBool sim_feedback_is_square_roomspace_key_pressed(void)
{
    return is_game_key_pressed(Gkey_SquareRoomSpace, false, true) != 0;
}

static TbBool sim_feedback_is_roomspace_incsize_key_pressed(void)
{
    return is_game_key_pressed(Gkey_RoomSpaceIncSize, false, true) != 0;
}

static TbBool sim_feedback_is_roomspace_decsize_key_pressed(void)
{
    return is_game_key_pressed(Gkey_RoomSpaceDecSize, false, true) != 0;
}

static TbBool sim_feedback_is_sell_trap_on_subtile_key_pressed(void)
{
    return is_game_key_pressed(Gkey_SellTrapOnSubtile, false, true) != 0;
}

static void sim_feedback_set_room_type_highlighted(char room_kind)
{
    gui_room_type_highlighted = room_kind;
}

static void sim_feedback_set_visible_event_idx(EventIndex evidx)
{
    my_visible_event_idx = evidx;
}

static void sim_feedback_clear_all_event_button_states(void)
{
    memset(my_event_button_state, 0, EVENTS_COUNT);
}

static void sim_feedback_clear_event_button_state(EventIndex evidx)
{
    my_event_button_state[evidx] = 0;
}

static void sim_feedback_mark_event_button_read(EventIndex evidx)
{
    my_event_button_state[evidx] |= EvBtnS_Read;
}

static TbBool sim_feedback_is_battle_creature_over_active(void)
{
    return battle_creature_over > 0;
}

static void sim_feedback_hide_map_volume_box(void)
{
    map_volume_box.visible = 0;
}

static void sim_feedback_reset_box_lag_compensation(void)
{
    box_lag_compensation_x = 0;
    box_lag_compensation_y = 0;
}

static int64_t render_overlay_get_main_menu_width(void)
{
    struct GuiMenu *gmnu = get_active_menu(menu_id_to_number(GMnu_MAIN));
    return gmnu->width;
}

// draw_gui_panel_sprite_left is itself a macro (expands to
// draw_gui_panel_sprite_left_player(...,my_player_number)), so this
// wrapper needs a distinct name.
static void render_overlay_draw_gui_panel_sprite_left(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx)
{
    draw_gui_panel_sprite_left(x, y, units_per_px, spridx);
}

static void render_overlay_sync_cheat_box_3_active_option(CrInstance active_instance_id)
{
    if (!gui_box_is_not_valid(kfx_frontend_local.gui_cheat_box_3))
    {
        struct GuiBoxOption* guop = kfx_frontend_local.gui_cheat_box_3->optn_list;
        while (guop->label[0] != '!')
        {
            guop->active = (active_instance_id == guop->cb_param1);
            guop++;
        }
    }
}

static TbBool render_overlay_cheat_or_menu_window_active(void)
{
    return cheat_menu_is_active() || a_menu_window_is_active();
}

static void render_overlay_set_winfont(void)
{
    LbTextSetFont(winfont);
}

static int64_t render_overlay_get_status_panel_width(void)
{
    // The ImGui HUD composites over a full-screen 3D view -- it does not
    // inset the engine window (and a horizontal HUD layout could not be
    // expressed as a left inset at all). The classic sprite GUI keeps its
    // inset.
    if (!ingame_gui_use_classic_hud())
        return 0;
    return status_panel_width;
}

static void render_overlay_draw_debug_overlays(void)
{
    // Phase 2 (docs/refactor/ingame-gui/03-debug-overlays-and-box-menus.md):
    // when the ImGui in-game HUD is active, ingame_debug_overlays_frame()
    // (frontgui_ingame_debug.cpp) draws all of these instead, from the
    // FrontendImGuiFrame submission -- same *_enabled() gates. Only reached
    // for the classic HUD (ingame_gui_use_classic_hud()).
    if (!ingame_gui_use_classic_hud())
        return;
    if (bonus_timer_enabled())
    {
        draw_bonus_timer();
    }
    else if (script_timer_enabled())
    {
        draw_script_timer(kfx_game_state.script_timer_player, kfx_game_state.script_timer_id, kfx_game_state.script_timer_limit, kfx_game_state.timer_real);
    }
    if (gameturn_timer_enabled())
    {
        draw_gameturn_timer();
    }
    if (display_variable_enabled())
    {
        draw_script_variable(kfx_game_state.script_variables[0].variable_player, kfx_game_state.script_variables[0].value_type, kfx_game_state.script_variables[0].value_id, kfx_game_state.script_variables[0].variable_target, kfx_game_state.script_variables[0].variable_target_type);
    }
    if (timer_enabled())
    {
        draw_timer();
    }
    if (frametime_enabled())
    {
        draw_frametime();
    }
    if (debug_display_network_stats != 0)
    {
        draw_network_stats();
    }
    if (consolelog_enabled())
    {
        draw_consolelog();
    }
}

static TbBool render_overlay_bonus_script_or_variable_overlay_active(void)
{
    return bonus_timer_enabled() || script_timer_enabled() || display_variable_enabled();
}

static int64_t render_overlay_get_battle_creature_over(void)
{
    return battle_creature_over;
}

static int64_t render_overlay_get_map_diagonal_length(void)
{
    return MapDiagonalLength;
}

static TbBool render_overlay_get_unpausing_in_progress(void)
{
    return unpausing_in_progress;
}

// Wrapper functions for struct NetCallbacks (see
// src/kfx_config/include/net_callbacks.h and
// docs/refactor/stage-08-kfx-net.md).
static void net_callbacks_enter_net_session_screen(void)
{
    frontend_set_state(FeSt_NET_SESSION);
}

static void net_callbacks_set_lobby_button_labels(TbBool is_lan)
{
    if (is_lan) {
        frontend_button_info[11].capstr_idx = GUIStr_MnuLanLobby;
        frontend_button_info[12].capstr_idx = GUIStr_MnuLanLobbies;
    } else {
        frontend_button_info[11].capstr_idx = GUIStr_MnuOnlineLobby;
        frontend_button_info[12].capstr_idx = GUIStr_MnuOnlineLobbies;
    }
}

static unsigned char net_callbacks_get_default_tag_mode(void)
{
    // Read keeperfx_ui_config directly rather than a kfx_sim_state copy --
    // clear_complete_game() memsets the whole of kfx_sim_state (including
    // any such copy) once at startup, right after setup_game() would have
    // populated it, permanently zeroing it for the rest of the process.
    // keeperfx_ui_config is kfx_config's own struct and isn't touched by
    // that reset.
    return keeperfx_ui_config.default_tag_mode;
}

static TbBool net_callbacks_is_frontend_starting_mp_level(void)
{
    return frontend_menu_state == FeSt_START_MPLEVEL;
}

static TbBool net_callbacks_is_frontend_at_initial_state(void)
{
    return frontend_menu_state == FeSt_INITIAL;
}

static TbBool net_callbacks_frontnet_service_selected(int64_t service)
{
    return frontnet_service_selected((enum FrontendNetService)service);
}

static void net_callbacks_clear_player_lightning_palette(struct PlayerInfo *player)
{
    PaletteSetUserPalette(player->user_id, engine_palette);
    get_player_user_state(player)->additional_flags &= ~UsrAF_LightningPaletteIsActive;
}

static TbBool net_callbacks_lua_script_active(void)
{
    return Lvl_script != NULL;
}

// Wrapper functions for struct GameCallbacks (see
// src/kfx_config/include/game_callbacks.h and
// docs/refactor/stage-09-kfx-game.md).
static TbBool game_callbacks_is_fe_computer_players_active(void)
{
    return fe_computer_players != 0;
}

static void game_callbacks_set_timer_turns(uint64_t value)
{
    TimerTurns = value;
}

static void game_callbacks_toggle_debug_network_stats(void)
{
    debug_display_network_stats = (debug_display_network_stats != 0) ? 0 : 1;
}

static TbBool game_callbacks_toggle_tooltip_land_coord(void)
{
    tool_tip_dbg.land_coord = !tool_tip_dbg.land_coord;
    return tool_tip_dbg.land_coord;
}

static void game_callbacks_get_high_score_entry(char *dest, size_t dest_size)
{
    snprintf(dest, dest_size, "%s", high_score_entry);
}

static void game_callbacks_set_high_score_entry(const char *name)
{
    snprintf(high_score_entry, sizeof(high_score_entry), "%s", name);
}

// docs/refactor/editor/01-entry-and-editor-session.md §0/F11 -- kfx_editor
// ranks above kfx_apploop, so it can't register its own ImGui-frame
// callback the way FrontendImGuiFrame itself is registered just below;
// main.cpp (the composition root, the one file allowed to #include every
// layer) wraps the single existing callback instead. editor_frame() no-ops
// unless editor_is_active().
static void app_imgui_frame(void)
{
    FrontendImGuiFrame();
    editor_frame();
}

/**
 * Displays 'legal' screens, intro and initializes basic game data.
 * If true is returned, then all files needed for startup were loaded,
 * and there should be the loading screen visible.
 * @return Returns true on success, false on error which makes the
 *   gameplay impossible (usually files loading failure).
 * @note The current screen resolution at end of this function may vary.
 */

int64_t setup_game(void)
{
  struct CPU_INFO cpu_info; // CPU status variable
  int64_t result;
  // Do only a very basic setup
  cpu_detect(&cpu_info);
  SYNCMSG("CPU %s type %" PRId64 " family %" PRId64 " model %" PRId64 " stepping %" PRId64 " features %08" PRIx64,cpu_info.vendor,
      (int64_t)cpu_get_type(&cpu_info),(int64_t)cpu_get_family(&cpu_info),(int64_t)cpu_get_model(&cpu_info),
      (int64_t)cpu_get_stepping(&cpu_info),(uint64_t)(cpu_info.feature_edx));
  if (cpu_info.BrandString)
  {
      SYNCMSG("%s", &cpu_info.brand[0]);
  }
  SYNCMSG("Build image base: %p", PlatformManager_GetImageBase());
  SYNCMSG("Operating System: %s", PlatformManager_GetOSVersion());

  const auto wine_version = PlatformManager_GetWineVersion();
  if (wine_version) {
        SYNCMSG("Running on Wine v%s", wine_version);
        is_running_under_wine = true;
        const auto wine_host = PlatformManager_GetWineHost();
        SYNCMSG("Wine Host: %s", wine_host);
  }

  // Enable features that require more than 32 megs of memory
  features_enabled |= Ft_HiResCreatr;
  // Enable features that require more than 16 megs of memory
  features_enabled |= Ft_EyeLens;
  features_enabled |= Ft_HiResVideo;
  features_enabled |= Ft_BigPointer;
  features_enabled |= Ft_AdvAmbSound;

  // Default feature settings (in case the options are absent from keeperfx.cfg)
  features_enabled &= ~Ft_FreezeOnLoseFocus; // don't freeze the game, if the game window loses focus
  features_enabled &= ~Ft_UnlockCursorOnPause; // don't unlock the mouse cursor from the window, if the user pauses the game
  features_enabled |= Ft_LockCursorInPossession; // lock the mouse cursor to the window, when the user enters possession mode (when the cursor is already unlocked)
  features_enabled |= Ft_RelativeMouseMode; // use SDL relative ("raw") mouse mode; set RELATIVE_MOUSE_MODE=OFF for the grab-and-warp scheme
  features_enabled &= ~Ft_PauseMusicOnGamePause; // don't pause the music, if the user pauses the game
  features_enabled &= ~Ft_MuteAudioOnLoseFocus; // don't mute the audio, if the game window loses focus
  if (start_params.skip_heart_zoom) {
    features_enabled |= Ft_SkipHeartZoom;
  } else {
    features_enabled &= ~Ft_SkipHeartZoom;
  }
  features_enabled &= ~Ft_DisableCursorCameraPanning; // don't disable cursor camera panning
  features_enabled |= Ft_DeltaTime; // enable delta time
  features_enabled |= Ft_NoCdMusic; // use music files (OGG) rather than CD music

  // Reserve the video-mode table's index 0 as the INVALID sentinel before any
  // config parsing can call LbRegisterVideoModeString() (INGAME_RES, case 7)
  // -- LbScreenInitialize() itself doesn't run until setup_screen_mode_zero()
  // much later, and without this, a config-parsed custom resolution would be
  // the table's actual first entry, landing on index 0 itself and being
  // silently rejected by every "mode > 0" caller (screen_vidmode would then
  // stay stuck at its compiled default across every restart).
  LbRegisterDefaultVideoModesIfNeeded();

  // ConfigReloadCallbacks must be wired up before load_configuration() runs,
  // not after: config_keeperfx.c's own switch-case parser (INGAME_RES/
  // POINTER_SENSITIVITY/VID_SMOOTHING/... -- every case that reaches through
  // config_reload_callbacks->X() to apply what it just parsed) calls it
  // *during* load_configuration() itself. Wiring this up later than that
  // point (as it used to be, far below in this same function) meant every
  // one of those setters silently invoked the do-nothing default/noop
  // implementation the whole time load_configuration() ran -- confirmed
  // live for INGAME_RES specifically: keeperfx.cfg correctly held
  // INGAME_RES=1920x1080x32, case 7 parsed it with no warning logged, but
  // set_screen_vidmode() never actually reached vidmode.c's real
  // implementation, so screen_vidmode stayed at its compiled 640x480
  // default across every single restart, no matter what was saved. This
  // struct is a pure function-pointer table (every entry a plain
  // already-declared free function, no captured local state), so moving
  // its wiring earlier changes nothing about what it does -- only when.
  static const struct ConfigReloadCallbacks config_reload_callbacks_impl = {
      &update_room_tab_to_config, &update_trap_tab_to_config, &update_powers_tab_to_config,
      &update_creatr_model_activities_list,
      &update_all_door_stats, &update_all_trap_draws_of_model,
      &add_research_to_all_players, &clear_research_for_all_players,
      &panel_map_update, &update_panel_color_player_color, &setup_panel_colors,
      &reset_panel_map_background_cache,
      &clear_subtiles_lightness_wrapper,
      &get_function_idx,
      &config_reload_get_map_subtiles_x, &config_reload_get_map_subtiles_y,
      &thing_is_workshop_crate,
      &get_wealth_size_of_gold_hoard_model,
      &set_call_to_arms_graphics,
      &set_screen_vidmode, &get_screen_vidmode, &set_base_mouse_sensitivity, &get_base_mouse_sensitivity,
      &thing_is_creature_digger, &creature_is_for_dungeon_diggers_list,
      &config_reload_get_thing_model, &config_reload_get_thing_class_id, &config_reload_get_thing_owner,
      &config_reload_get_thing_creation_turn, &config_reload_get_thing_index,
      &config_reload_get_creature_blood_type, &config_reload_get_creature_name_buffer,
      &get_slabmap_for_subtile, &slabmap_owner,
      &thing_create_thing, &thing_create_thing_adv,
      &set_screenshot_format,
      &get_screenshot_format,
      &set_vid_smooth,
      &set_hand_scale,
      &get_hand_scale,
      &get_room_kind_thing_is_on,
      &get_player_color_idx_wrapper,
      &get_slabset_array,
      &get_slabset_num_ptr,
      &get_slabobjs_array,
      &get_slabobjs_idx_array,
      &get_slabobjs_num_ptr,
      &set_block_health,
      &get_player_special_digger, &set_player_special_digger,
      &get_computer_player_f,
      &reactivate_build_process,
      &reinitialise_rooms_of_kind,
      &recalculate_effeciency_for_rooms_of_kind,
      &slabmap_block_invalid,
      &slabmap_kind,
      &find_and_load_lif_files, &find_and_load_lof_files,
      &get_computer_process_func_type, &get_computer_check_func_type,
      &get_computer_event_func_type, &get_computer_event_test_func_type,
      &get_my_player_number,
      &player_is_roaming,
      &slab_is_area_inner_fill,
      &thing_class_and_model_name,
      &get_creature_instances_func_type, &get_creature_instances_validate_func_type,
      &get_creature_instances_search_targets_func_type,
      &get_creature_job_player_assign_func_type, &get_creature_job_player_check_func_type,
      &get_creature_job_coords_check_func_type, &get_creature_job_coords_assign_func_type,
      &remove_creature_lair, &update_creature_health_to_max, &update_relative_creature_health,
      &do_to_players_all_creatures_of_model,
      &do_to_all_things_of_class_and_model,
      &recalculate_all_creature_digger_lists,
      &update_speed_of_player_creatures_of_model,
      &creature_increase_available_instances, &process_job_stress_and_going_postal,
      &get_process_func_commands, &get_cleanup_func_commands,
      &get_move_from_slab_func_commands, &get_move_check_func_commands,
      &player_has_heart,
      &thing_is_invalid,
      &setup_excess_creatures_to_leave_or_die,
      &get_level_strings,
      &set_door_buildable_and_add_to_amount, &set_trap_buildable_and_add_to_amount,
      &set_speech_queue_limit,
      &script_strdup, &script_strval,
      &reset_all_campaign_progress,
  };
  set_config_reload_callbacks(&config_reload_callbacks_impl);

  // Configuration file
  if ( !load_configuration() )
  {
      ERRORLOG("Configuration load error.");
      return 0;
  }

  // gpu-v2 Phase C.1 (docs/refactor/renderer/gpu-v2/07-phased-delivery.md):
  // RendererInit(RENDERER_SOFTWARE) above already brought up an active
  // renderer for the pre-config-load splash/legal screens -- load_configuration()
  // just above is the earliest point RENDERER's cfg value (and R13's
  // GUI_ICON_PACK=CLASSIC check) is actually known, so the real backend
  // swap (if the user asked for one) happens here, not earlier.
  {
      RendererType desired = RendererGetDesiredType();
      // R13 (docs/refactor/renderer/gpu-v2/08-risks.md, gpu-v2/04's
      // "CLASSIC's compositing assumption doesn't survive Phase C
      // unmodified"): CLASSIC's draw calls assume they're painting directly
      // onto a lbDrawSurface that already contains the CPU-rasterized 3D
      // scene -- once RendererGpu3D is active that's no longer true. Fail
      // clearly (fall back, logged) rather than let a broken frame render.
      if (desired == RENDERER_GPU3D && ingame_gui_use_classic_hud())
      {
          WARNLOG("RENDERER=VULKAN is incompatible with GUI_ICON_PACK=CLASSIC (R13) -- falling back to the software renderer");
          desired = RENDERER_SOFTWARE;
      }
      if (desired != RendererGetActiveType() && RendererInit(desired) == 0)
      {
          ERRORLOG("Renderer initialisation error.");
          return 0;
      }
  }

  #ifdef FUNCTESTING
    start_params.startup_flags &= ~SFlg_Legal;
    start_params.startup_flags &= ~SFlg_FX;
    features_enabled |= Ft_SkipHeartZoom;
  #endif

  // Process CmdLine overrides
  process_cmdline_overrides();

  // Push resolved config/cmdline state down into bflib_* (see
  // docs/refactor/stage-02-decouple-bflib.md).
  bf_sprfnt_set_language_lwrstr(get_language_lwrstr(install_info.lang_id));
  bf_sprfnt_set_fxdata_dir(prepare_file_path(FGrp_FxData, ""));
  bf_sndlib_set_audio_config(get_language_lwrstr(install_info.lang_id), is_feature_on(Ft_NoCdMusic));
  bf_sound_set_atmos_config(AtmosStart, AtmosEnd, AtmosRepeat, atmos_sounds_enabled());
  // docs/refactor/renderer/05-imgui-linkage-consolidation.md: ImGui context/
  // backend ownership lives in kfx_frontend (gui/FrontendImGui.{h,cpp}) --
  // kfx_platform can't call up into it directly (layering), so
  // RendererSoftware::PresentFrame/bflib_inputctrl.cpp/bflib_mspointer.cpp
  // reach it through this one RendererImGuiCallbacks struct instead, the
  // same *Callbacks-in-kfx_platform, registered-here pattern
  // renderer_draw_callbacks_impl below already uses. Its members are thin
  // wrappers around FrontendImGui*'s real functions -- the mouse-position
  // and cursor-image queries used to be separate callbacks registered from
  // here too, but now that the context itself lives in kfx_frontend, it
  // calls GetMouseX()/GetMouseY() (kjm_input.h) and FeStyleGetCursorImage()
  // directly instead, both same-library kfx_frontend calls -- see
  // FrontendImGui.cpp.
  //
  // submit stays a wrapper defined here (app_imgui_frame), not
  // FrontendImGuiFrame directly: kfx_editor ranks above kfx_apploop, so it
  // can't register its own ImGui-frame callback the way FrontendImGuiFrame
  // itself could -- main.cpp (the composition root, the one file allowed
  // to #include every layer) wraps the single registered callback instead.
  // editor_frame() no-ops unless editor_is_active().
  static const struct RendererImGuiCallbacks renderer_imgui_callbacks_impl = {
      &FrontendImGuiEnsure,
      &FrontendImGuiRendererDestroying,
      &FrontendImGuiBeginFrame,
      &app_imgui_frame,
      &FrontendImGuiRender,
      &FrontendImGuiProcessEvent,
      &FrontendImGuiIsActive,
      &FrontendImGuiWantCaptureMouse,
      &FrontendImGuiWantCaptureKeyboard,
      &FrontendImGuiScreenOwned,
      &FrontendImGuiSetDemoVisible,
  };
  set_renderer_imgui_callbacks(&renderer_imgui_callbacks_impl);
  // docs/refactor/renderer/04-imgui-gui-foundation.md §3.5/§7 Phase A --
  // kfx_platform can't call kfx_config's is_feature_on() itself (kfx_config
  // ranks above kfx_platform), so push the resolved flag down through the
  // struct above instead. ImGui itself is unconditional now (every
  // frontend menu needs it, and the in-game HUD's own classic-vs-ImGui
  // choice, GUI_ICON_PACK's "CLASSIC" value, is decided per-draw-call
  // inside kfx_frontend -- see ingame_gui_use_classic_hud()) -- there is no
  // longer a session-wide "ImGui enabled" switch to push down here.
  RendererSetImGuiDemoVisible((start_params.debug_flags & DFlg_ImGuiDemo) != 0);
  FeStyleSheetSetVisible((start_params.debug_flags & DFlg_ImGuiStyleSheet) != 0);
  static const struct InputFocusPredicates input_focus_predicates = {
      &freeze_game_on_focus_lost, &mute_audio_on_focus_lost,
      &unlock_cursor_when_game_paused, &lock_cursor_in_possession,
      &is_game_paused, &is_possession_mode_active, &is_packet_load_enabled,
      &use_relative_mouse_mode,
  };
  set_input_focus_predicates(&input_focus_predicates);
  static const struct SoundStateCallbacks sound_state_callback_table = {
      &get_music_track, &get_music_fname, &get_frame_skip,
      &get_easter_eggs_enabled, &get_last_level,
      &get_creature_model_count, &get_creature_sounds,
      &get_mods_after_map, &get_mods_after_map_count,
      &get_mods_after_campaign, &get_mods_after_campaign_count,
      &get_mods_after_base, &get_mods_after_base_count,
      &get_sound_random_seed, &get_unsync_random_seed,
      &init_sound, &mute_audio,
      &play_creature_sound,
      &prepare_file_path, &prepare_file_path_mod,
      &prepare_file_path_buf, &prepare_file_fmtpath,
      &creature_code_name, &get_creature_desc,
      &thing_is_invalid,
  };
  set_sound_state_callbacks(&sound_state_callback_table);
  static const struct VideoScaleCallbacks video_scale_callback_table = {
      &get_video_scale_values,
  };
  set_video_scale_callbacks(&video_scale_callback_table);
  set_emulate_integer_overflow_provider(&emulate_integer_overflow);
  set_get_gameturn_provider(&game_legacy_get_gameturn);
  static const struct MapZipCallbacks map_zip_callback_table = {
      &prepare_map_zip_path,
  };
  set_map_zip_callbacks(&map_zip_callback_table);
  bf_sprfnt_set_font_role_resolver(resolve_font_role);
  set_config_network_is_active_check(network_is_active);
  set_power_grant_revoke_callbacks(add_power_to_player, remove_power_from_player);
  static const struct ScriptHookCallbacks script_hooks_impl = {
      &lua_on_power_cast, &lua_on_special_box_activate, &lua_on_creature_death,
      &lua_on_creature_fell_into_abyss,
      &lua_on_creature_rebirth, &lua_on_trap_placed, &lua_on_object_destroyed,
      &lua_on_apply_damage_to_thing, &lua_on_level_up, &lua_on_pick_up, &lua_on_slap,
      &lua_on_slab_kind_change, &lua_on_slab_owner_change, &lua_on_room_owner_change,
      &lua_on_shot_hit, &lua_on_dungeon_destroyed, &luafunc_crstate_func, &luafunc_thing_update_func,
      &luafunc_shot_hit_thing_func, &luafunc_magic_use_power, &luafunc_trap_activation_func, &api_event,
      &api_event_with_data,
      &lua_on_game_start, &open_lua_script, &execute_lua_code_from_console,
      &execute_lua_code_from_script, &generate_lua_types_file,
      &lua_get_serialised_data, &lua_set_serialised_data, &cleanup_serialized_data,
  };
  set_script_hook_callbacks(&script_hooks_impl);
  static const struct SimFeedbackCallbacks sim_feedback_impl = {
      &erstat_inc, &show_onscreen_msg_plain, &output_message, &output_room_message,
      &output_message_far_from_thing, &play_speech_ref, &clear_messages, &process_messages,
      &clear_messages_from_player, &targeted_message_add_plain,
      &message_add, &message_add_fmt, &zero_messages, &show_real_time_taken,
      &thing_play_sample, &stop_thing_playing_sample, &create_ambient_sound,
      &play_sound_if_close_to_receiver, &play_thing_walking,
      &sim_feedback_is_best_roomspace_key_pressed, &sim_feedback_is_square_roomspace_key_pressed,
      &sim_feedback_is_roomspace_incsize_key_pressed, &sim_feedback_is_roomspace_decsize_key_pressed,
      &sim_feedback_is_sell_trap_on_subtile_key_pressed,
      &sim_feedback_set_room_type_highlighted, &sim_feedback_set_visible_event_idx,
      &sim_feedback_clear_all_event_button_states, &sim_feedback_clear_event_button_state,
      &sim_feedback_mark_event_button_read,
      &sim_feedback_is_battle_creature_over_active,
      &sim_feedback_hide_map_volume_box, &sim_feedback_reset_box_lag_compensation,
      &tag_cursor_blocks_dig,
      &tag_cursor_blocks_place_door, &tag_cursor_blocks_place_room, &tag_cursor_blocks_sell_area,
      &set_engine_view, &setup_engine_window,
      &light_create_light, &light_init_dungeon_heart,
      &light_delete_light, &light_turn_light_off, &light_turn_light_on,
      &light_get_light_intensity, &light_set_light_intensity, &light_signal_update_in_area,
      &light_set_light_never_cache, &light_is_light_allocated, &light_set_light_position,
      &light_get_light_radius, &light_set_light_radius,
      &light_initialise, &light_count_lights, &light_create_light_adv,
      &process_dungeon_destroy, &initialise_devastate_dungeon_from_heart,
      &load_texture_map_file,
      &get_event_button_info,
      &frontstats_initialise,
      &GetMouseX, &GetMouseY, &is_mouse_pressed_lrbutton, &is_key_pressed, &mouse_is_over_panel_map,
      &sim_feedback_is_left_button_held,
      &PaletteSetUserPalette, &PaletteApplyPainToPlayer,
      &toggle_status_menu, &turn_off_roaming_menus, &initialise_tab_tags_and_menu,
      &init_gui, &set_gui_visible, &update_player_objectives, &create_message_box,
      &turn_on_menu, &turn_off_menu, &turn_off_query_menus, &turn_off_all_menus,
      &turn_off_all_window_menus, &turn_on_main_panel_menu, &turn_off_all_panel_menus,
      &turn_off_event_box_if_necessary,
      &refresh_active_button_sprites_for_player,
      &find_next_room_of_type,
      &sync_local_camera, &set_local_camera_destination, &get_local_camera,
      &move_local_camera_to_position,
      &get_camera_zoom, &set_camera_zoom, &view_zoom_camera_in, &view_zoom_camera_out,
      &view_set_camera_move_to_position, &view_move_camera_to_position,
      &init_player_cameras, &any_player_close_enough_to_see, &lightning_is_close_to_player,
      &packet_crtr_control_pressed,
      &output_message_far_from_thing,
      &get_packet_load_enable, &get_local_plyr_idx, &get_input_lag_turns,
      &set_active_players_count,
      &get_isometric_view_zoom_level, &get_frontview_zoom_level,
      &get_player_exists_flag, &get_player_comp_flag,
      &increment_active_players_count,
      &sim_feedback_get_loaded_level_number,
      &sim_feedback_get_selected_level_number,
      &sim_feedback_get_level_number,
      &sim_feedback_get_play_gameturn,
      &update_time, &get_game_time, &player_has_enemies_to_defeat, &get_zoom_key_room_order,
      &get_history_packet,
      &setup_eye_lens, &lens_is_ready, &lens_get_render_target,
      &lens_get_render_target_width, &lens_get_render_target_height, &draw_lens_effect,
      &get_td_animation_sprite,
      &process_keeper_sprite, &engine,
      &add_transfered_creature, &clear_transfered_creatures,
      &reset_ambient_sound_thing_idx,
      &get_lens_mode,
      &hide_tooltip,
      &timer_enabled,
      &set_timer_turns,
      &get_transferred_creature,
      &activate_bonus_level_for_singleplayer,
  };
  set_sim_feedback_callbacks(&sim_feedback_impl);
  static const struct PathfindingWorldCallbacks pathfinding_world_impl = {
      &pathfinding_world_get_map_size_x, &pathfinding_world_get_map_size_y,
      &get_map_block_at, &get_map_block_at_pos,
      &pathfinding_world_map_block_flags, &map_block_invalid,
      &get_floor_filled_subtiles_at, &subtile_is_unsafe,
      &get_slabmap_block, &pathfinding_world_slabmap_block_kind, &slabmap_block_invalid, &pathfinding_world_slabmap_owner,
      &is_valid_hug_subtile, &subtile_is_door, &pathfinding_world_thing_in_wall_at,
      &get_door_for_position, &door_is_hidden_to_player, &door_will_open_for_thing,
      &pathfinding_world_door_is_locked, &players_are_mutual_allies,
      &thing_is_invalid, &pathfinding_world_thing_get_owner,
      &get_thing_height_at, &get_floor_height_under_thing_at,
      &creature_can_travel_over_lava, &pathfinding_world_thing_is_flying, &thing_model_name,
      &pathfinding_world_thing_get_position, &pathfinding_world_thing_set_position,
      &pathfinding_world_thing_get_move_angle, &pathfinding_world_thing_set_move_angle,
      &pathfinding_world_thing_get_index, &pathfinding_world_thing_get_clipbox_size,
      &pathfinding_world_creature_get_navigation, &pathfinding_world_creature_get_ariadne_state,
      &pathfinding_world_creature_get_max_speed,
      &pathfinding_world_creature_clear_state_flags_for_wallhug_override,
      &get_subtile_number, &stl_num_decode_x, &stl_num_decode_y, &stl_slab_center_subtile,
      &get_slabmap_for_subtile, &hug_can_move_on,
      &cross_x_boundary_first, &cross_y_boundary_first,
      &pathfinding_world_get_small_around,
      &pathfinding_world_get_small_around_length,
      &small_around_index_in_direction,
      &pathfinding_world_get_map_size_z,
      &creature_cannot_move_directly_to,
      &pathfinding_world_get_owner_player_navigating, &pathfinding_world_set_owner_player_navigating,
      &pathfinding_world_get_nav_thing_can_travel_over_lava, &pathfinding_world_set_nav_thing_can_travel_over_lava,
      &pathfinding_world_get_nav_thing_is_flying, &pathfinding_world_set_nav_thing_is_flying,
      &subtile_has_abyss_on_top,
      &pathfinding_world_creature_steps_into_toxic_terrain,
  };
  set_pathfinding_world_callbacks(&pathfinding_world_impl);
  static const struct SpriteLookupCallbacks sprite_lookup_impl = {
      &get_icon_id, &get_anim_id, &get_anim_id_,
      &get_button_sprite, &get_panel_sprite,
      &get_ensign_id, &init_custom_campaign_sprites,
      &load_sprites_for_multi_front,
  };
  set_sprite_lookup_callbacks(&sprite_lookup_impl);
  static const struct MatchmakingConfigCallbacks matchmaking_config_impl = {
      &matchmaking_config_set_enabled, &matchmaking_set_server, &matchmaking_config_get_ws_url,
  };
  set_matchmaking_config_callbacks(&matchmaking_config_impl);
  static const struct RenderOverlayCallbacks render_overlay_impl = {
      &redraw_parchment_view, &render_overlay_load_and_redraw_minimal_overhead_view, &render_overlay_set_parchment_loaded,
      &is_parchment_loaded, &reload_parchment_file, &point_to_overhead_map,
      &render_overlay_get_main_menu_width,
      &render_overlay_draw_gui_panel_sprite_left, &draw_slab64k,
      &draw_gui_panel_sprite_centered, &draw_button_sprite_left,
      &message_draw, &gui_draw_all_boxes, &draw_tooltip, &render_overlay_sync_cheat_box_3_active_option, &render_overlay_cheat_or_menu_window_active,
      &draw_eastegg,
      &render_overlay_set_winfont, &render_overlay_get_status_panel_width, &draw_gui, &render_overlay_game_is_busy_doing_gui,
      &render_overlay_game_is_busy_doing_gui_string_input,
      &draw_whole_status_panel,
      &render_overlay_draw_debug_overlays, &render_overlay_bonus_script_or_variable_overlay_active, &render_overlay_get_battle_creature_over,
      &render_overlay_get_map_diagonal_length,
      &render_overlay_get_unpausing_in_progress,
      &can_process_creature_input, &process_first_person_look, &process_camera_controls,
      &process_camera_action,
      &get_history_packet, &set_packet_control,
      &frontend_load_data_from_cd, &frontend_load_data_reset, &menu_is_active, &reinit_all_menus,
      &turn_on_menu,
      &sync_render_globals,
      &setup_heap_manager, &reset_heap_manager, &he_alloc,
      &light_create_light, &light_set_attached_slab, &delete_lights_attached_to_slab_in_area, &light_get_lights_enabled,
      &get_interpolate_time,
  };
  set_render_overlay_callbacks(&render_overlay_impl);
  static const struct DungeonAvailabilityCallbacks dungeon_availability_impl = {
      &player_has_valid_dungeon, &player_has_valid_dungeon_with_heart,
      &players_num_dungeon_valid, &players_num_dungeon_valid_with_heart,
      &set_creature_availability,
      &try_set_backup_heart_idx,
      &set_room_resrchable_and_buildable,
      &get_room_resrchable, &set_all_room_resrchable,
      &get_room_buildable, &set_all_room_buildable_from_resrchable,
      &get_magic_resrchable, &set_magic_resrchable, &set_all_magic_resrchable_unchecked, &get_magic_level_gt0,
      &get_trap_placeable, &get_trap_manufacturable, &get_trap_built,
      &get_door_placeable, &get_door_manufacturable, &get_door_built,
  };
  set_dungeon_availability_callbacks(&dungeon_availability_impl);
  static const struct NetCallbacks net_callbacks_impl = {
      &net_callbacks_enter_net_session_screen, &net_callbacks_set_lobby_button_labels,
      &create_frontend_error_box, &frontend_save_continue_game, &toggle_status_menu,
      &set_gui_visible,
      &net_callbacks_get_default_tag_mode, &net_callbacks_is_frontend_starting_mp_level,
      &net_callbacks_is_frontend_at_initial_state,

      &display_attempting_to_join_message, &attempting_to_join_cancel_requested,
      &reset_attempting_to_join_cancel, &process_network_error, &net_callbacks_frontnet_service_selected,

      &turn_off_all_menus, &turn_off_query_menus, &turn_on_main_panel_menu,
      &turn_off_all_panel_menus, &turn_on_menu,

      &panel_map_update,

      &update_trap_tab_to_config, &instant_instance_selected,

      &is_key_pressed, &clear_key_pressed,

      &process_cheat_heart_health_inputs,

      &net_callbacks_clear_player_lightning_palette,

      &cmd_exec,

      &lua_on_chatmsg,

      &net_callbacks_lua_script_active, &lua_resync_export, &lua_resync_import,
      &lua_set_random_seed, &cleanup_serialized_data,

      &resync_export_game_state, &resync_import_game_state,
      &resync_export_frontend_state, &resync_import_frontend_state,

      &network_yield_poll_gameplay, &network_yield_waiting_gameplay_packets,
      &network_yield_draw_frontend,
      &output_message,
      &erstat_inc, &show_onscreen_msg_plain, &is_onscreen_msg_visible,
      &winning_player_quitting, &reinit_level_after_load, &complete_level, &lose_level, &resign_level,
      &load_game_chunks, &fill_game_catalogue_entry, &save_packet_chunks,
      &draw_out_of_sync_box, &process_frontend_chat_message,
      &set_host_packet_received,
      &net_callbacks_get_frontend_alliances,
  };
  set_net_callbacks(&net_callbacks_impl);
  static const struct GameCallbacks game_callbacks_impl = {
      &toggle_main_cheat_menu, &toggle_instance_cheat_menu, &toggle_secondary_cheat_menu,
      &toggle_creature_cheat_menu, &close_main_cheat_menu, &close_instance_cheat_menu,
      &close_secondary_cheat_menu, &close_creature_cheat_menu, &create_error_box,
      &game_callbacks_is_fe_computer_players_active, &set_gui_visible, &menu_is_active,

      &game_callbacks_set_timer_turns, &timer_enabled, &game_callbacks_toggle_debug_network_stats,
      &bonus_timer_enabled,

      &go_to_my_next_room_of_type, &get_button_designation, &gui_set_button_flashing,

      &gui_create_box,

      &zero_messages, &show_game_time_taken,

      &game_callbacks_toggle_tooltip_land_coord,

      &game_callbacks_get_high_score_entry, &game_callbacks_set_high_score_entry,

      &frontstats_initialise,

      &setup_alliances,

      &clear_messages, &process_messages, &script_play_message,

      &turn_on_menu, &turn_off_menu,

      &erstats_clear,

      &init_gui,

      &set_level_objective, &display_objectives, &display_objectives_with_icon,

      &reset_gui_based_on_player_mode,

      &update_panel_colors,
      &save_frontend_state, &load_frontend_state, &reset_frontend_state,
      &get_frontend_state_size,
      &get_intralvl_next_level, &clear_intralvl_next_level,
      &game_callbacks_set_frontend_alliances,
  };
  set_game_callbacks(&game_callbacks_impl);
  // docs/refactor/editor/01-entry-and-editor-session.md §4 -- the one
  // inbound edge kfx_apploop needs into kfx_editor (which it can't
  // #include directly, ranked below it): the hand-off from "sim loaded
  // and paused" to "editor session active" once
  // startup_local_game_for_editor()'s coroutine finishes.
  static const struct EditorCallbacks editor_callbacks_impl = {
      &editor_open,
      &editor_is_active,
  };
  set_editor_callbacks(&editor_callbacks_impl);
  // docs/refactor/editor/fx-plans/03-content-editors-foundation.md §5 -- the main menu's Tools modal
  // (kfx_frontend, ranked below kfx_editor) opens and draws the content editors through this.
  static const struct ContentToolsCallbacks content_tools_callbacks_impl = {
      &content_tools_is_available,
      &content_tools_open,
      &content_tools_frame,
      &content_tools_is_open,
  };
  set_content_tools_callbacks(&content_tools_callbacks_impl);
  // docs/refactor/editor/02-editing-toolbox.md §4 -- lets packets_cheats.c
  // (kfx_net) record a placement into kfx_editor's undo journal without
  // depending on kfx_editor.h directly, same shape as EditorCallbacks above.
  static const struct EditorJournalCallbacks editor_journal_callbacks_impl = {
      &editor_journal_record_placement,
      &editor_journal_record_rect_terrain,
      &editor_journal_record_door_lock,
  };
  set_editor_journal_callbacks(&editor_journal_callbacks_impl);
  kfx_config_state.gui_blink_rate = keeperfx_ui_config.gui_blink_rate;
  kfx_config_state.neutral_flash_rate = keeperfx_ui_config.neutral_flash_rate;
  creature_status_size = keeperfx_ui_config.creature_status_size;
  line_box_size = keeperfx_ui_config.line_box_size;
  right_click_tag_mode_toggle = keeperfx_ui_config.right_click_tag_mode_toggle;
  zoom_to_mouse_option = (enum ZoomToMouseOptions)keeperfx_ui_config.zoom_to_mouse_option;
  rotate_around_mouse_option = (enum RotateAroundMouseOptions)keeperfx_ui_config.rotate_around_mouse_option;

  LbIKeyboardOpen();

  if (LbDataLoadAll(legal_load_files) != 0)
  {
      ERRORLOG("Error on allocation/loading of legal_load_files.");
      return 0;
  }

  // Setup polyscans
  setup_bflib_render();

  // View the legal screen
  if (!setup_screen_mode_zero(get_screen_vidmode()))
  {
      ERRORLOG("Unable to set display mode for legal screen");
      return 0;
  }

  if (flag_is_set(start_params.startup_flags, SFlg_Legal))
  {
      if (is_ar_wider_than_original(LbGraphicsScreenWidth(), LbGraphicsScreenHeight()))
      {
        result = init_actv_bitmap_screen(RBmp_SplashLegalWide);
      } else {
        result = init_actv_bitmap_screen(RBmp_SplashLegal);
      }
       if ( result )
      {
          result = show_actv_bitmap_screen(3000);
          free_actv_bitmap_screen();
      } else
          SYNCLOG("Legal image skipped");
  }
  else
  {
      // Make the white screen into a black screen faster
      draw_clear_screen();
  }

  // Now do more setup
  // Prepare the Game structure
  clear_complete_game();
  // Moon phase calculation
  calculate_moon_phase(true,true);
  // Start the sound system
  if (!init_sound())
    WARNMSG("Sound system disabled.");
  // Note: for some reason, signal handlers must be installed AFTER
  // init_sound(). This will probably change when we'll move sound
  // to SDL - then we'll put that line earlier, before setup_game().
  LbErrorParachuteInstall();
  // View second splash screen
  if (flag_is_set(start_params.startup_flags, SFlg_FX))
  {
      result = init_actv_bitmap_screen(RBmp_SplashFx);
      if ( result == 1 )
      {
          result = show_actv_bitmap_screen(4000);
          free_actv_bitmap_screen();
      } else
          SYNCLOG("startup_fx image skipped");
  }

  draw_clear_screen();
  // View Bullfrog company logo animation when new moon
  if ( ( is_new_moon ) || (flag_is_set(start_params.startup_flags, SFlg_Bullfrog)) )
    if (!start_params.no_intro)
    {
        result = moon_video();
        if ( !result ) {
            ERRORLOG("Unable to play new moon movie");
        }
    }

  result = 1;
  // Setup the intro video mode
  if (result && (!start_params.no_intro) )
  {
      if (!setup_screen_mode_zero(get_screen_vidmode()))
      {
        ERRORLOG("Can't enter movies screen mode to play intro");
        result=0;
      }
  }

  if (result == 1)
  {
      draw_clear_screen();
      if (wait_for_installation_files())
      {
          //result = -1; // Helps with better warning message later
      }
      if (!start_params.no_intro)
      {
         if (flag_is_set(start_params.startup_flags, SFlg_EA))
         {
             ea_video();
         }
         if (flag_is_set(start_params.startup_flags, SFlg_Intro))
         {
            result = intro_replay();
         }
      }
  }

  kfx_net_state.frame_skip = start_params.frame_skip;
  redetect_screen_refresh_rate_for_draw();

  // Intro problems shouldn't force the game to quit,
  // so we're re-setting the result flag
  if (result == 0)
      result = 1;

  if (result == 1)
  {
      if (flag_is_set(start_params.operation_flags, GOF_SingleLevel) && !(game_flags2 & (GF2_Connect | GF2_Server)))
          level_load_time_phase(LevelLoadTime_EngineStartup);
      display_loading_screen();
  }
  LbDataFreeAll(legal_load_files);

  if (result == 1)
  {
      if ( !initial_setup() )
        result = 0;
  }

  if (result == 1)
  {
    load_settings();
    if ( !setup_gui_strings_data() )
      result = 0;
  }

  if (result == 1)
  {
      init_keeper();
      set_gamma(settings.gamma_correction, 0);
      set_music_volume(settings.music_volume);
      SetSoundMasterVolume(settings.sound_volume);
      setup_mesh_randomizers();
      setup_stuff();
  }

  return result;
}

/**
 * Sets to defaults some basic parameters which are
 * later copied into Game structure.
 */
TbBool set_default_startup_parameters(void)
{
    memset(&start_params, 0, sizeof(struct StartupParameters));
    start_params.startup_flags = (SFlg_Legal|SFlg_FX|SFlg_Intro);
    start_params.packet_checksum_verify = 1;
    // Set levels to 0, as we may not have the campaign loaded yet
    start_params.selected_level_number = 0;
    start_params.num_fps = 20;
    start_params.one_player = 1;
    start_params.computer_chat_flags = CChat_None;
    start_params.autostart_multiplayer_users_expected = 2;
    clear_flag(start_params.mode_flags, MFlg_IsDemoMode);
    set_flag(start_params.mode_flags, MFlg_DemoMode);
    return true;
}

static int64_t process_command_line(int64_t argc, char *argv[])
{
  char fullpath[CMDLN_MAXLEN+1];
  snprintf(fullpath, CMDLN_MAXLEN, "%s", argv[0]);
  snprintf(keeper_runtime_directory, sizeof(keeper_runtime_directory), "%s", fullpath);
  char *endpos = strrchr( keeper_runtime_directory, '\\');
  if (endpos==NULL)
      endpos=strrchr( keeper_runtime_directory, '/');
  if (endpos!=NULL)
      *endpos='\0';
  else
      strcpy(keeper_runtime_directory, ".");

  AssignCpuKeepers = 0;
  SoundDisabled = 0;
  // Note: the working log file is set up in LbBullfrogMain

  set_default_startup_parameters();

  int64_t bad_param;
  LevelNumber level_num;
  bad_param = 0;
  int64_t narg;
  level_num = LEVELNUMBER_ERROR;
  TbBool one_player_mode = 0;
  narg = 1;
  char bad_params[TEXT_BUFFER_LENGTH] = "\0";
  while ( narg < argc )
  {
      char *par;
      par = argv[narg];
      if ( (par == NULL) || ((par[0] != '-') && (par[0] != '/')) )
          return -1;
      char parstr[CMDLN_MAXLEN+1];
      char pr2str[CMDLN_MAXLEN+1];
      char pr3str[CMDLN_MAXLEN+1];
      snprintf(parstr, CMDLN_MAXLEN, "%s", par + 1);
      if (narg + 1 < argc)
      {
          snprintf(pr2str, CMDLN_MAXLEN, "%s", argv[narg + 1]);
          if (narg + 2 < argc)
              snprintf(pr3str, CMDLN_MAXLEN, "%s", argv[narg + 2]);
          else
              pr3str[0]='\0';
      }
      else
      {
          pr2str[0]='\0';
          pr3str[0]='\0';
      }

      if (strcasecmp(parstr, "nointro") == 0)
      {
        start_params.no_intro = true;
      } else
      if (strcasecmp(parstr, "skipheartzoom") == 0)
      {
        start_params.skip_heart_zoom = true;
      } else
      if (strcasecmp(parstr, "nocd") == 0) // kept for legacy reasons
      {
          WARNLOG("The -nocd commandline parameter is no longer functional. Game music from CD is a setting in keeperfx.cfg instead.");
      } else
      if (strcasecmp(parstr, "columnconvert") == 0) //todo remove once it's no longer in the launcher
      {
          WARNLOG("The -%s commandline parameter is no longer functional.", parstr);
      }
      else
      if (strcasecmp(parstr, "cd") == 0)
      {
          start_params.overrides[Clo_CDMusic] = true;
      } else
      if (strcasecmp(parstr, "1player") == 0)
      {
          start_params.one_player = true;
          one_player_mode = true;
      } else
      if ((strcasecmp(parstr, "s") == 0) || (strcasecmp(parstr, "nosound") == 0))
      {
          SoundDisabled = true;
      } else
      if (strcasecmp(parstr, "headless") == 0)
      {
          // No real display or audio device needed -- SDL still gets a
          // (unshown) window/surface via its "dummy" video driver
          // (VideoDisabled, checked in PlatformLinux/PlatformWindows::
          // VideoInit()), and SoundDisabled skips audio device init
          // entirely (sounds.c). For running src/ftests/ in CI/sandboxed
          // environments, e.g. under coverage instrumentation.
          VideoDisabled = true;
          SoundDisabled = true;
      } else
      if (strcasecmp(parstr, "fps") == 0)
      {
          narg++;
          start_params.num_fps = atoi(pr2str);
          start_params.overrides[Clo_GameTurns] = true;
      } else
      if (strcasecmp(parstr, "fps_draw") == 0)
      {
          narg++;
	  if (parse_draw_fps_config_val(pr2str, &start_params.num_fps_draw_main, &start_params.num_fps_draw_secondary) > 0)
            start_params.overrides[Clo_FramesPerSecond] = true;
      } else
      if (strcasecmp(parstr, "human") == 0)
      {
          narg++;
          default_loc_player = atoi(pr2str);
          start_params.force_player_num = true;
      } else
      if (strcasecmp(parstr, "vidsmooth") == 0)
      {
          smooth_on = true;
          start_params.overrides[Clo_VidSmooth] = true;
      } else
      if ( strcasecmp(parstr,"level") == 0 )
      {
        set_flag(start_params.operation_flags, GOF_SingleLevel);
        level_num = atoi(pr2str);
        start_params.autostart_multiplayer_level = atoi(pr2str);
        narg++;
      } else
      if ( strcasecmp(parstr,"campaign") == 0 )
      {
        strcpy(start_params.selected_campaign, pr2str);
        strcpy(start_params.autostart_multiplayer_campaign, pr2str);
        narg++;
      } else
      if ( strcasecmp(parstr,"altinput") == 0 )
      {
          SYNCLOG("Mouse auto reset disabled");
          lbMouseGrab = false;
          start_params.overrides[Clo_AltInput] = true;
      }
      else if (strcasecmp(parstr,"packetload") == 0)
      {
         if (start_params.packet_save_enable)
            WARNMSG("PacketSave disabled to enable PacketLoad.");
         start_params.packet_load_enable = true;
         start_params.packet_save_enable = false;
         snprintf(start_params.packet_fname, sizeof(start_params.packet_fname), "%s", pr2str);
         set_flag(start_params.debug_flags, DFlg_ShowGameTurns | DFlg_FrameStep);
         narg++;
      } else
      if (strcasecmp(parstr,"packetsave") == 0)
      {
         if (start_params.packet_load_enable)
            WARNMSG("PacketLoad disabled to enable PacketSave.");
         start_params.packet_load_enable = false;
         start_params.packet_save_enable = true;
         snprintf(start_params.packet_fname, sizeof(start_params.packet_fname), "%s", pr2str);
         narg++;
      } else
      if (strcasecmp(parstr,"pause_at_gameturn") == 0)
      {
         set_flag(start_params.debug_flags, DFlg_ShowGameTurns | DFlg_FrameStep | DFlg_PauseAtGameTurn);
         start_params.pause_at_gameturn = atoi(pr2str);
         narg++;
      } else
      if (strcasecmp(parstr,"q") == 0)
      {
         set_flag(start_params.operation_flags, GOF_SingleLevel);
      } else
      if (strcasecmp(parstr,"lightconvert") == 0)
      {
         WARNLOG("The -%s commandline parameter is no longer functional.", parstr); //todo remove once it's no longer in the launcher
      } else
      if (strcasecmp(parstr, "dbgshots") == 0)
      {
          set_flag(start_params.debug_flags, DFlg_ShotsDamage);
      } else
      if (strcasecmp(parstr, "dbgpathfind") == 0)
      {
          set_flag(start_params.debug_flags, DFlg_CreatrPaths);
      } else
      if (strcasecmp(parstr, "imguidemo") == 0)
      {
          // docs/refactor/renderer/04-imgui-gui-foundation.md §7 Phase A
          // exit criteria: prove the ImGui backend wiring with imgui_demo
          // ahead of any real screen migrating.
          set_flag(start_params.debug_flags, DFlg_ImGuiDemo);
      } else
      if (strcasecmp(parstr, "imguistyle") == 0)
      {
          // §7 Phase B exit criteria: the style-sheet test screen
          // exercising every frontgui_widgets.h wrapper.
          set_flag(start_params.debug_flags, DFlg_ImGuiStyleSheet);
      } else
      if (strcasecmp(parstr, "show_game_turns") == 0)
      {
          set_flag(start_params.debug_flags, DFlg_ShowGameTurns);
      } else
      if (strcasecmp(parstr, "mplog") == 0)
      {
          detailed_multiplayer_logging = true;
      } else
      if (strcasecmp(parstr, "netstats") == 0)
      {
          debug_display_network_stats = 1;
      } else
      if (strcasecmp(parstr, "compuchat") == 0)
      {
          if (strcasecmp(pr2str,"scarce") == 0) {
              start_params.computer_chat_flags = CChat_TasksScarce;
          } else
          if (strcasecmp(pr2str,"frequent") == 0) {
              start_params.computer_chat_flags = CChat_TasksScarce|CChat_TasksFrequent;
          } else {
              start_params.computer_chat_flags = CChat_None;
          }
          narg++;
      } else
      if (strcasecmp(parstr, "sessions") == 0) {
          narg++;
          LbNetwork_InitSessionsFromCmdLine(pr2str);
      } else
      if (strcasecmp(parstr, "nomods") == 0) {
          start_params.ignore_mods = true;
      } else
      if (strcasecmp(parstr,"alex") == 0)
      {
         start_params.easter_egg = true;
         start_params.overrides[Clo_EasterEgg] = true;
      }
      else if (strcasecmp(parstr,"connect") == 0)
      {
          narg++;
          LbNetwork_InitSessionsFromCmdLine(pr2str);
          game_flags2 |= GF2_Connect;
      }
      else if (strcasecmp(parstr,"waitusers") == 0)
      {
          start_params.autostart_multiplayer_users_expected = clamp(atoi(pr2str), MIN_NET_USERS, MAX_NET_USERS);
          narg++;
      }
      else if (strcasecmp(parstr,"server") == 0)
      {
          game_flags2 |= GF2_Server;
          int64_t port = atoi(pr2str);
          if (port > 0)
          {
              LbNetwork_SetServerPort(port);
              narg++;
          }
      }
      else if (strcasecmp(parstr, "nick") == 0)
      {
          if (pr2str[0])
          {
              snprintf(net_player_name, sizeof(net_player_name), "%s", pr2str);
              snprintf(tmp_net_player_name, sizeof(net_player_name), "%s", pr2str);
              narg++;
          }
          else
          {
              WARNMSG("No player name given after -nick");
          }
      }
      else if (strcasecmp(parstr,"frameskip") == 0)
      {
         start_params.frame_skip = atoi(pr2str);
         narg++;
      } else
      if (strcasecmp(parstr,"framestep") == 0)
      {
         set_flag(start_params.debug_flags, DFlg_ShowGameTurns | DFlg_FrameStep);
      }
      else if (strcasecmp(parstr, "timer") == 0)
      {
          game_flags2 |= GF2_Timer;
          if (strcasecmp(pr2str, "game") == 0)
          {
              kfx_sim_state.TimerGame = true;
              narg++;
              if (strcasecmp(pr3str, "real") == 0)
              {
                  kfx_sim_state.TimerGameReal = true;
                  narg++;
              }
          }
          else if (strcasecmp(pr2str, "continuous") == 0)
          {
              kfx_sim_state.TimerNoReset = true;
              narg++;
          }
      }
      else if ( strcasecmp(parstr,"config") == 0 )
      {
        strcpy(start_params.config_file, pr2str);
        start_params.overrides[Clo_ConfigFile] = true;
        narg++;
      }
      else if ( strcasecmp(parstr,"Bullfrog") == 0 ) // force playing the Bullfrog video
      {
        set_flag(start_params.startup_flags, SFlg_Bullfrog);
      }
      else if ( strcasecmp(parstr,"ea") == 0 ) // force playing the EA video
      {
        set_flag(start_params.startup_flags, SFlg_EA);
      }
      else if (strcasecmp(parstr, "ftests") == 0)
      {
#ifdef FUNCTESTING
        if(ftest_parse_arg(pr2str)) // handle arg on ftest build
#else
        if(strlen(pr2str) > 0 && pr2str[0] != '-') // ignore arg on regular build
#endif // FUNCTESTING
        {
            ++narg;
        }

#ifdef FUNCTESTING
        set_flag(start_params.functest_flags, FTF_Enabled);
#else
        WARNLOG("Flag '%s' disabled for release builds.", parstr);
#endif // FUNCTESTING
      }
      else if (strcasecmp(parstr, "log") == 0)
      {
          narg++;
      }
      else if(strcasecmp(parstr, "exitonfailedtest") == 0)
      {
#ifdef FUNCTESTING
        set_flag(start_params.functest_flags, FTF_ExitOnTestFailure);
#else
       WARNLOG("Flag '%s' disabled for release builds.", parstr);
#endif // FUNCTESTING
      }
      else if(strcasecmp(parstr, "includelongtests") == 0)
      {
#ifdef FUNCTESTING
        set_flag(start_params.functest_flags, FTF_IncludeLongTests);
#else
       WARNLOG("Flag '%s' disabled for release builds.", parstr);
#endif // FUNCTESTING
      }
      else
      {
        // append bad parstr to bad_params string
        char param_buffer[128] = "";
        snprintf(param_buffer, sizeof(param_buffer), "%s%s", strnlen(bad_params, TEXT_BUFFER_LENGTH) > 0 ? ", " : "" , parstr);
        str_append(bad_params, sizeof(bad_params), param_buffer);
        bad_param=narg;
      }
      narg++;
  }

  if (level_num == LEVELNUMBER_ERROR)
  {
      if (first_singleplayer_level() > 0)
      {
          level_num = first_singleplayer_level();
      }
      else
      {
          level_num = 1;
      }
  }
  else {
      if (one_player_mode) {
          AssignCpuKeepers = 1;
      }
  }
  start_params.selected_level_number = level_num;
  my_player_number = default_loc_player;

#ifdef FUNCTESTING
  ftest_init(); // initialise test framework on ftest build
#endif

  if(bad_param != 0)
  {
    char message[TEXT_BUFFER_LENGTH];
    snprintf(message, sizeof(message), "Incorrect command line parameters: '%s'.\nPlease correct your Run options.", bad_params);
    warning_dialog(__func__, 0, message);
  }

  return (bad_param==0);
}

static const char* determine_log_filename(int64_t argument_count, char *argument_values[])
{
    for (int64_t argument_index = 1; argument_index < argument_count; argument_index++) {
        if (argument_values[argument_index] && (argument_values[argument_index][0] == '-' || argument_values[argument_index][0] == '/')) {
            char* argument_name = argument_values[argument_index] + 1;
            if (strcasecmp(argument_name, "log") == 0 && argument_index + 1 < argument_count) {
                remove("keeperfx.log");
                return argument_values[argument_index + 1];
            }
        }
    }
    return log_file_name;
}

static int64_t reset_game(void)
{
    SYNCDBG(6,"Starting");

    LbMouseSuspend();
    LbIKeyboardClose();
    RendererResetScreen(false);
    LbDataFreeAllV2(game_load_files);
    free_gui_strings_data();
    free_level_strings_data();
    FreeAudio();
    return 1;
}

int64_t LbBullfrogMain(int64_t argc, char *argv[])
{
    int64_t retval;
    retval=0;

    // Determine correct log file based on command line flags
    const char* selected_log_file_name = determine_log_filename(argc, argv);
    LbErrorLogSetup("/", selected_log_file_name, 5);

    retval = process_command_line(argc,argv);
    if (retval < 1)
    {
        LbErrorLogClose();
        return 0;
    }

    retval = true;
    retval &= (LbTimerInit() != Lb_FAIL);
    retval &= (RendererScreenInitialize() != Lb_FAIL);
    retval &= (RendererInit(RENDERER_SOFTWARE) != 0);
    static const struct RendererDrawCallbacks renderer_draw_callbacks_impl = {
        &draw_slab64k_background_immediate,
    };
    set_renderer_draw_callbacks(&renderer_draw_callbacks_impl);
    LbSetTitle(PROGRAM_NAME);
    LbSetIcon(1);
    RendererSetDoubleBuffering(true);
    srand(LbTimerClock());

#ifdef FUNCTESTING
    ftest_srand();
#endif // FUNCTESTING

    if (!retval)
    {
        static const char *msg_text="Basic engine initialization failed.\n";
        error_dialog_fatal(__func__, 1, msg_text);
        LbErrorLogClose();
        return 0;
    }

    retval = setup_game();
    if (retval == 1)
    {
        steam_api_init();
    }
    if (retval == 1)
    {
        if (is_dbc_language(install_info.lang_id))
        {            
            dbc_initialized = 1;
        }
        load_unifont_files();
    }
    if ( retval == 1 )
    {
        api_init_server();
        game_loop();
    }
    reset_game();
    RendererResetScreen(true);
    RendererShutdown();
    if ( retval == 0 )
    {
        static const char *msg_text="Setting up game failed.\n";
        error_dialog_fatal(__func__, 2, msg_text);
    } else
    if (retval == -1)
    {
        static const char* msg_text = " Game files which have to be copied from original DK are not present.\n\n";
        error_dialog_fatal(__func__, 2, msg_text);
    }
    else
    {
        SYNCDBG(0,"finished properly");
    }

    steam_api_shutdown();
    LbErrorLogClose();
    return 0;
}

int64_t kfxmain(int64_t argc, char *argv[])
{
  try {
  LbBullfrogMain(argc, argv);
  } catch (const std::exception &e)
  {
      char msg[512];
      snprintf(msg, sizeof(msg), "Exception raised: %s", e.what());
      error_dialog(__func__, 1, msg);
      return 1;
  } catch (...)
  {
      error_dialog(__func__, 1, "Exception raised!");
      return 1;
  }

#ifdef FUNCTESTING
  TbBool should_report_failure = flag_is_set(start_params.functest_flags, FTF_TestFailed) && flag_is_set(start_params.functest_flags, FTF_ExitOnTestFailure);
  if(flag_is_set(start_params.functest_flags, FTF_Enabled) && (flag_is_set(start_params.functest_flags, FTF_Abort) || should_report_failure))
  {
      return -1;
  }
#endif

  return 0;
}

#ifdef __cplusplus
}
#endif
