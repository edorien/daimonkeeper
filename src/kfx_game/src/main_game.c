/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file main_game.c
 * @author KeeperFX Team
 * @date 24 Sep 2021
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "main_game.h"
#include "config_keeperfx.h"
#include "bflib_basics.h"
#include "game_saves.h"
#include "bflib_coroutine.h"
#include "bflib_datetm.h"
#include "bflib_math.h"
#include "bflib_sound.h"

#include "ariadne_update.h"
#include "config.h"
#include "config_compp.h"
#include "config_settings.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "kfx_pathfinding_state.h"
#include "map_blocks.h"
#include "player_computer.h"
#include "packets.h"
#include "creature_states_combt.h"
#include "dungeon_data.h"
#include "engine_lenses.h"
#include "engine_redraw.h"
#include "engine_textures.h"
#include "game_heap.h"
#include "game_legacy.h"
#include "bflib_video.h"
#include "renderer/RendererManager.h"
#include "game_merge.h"
#include "game_lifecycle.h"
#include "lvl_script.h"
#include "config_sounds.h"
#include "config_slabsets.h"
#include "lvl_filesdk1.h"
#include "light_data.h"
#include "light_registry.h"
#include "map_data.h"
#include "map_columns.h"
#include "map_ceiling.h"
#include "map_events.h"
#include "actionpt.h"
#include "net_exchange_common.h"
#include "net_exchange_gameplay.h"
#include "net_game.h"
#include "net_lobby.h"
#include "net_resync.h"
#include "room_library.h"
#include "room_list.h"
#include "power_specials.h"
#include "magic_powers.h"
#include "player_data.h"
#include "player_instances.h"
#include "player_utils.h"
#include "vidmode.h"
#include "custom_sprites.h"
#include "sounds.h"
#include "net_resync.h"
#include "timer.h"
#include "game_replay.h"

#ifdef FUNCTESTING
  #include "ftests/ftest.h"
#endif

#include "ports/script_port.h"
#include "ports/ui_port.h"
#include "ports/audio_port.h"
#include "local_state.h"
#include "agent_memory.h"
#include "compat_report.h"
#include "game_compat_review.h"
#include "render_creature_view.h"
#include "post_inc.h"

// force_player_num now lives in kfx_config's struct StartupParameters
// (start_params). See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
extern TbBool IMPRISON_BUTTON_DEFAULT;
extern TbBool FLEE_BUTTON_DEFAULT;
extern uint64_t features_enabled;

extern void setup_players_count();
extern void set_skip_heart_zoom_feature(TbBool enable);

CoroutineLoopState set_not_has_quit(CoroutineLoop *context);
TbBool luascript_loaded = false;
/**
 * Resets timers and flags of all players into default (zeroed) state.
 * Also enables spells which are always enabled by default.
 */
void reset_script_timers_and_flags(void)
{
    struct Dungeon *dungeon;
    int64_t plyr_idx;
    int64_t k;
    TbBool freeplay = is_map_pack();
    for (plyr_idx=0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        add_power_to_player(PwrK_HAND, plyr_idx);
        add_power_to_player(PwrK_SLAP, plyr_idx);
        add_power_to_player(PwrK_POSSESS, plyr_idx);
        dungeon = get_dungeon(plyr_idx);
        for (k=0; k<TURN_TIMERS_COUNT; k++)
        {
            memset(&dungeon->turn_timers[k], 0, sizeof(struct TurnTimer));
            dungeon->turn_timers[k].state = 0;
        }
        for (k=0; k<SCRIPT_FLAGS_COUNT; k++)
        {
            dungeon->script_flags[k] = 0;
            if (freeplay && (plyr_idx < PLAYERS_FOR_CAMPAIGN_FLAGS) && (k < CAMPAIGN_FLAGS_PER_PLAYER))
            {
                intralvl.campaign_flags[plyr_idx][k] = 0;
            }
        }
    }
}

static void init_player_types()
{
    for (size_t plr_idx = 0; plr_idx < PLAYERS_COUNT; plr_idx++)
    {
        struct PlayerInfo *player;
        player = get_player(plr_idx);
        switch (plr_idx)
        {
        case PLAYER_GOOD:
            player->allocflags |= PlaF_Allocated;
            player->allocflags |= PlaF_CompCtrl;
            player->player_type = PT_Roaming;
            player->id_number = plr_idx;
            break;
        case PLAYER_NEUTRAL:
            player->player_type = PT_Neutral;
            break;
        default:
            player->player_type = PT_Keeper;
            break;
        }
    }
}

/******************************************************************************/

/**
 * Clears game structures at end of level.
 * Also used as part of clearing before new level is loaded.
 */
void clear_game_for_summary(void)
{
    SYNCDBG(6,"Starting");
    delete_all_structures();
    clear_shadow_limits(&lish);
    light_registry_reset_lighting();
    clear_stat_light_map();
    clear_mapwho();
    kfx_sim_state.entrance_room_id = 0;
    kfx_sim_state.action_random_seed = 0;
    kfx_sim_state.ai_random_seed = 0;
    kfx_sim_state.player_random_seed = 0;
    kfx_sim_state.operation_flags &= ~GOF_Paused;
    clear_columns();
    clear_action_points();
    clear_players();
    memset(&local_state, 0, sizeof(local_state));
    clear_dungeons();
}

void clear_game(void)
{
    SYNCDBG(6,"Starting");
    clear_game_for_summary();
    kfx_game_state.music_track = 0;
    clear_map();
    clear_computer();
    clear_script();
    clear_events();
    clear_things_and_persons_data();
    ceiling_set_info(12, 4, 1);
    init_animating_texture_maps();
    clear_slabsets();
    kfx_net_state.skip_initial_input_turns = 0;
    initialize_packet_history();
}

void reinit_level_after_load(void)
{
    struct PlayerInfo *player;
    int64_t i;
    SYNCDBG(6,"Starting");
    // Reinit structures from within the game
    player = get_my_player();
    local_state.lens_palette = 0;
    local_state.main_palette = engine_palette;
    // A loaded save or a resync replaced kfx_sim_state wholesale, map size
    // included, without going through set_map_size().
    ariadne_set_map_dimensions(kfx_sim_state.map_subtiles_x, kfx_sim_state.map_subtiles_y, map_subtiles_z);
    // The navigation mesh came with the save or the resync (ariadne_saved_state.h): not built again from the map,
    // which gives a different mesh that creatures path through differently (refactor pass 4, P4-F7).
    reinit_packets_after_load();
    kfx_sim_state.easter_eggs_enabled = start_params.easter_egg;
    ui_set_parchment_loaded(0);
    for (i=0; i < PLAYERS_COUNT; i++)
    {
        player = get_player(i);
        if (player_exists(player))
        {
            set_engine_view(player, player->view_mode);
            ui_update_panel_color_player_color(player->id_number, get_dungeon(i)->color_idx);
        }
    }
    start_rooms = &kfx_sim_state.rooms[1];
    end_rooms = &kfx_sim_state.rooms[ROOMS_COUNT];
    ui_update_room_tab_to_config();
    ui_update_powers_tab_to_config();
    ui_update_trap_tab_to_config();
    load_texture_map_file(kfx_config_state.texture_id, get_loaded_level_number(), get_level_fgroup(get_loaded_level_number()));
    init_animating_texture_maps();
    ui_init_gui();
    ui_reset_gui_based_on_player_mode();
    ui_clear_top_message_stats();
    player = get_my_player();
    reinit_tagged_blocks_for_player(player->id_number);
    restore_computer_player_after_load();
    net_restore_external_seats_after_load();
    sound_reinit_after_load();
    ui_update_panel_colors();
    reset_postal_instance_cache();
}

void set_general_information(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y)
{
    set_general_information_with_icon(msg_id, plyr_idx, target, x, y, -1);
}

void set_general_information_with_icon(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx)
{
    struct PlayerInfo *player = get_player(plyr_idx);
    MapCoord pos_x = 0;
    MapCoord pos_y = 0;
    find_map_location_coords(target, &x, &y, plyr_idx, __func__);
    if ((x != 0) || (y != 0))
    {
        pos_y = subtile_coord_center(y);
        pos_x = subtile_coord_center(x);
    }
    struct Event* event = event_create_event(pos_x, pos_y, EvKind_Information, player->id_number, -msg_id);
    if (!event_is_invalid(event))
        event->icon_idx = icon_idx;
}

void set_quick_information_with_icon(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx)
{
    struct PlayerInfo *player = get_player(plyr_idx);
    MapCoord pos_x = 0;
    MapCoord pos_y = 0;
    find_map_location_coords(target, &x, &y, plyr_idx, __func__);
    if ((x != 0) || (y != 0))
    {
        pos_y = subtile_coord_center(y);
        pos_x = subtile_coord_center(x);
    }
    struct Event* event = event_create_event(pos_x, pos_y, EvKind_QuickInformation, player->id_number, -msg_id);
    if (!event_is_invalid(event))
        event->icon_idx = icon_idx;
}

void set_quick_information(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y)
{
    set_quick_information_with_icon(msg_id, plyr_idx, target, x, y, -1);
}

void process_objective(const char *msg_text, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y)
{
    process_objective_with_icon(msg_text, plyr_idx, target, x, y, -1);
}

void process_objective_with_icon(const char *msg_text, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx)
{
    struct PlayerInfo *player = get_player(plyr_idx);
    find_map_location_coords(target, &x, &y, plyr_idx, __func__);
    ui_set_level_objective(player->id_number, msg_text);
    ui_display_objectives_with_icon(player->id_number, x, y, icon_idx);
}

void set_general_objective(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y)
{
    set_general_objective_with_icon(msg_id, plyr_idx, target, x, y, -1);
}

void set_general_objective_with_icon(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx)
{
    process_objective_with_icon(get_string(msg_id), plyr_idx, target, x, y, icon_idx);
}

int64_t lose_level(struct PlayerInfo *player)
{
    if (!is_my_player(player))
        return false;
    if (network_is_active())
    {
        LbNetwork_Stop();
    }
    quit_game = 1;
    return true;
}

int64_t resign_level(struct PlayerInfo *player)
{
    if (!is_my_player(player))
        return false;
    if (network_is_active())
    {
        LbNetwork_Stop();
    }
    quit_game = 1;
    return true;
}

int64_t complete_level(struct PlayerInfo *player)
{
    SYNCDBG(6,"Starting");
    if (!is_my_player(player))
        return false;
    if (network_is_active())
    {
        LbNetwork_Stop();
        quit_game = 1;
        return true;
    }
    LevelNumber lvnum;
    lvnum = get_continue_level_number();
    if (get_loaded_level_number() == lvnum)
    {
        SYNCDBG(7,"Progressing the campaign");
        move_campaign_to_next_level();
    }
    quit_game = 1;
    return true;
}

int64_t default_loc_player = 0;

// docs/refactor/editor/01-entry-and-editor-session.md §5 -- armed by
// editor_request_blank_map(), consumed once by init_level() below.
static TbBool s_editor_blank_map_pending = false;
static MapSlabCoord s_editor_blank_map_w = 0;
static MapSlabCoord s_editor_blank_map_h = 0;
static int64_t s_editor_blank_map_texture = 0;

void editor_request_blank_map(MapSlabCoord tiles_x, MapSlabCoord tiles_y, int64_t texture_set)
{
    s_editor_blank_map_pending = true;
    s_editor_blank_map_w = tiles_x;
    s_editor_blank_map_h = tiles_y;
    s_editor_blank_map_texture = texture_set;
}

static TbBool init_level(void)
{
    SYNCDBG(6,"Starting");
    compat_report_clear(); // collects this level's unsupported content until post_init_level()
    struct IntralevelData transfer_mem;
    //memcpy(&transfer_mem,&game.intralvl.transferred_creature,sizeof(struct CreatureStorage));
    memcpy(&transfer_mem,&intralvl,sizeof(struct IntralevelData));
    kfx_game_state.flags_gui = GGUI_SoloChatEnabled;
    clear_flag(kfx_sim_state.system_flags, GSF_RunAfterVictory);
    free_swipe_graphic();
    kfx_sim_state.play_gameturn = 0;
    kfx_game_state.paused_at_gameturn = false;
    game_flags2 &= (GF2_PERSISTENT_FLAGS | GF2_Timer);
    clear_game();
    reset_heap_manager();
    lens_mode = 0;
    setup_heap_manager();

    wait_for_all_players();
    init_seeds();
    sync_initial_network_seed();

    recheck_all_mod_exist();

    luascript_loaded = script_open_lua_script(get_selected_level_number());
    // Restore campaign-layer sounds before creature configs load, so creature cfg custom
    // sounds are added to the already-restored bank (not wiped afterwards).
    sound_restore_to_campaign_snapshot();
    // Load configs which may have per-campaign part, and can even be modified within a level
    level_load_time_phase(LevelLoadTime_Sprites);
    init_custom_sprites(get_selected_level_number());
    level_load_time_phase(LevelLoadTime_Configs);
    load_stats_files();
    level_load_time_phase(LevelLoadTime_GameSetup);
    check_and_auto_fix_stats();

    // We should do this after 'load stats'
    ui_update_room_tab_to_config();
    ui_update_powers_tab_to_config();
    ui_update_trap_tab_to_config();

    init_creature_scores();

    init_player_types();
    light_set_lights_on(1);
    start_rooms = &kfx_sim_state.rooms[1];
    end_rooms = &kfx_sim_state.rooms[ROOMS_COUNT];

    ui_clear_top_message_stats();
    init_dungeons();
    ui_setup_panel_colors();
    // This early call above runs before this level's own map/camera has
    // rendered a single frame, so on any level after the first in this
    // process it builds the minimap panel's colours from a previous
    // level's background capture under whatever palette was active at
    // this early point -- not this level's. Force the lazy in-game
    // rebuild (frontmenu_ingame_map.c's auto_gen_tables(), invoked from
    // the first real panel_map_draw_slabs() call) to run again once this
    // level is actually rendering, so it captures and rebuilds correctly.
    ui_reset_panel_map_background_cache();
    init_map_size(get_selected_level_number());
    audio_clear_sound_messages();
    
    // Load the actual level files
    LevelNumber level = get_selected_level_number();
    level_load_time_phase(LevelLoadTime_Data);
    TbBool script_preloaded = preload_script(level);
    // docs/refactor/editor/01-entry-and-editor-session.md §5 -- New Map:
    // a one-shot request armed by editor_request_blank_map() builds a
    // fresh blank map instead of reading `level` from disk. Everything
    // else below (navigation, player init, ...) runs unchanged either way.
    TbBool map_loaded;
    if (s_editor_blank_map_pending)
    {
        map_loaded = create_blank_map(level, s_editor_blank_map_w, s_editor_blank_map_h, s_editor_blank_map_texture);
        s_editor_blank_map_pending = false;
    }
    else
    {
        map_loaded = load_map_file(level);
    }
    if (!map_loaded)
    {
        ui_create_frontend_error_box(15000, "Map content is missing or incompatible.");
        JUSTMSG("Unable to load level %" PRIu64 " from %s", (uint64_t)(level), campaign.name);
        return false;
    }
    level_load_time_phase(LevelLoadTime_GameSetup);
    if (script_preloaded == false && luascript_loaded == false)
    {
        char no_script_msg[MESSAGE_TEXT_LEN];
        snprintf(no_script_msg, sizeof(no_script_msg), "%s: No Script %" PRIu64, get_string(GUIStr_Error), (uint64_t)(level));
        ui_show_onscreen_msg(200, no_script_msg);
        JUSTMSG("Unable to load script level %" PRIu64 " from %s", (uint64_t)(level), campaign.name);
    }
    level_load_time_phase(LevelLoadTime_Navigation);
    init_navigation();
    level_load_time_phase(LevelLoadTime_GameSetup);
    snprintf(kfx_game_state.campaign_fname, sizeof(kfx_game_state.campaign_fname), "%s", campaign.fname);
    light_set_lights_on(1);
    {
        for (size_t i = 0; i < PLAYERS_COUNT; i++)
        {
            if(player_is_roaming(i))
            {
                struct PlayerInfo *player;
                player = get_player(i);
                init_player_start(player, false);
            }
        }
    }
    kfx_sim_state.view_mode_flags |= GNFldD_ComputerPlayerProcessing;
    //memcpy(&game.intralvl.transferred_creature,&transfer_mem,sizeof(struct CreatureStorage));
    memcpy(&intralvl,&transfer_mem,sizeof(struct IntralevelData));
    event_initialise_all();
    battle_initialise();
    ambient_sound_prepare();
    ui_zero_messages();
    kfx_sim_state.armageddon_cast_turn = 0;
    kfx_sim_state.armageddon_over_turn = 0;
    audio_clear_sound_messages();
    show_ignored_fxdata_zip_messages();
    kfx_sim_state.creatures_tend_imprison = 0;
    kfx_sim_state.creatures_tend_flee = 0;
    memset(kfx_config_state.pay_day_progress, 0, sizeof(kfx_config_state.pay_day_progress));
    kfx_sim_state.chosen_room_kind = 0;
    kfx_sim_state.chosen_room_spridx = 0;
    kfx_sim_state.chosen_room_tooltip = 0;
    set_chosen_power_none();
    kfx_sim_state.manufactr_element = 0;
    kfx_sim_state.manufactr_spridx = 0;
    kfx_sim_state.manufactr_tooltip = 0;
    reset_postal_instance_cache();
    JUSTMSG("Started level %" PRIu64 " from %s", (uint64_t)(get_selected_level_number()), campaign.name);

    script_api_event("GAME_STARTED");
    return true;
}

static void post_init_level(void)
{
    SYNCDBG(8,"Starting");
    if (kfx_net_state.packet_save_enable)
        open_new_packet_file_for_save();
    calculate_dungeon_area_scores();
    init_animating_texture_maps();
    reset_creature_max_levels();
    clear_creature_pool();
    setup_computer_players2();
    load_script(get_loaded_level_number());
    compat_review_after_level_load(get_loaded_level_number());
    script_lua_on_game_start();
    init_dungeons_research();
    init_dungeons_essential_position();
    if (!is_map_pack())
    {
        create_transferred_creatures_on_level();
    }
    update_dungeons_scores();
    update_dungeon_generation_speeds();
    init_traps();
    init_all_creature_states();
    init_keepers_map_exploration();
    SYNCDBG(9,"Finished");
}

/******************************************************************************/

TbBool startup_saved_packet_game(void)
{
    struct CatalogueEntry centry;
    clear_packets();
    if (!open_packet_file_for_load(kfx_net_state.packet_fname,&centry))
    {
        ERRORLOG("Cannot replay \"%s\": unreadable, or written by an incompatible version",
            kfx_net_state.packet_fname);
        return false;
    }
    if (!change_campaign(CampgnT_Default, centry.campaign_fname))
    {
        ERRORLOG("Unable to load campaign associated with packet file");
    }
    set_selected_level_number(kfx_net_state.packet_save_head.level_num);
    RendererSetDrawColour(kfx_sim_state.colours[15][15][15]);
    kfx_net_state.pckt_gameturn = 0;
    if (KFX_DEBUG_ON(0))
    {
        SYNCDBG(0,"Initialising level %" PRId64, (int64_t)get_selected_level_number());
        SYNCMSG("Packet Loading Active (File contains %" PRIu64 " turns)", (uint64_t)(kfx_net_state.turns_stored));
        SYNCMSG("Packet Checksum Verification %s",kfx_net_state.packet_checksum_verify ? "Enabled" : "Disabled");
        SYNCMSG("Fast Forward through %" PRIu64 " game turns", (uint64_t)(kfx_net_state.turns_fastforward));
        if (kfx_net_state.turns_packetoff != -1)
            SYNCMSG("Packet Quit at %" PRIu64, (uint64_t)(kfx_net_state.turns_packetoff));
        if (kfx_sim_state.replay_active)
        {
          if (kfx_net_state.log_things_end_turn != kfx_net_state.log_things_start_turn)
            SYNCMSG("Logging things, game turns %" PRIu64 " -> %" PRIu64, (uint64_t)(kfx_net_state.log_things_start_turn), (uint64_t)(kfx_net_state.log_things_end_turn));
        }
        SYNCMSG("Packet file prepared on " PRODUCT_NAME " %" PRId64 ".%" PRId64 ".%" PRId64 ".%" PRId64,(int64_t)kfx_net_state.packet_save_head.game_ver_major,(int64_t)kfx_net_state.packet_save_head.game_ver_minor,
            (int64_t)kfx_net_state.packet_save_head.game_ver_release,(int64_t)kfx_net_state.packet_save_head.game_ver_build);
    }
    if ((kfx_net_state.packet_save_head.game_ver_major != VER_MAJOR) || (kfx_net_state.packet_save_head.game_ver_minor != VER_MINOR)
        || (kfx_net_state.packet_save_head.game_ver_release != VER_RELEASE) || (kfx_net_state.packet_save_head.game_ver_build != VER_BUILD)) {
        WARNLOG("Packet file was created with different version of the game; this rarely works");
    }
    kfx_sim_state.game_kind = GKind_LocalGame;
    {
        PlayerNumber view_plyr = -1;
        NetUserId rec_user = kfx_net_state.packet_save_head.recording_user;
        if (!start_params.force_player_num && (rec_user >= 0) && (rec_user < MAX_NET_USERS))
            view_plyr = kfx_net_state.packet_save_head.user_players[rec_user];
        if (view_plyr < 0)
            view_plyr = kfx_sim_state.level_human_player;
        if (!flag_is_set(kfx_net_state.packet_save_head.players_exist, to_flag(view_plyr))
            || flag_is_set(kfx_net_state.packet_save_head.players_comp, to_flag(view_plyr)))
            my_player_number = 0;
        else
            my_player_number = view_plyr;
    }
    settings.isometric_view_zoom_level = kfx_net_state.packet_save_head.isometric_view_zoom_level;
    settings.frontview_zoom_level = kfx_net_state.packet_save_head.frontview_zoom_level;
    settings.isometric_tilt = kfx_net_state.packet_save_head.isometric_tilt;
    settings.highlight_mode = kfx_net_state.packet_save_head.highlight_mode;
    IMPRISON_BUTTON_DEFAULT = kfx_net_state.packet_save_head.default_imprison_tendency;
    FLEE_BUTTON_DEFAULT = kfx_net_state.packet_save_head.default_flee_tendency;
    set_skip_heart_zoom_feature(kfx_net_state.packet_save_head.skip_heart_zoom);
    if (!init_level())
        return false;
    setup_zombie_players();
    init_players(&kfx_net_state.packet_save_head);
    restore_users_from_packet_save();
    ui_set_frontend_alliances(kfx_net_state.packet_save_head.frontend_alliances);
    ui_setup_alliances();
    if (kfx_sim_state.human_players_count == 1)
        kfx_sim_state.game_kind = GKind_LocalGame;
    if (kfx_net_state.turns_stored < kfx_net_state.turns_fastforward)
        kfx_net_state.turns_fastforward = kfx_net_state.turns_stored;
    post_init_level();
    post_init_players();
    computer_players_set_dungeons(); // as after a load (P5-F21)
    set_selected_level_number(0);
    struct PlayerInfo* player = get_my_player();
    set_engine_view(player, rotate_mode_to_view_mode(kfx_net_state.packet_save_head.video_rotate_mode));
    return true;
}

static CoroutineLoopState startup_network_game_tail(CoroutineLoop *context);

void startup_network_game(CoroutineLoop *context, TbBool local)
{
    SYNCDBG(0,"Starting up network game");
    stop_streamed_samples();
    // A new level: no agent remembers anything about it yet (a loaded save brings its own, game_saves.c).
    agent_memory_clear_all();
    setup_count_players();
    if (local && (campaign.human_player >= 0) && (!start_params.force_player_num))
    {
        default_loc_player = campaign.human_player;
        kfx_sim_state.level_human_player = default_loc_player;
        my_player_number = default_loc_player;
    }
    if (!init_level()) {
        coroutine_clear(context, true);
        return;
    }
    //if (game.flagfield_14EA4A == 2) //was wrong because init_level sets this to 2. global variables are evil (though perhaps that's why they were chosen for DK? ;-))
    TbBool ShouldAssignCpuKeepers = 0;
    if (local)
    {
        kfx_sim_state.game_kind = GKind_LocalGame;
        net_clear_external_seats();
        init_players_local_game(&kfx_net_state.packet_save_head);
        net_apply_keeper_name(my_player_number);
        if (AssignCpuKeepers || campaign.assignCpuKeepers) {
            ShouldAssignCpuKeepers = 1;
        }
    } else
    {
        kfx_sim_state.game_kind = GKind_MultiGame;
        if (!init_players_network_game()) {
            coroutine_clear(context, true);
            return;
        }
    }
    setup_count_players(); // It is reset by init_level
    int64_t args[COROUTINE_ARGS] = {ShouldAssignCpuKeepers, 0};
    coroutine_add_args(context, &startup_network_game_tail, args);
}

static CoroutineLoopState startup_network_game_tail(CoroutineLoop *context)
{
    TbBool ShouldAssignCpuKeepers = coroutine_args(context)[0];
    if (kfx_sim_state.game_kind == GKind_MultiGame) {
        ui_setup_alliances();
    }
    if (ui_is_fe_computer_players_active() || ShouldAssignCpuKeepers)
    {
        SYNCDBG(5,"Setting up uninitialized players as computer players");
        setup_computer_players();
    } else
    {
        SYNCDBG(5,"Setting up uninitialized players as zombie players");
        setup_zombie_players();
    }
    // Only a normal level start is autosaved (AUTOSAVE_REPLAYS): not an editor playtest,
    // which runs post_init_level() from its own tail, nor a -packetsave/-packetload game.
    if (!kfx_net_state.packet_save_enable && !kfx_sim_state.replay_active)
        setup_auto_replay_save();
    post_init_level();
    post_init_players();
    computer_players_set_dungeons(); // as after a load (P5-F21)
    post_init_packets();
    if (kfx_sim_state.game_kind == GKind_LocalGame) {
        // Slots the Skirmish page marked External (docs/refactor/AI/LLM/01 M5): after the script has run, so the
        // keeper already exists, and consumed here so a later game does not inherit them.
        net_claim_pending_external_seats();
        // Campaign/scenario "Spectate" checkbox (docs/refactor/AI/LLM/01 M9b): same timing requirement as
        // above -- the local player's dungeon must already exist (post_init_players() just ran).
        if (ui_is_fe_spectate_campaign_active())
            player_enter_spectator_mode(my_player_number);
    }
    set_selected_level_number(0);

#ifdef FUNCTESTING
    set_flag(start_params.functest_flags, FTF_LevelLoaded);
#endif

    return CLS_CONTINUE;
}

/******************************************************************************/

static CoroutineLoopState startup_local_game_for_editor_tail(CoroutineLoop *context);

// docs/refactor/editor/01-entry-and-editor-session.md §4 -- "one player,
// zombie keepers, optionally-trimmed post-init, optionally-paused". Mirrors
// startup_network_game()'s local-game half above, minus the CPU-keeper /
// network-players branching an editor session never needs.
void startup_local_game_for_editor(CoroutineLoop *context, LevelNumber lvnum, TbBool suspend, TbBool trim_post_init)
{
    SYNCDBG(0,"Starting up editor session for level %" PRIu64, (uint64_t)lvnum);
    stop_streamed_samples();
    my_player_number = default_loc_player;
    set_selected_level_number(lvnum);
    if (!init_level()) {
        coroutine_clear(context, true);
        return;
    }
    kfx_sim_state.game_kind = GKind_LocalGame;
    init_players_local_game(&kfx_net_state.packet_save_head);
    setup_count_players(); // It is reset by init_level
    int64_t args[COROUTINE_ARGS] = {trim_post_init, suspend};
    coroutine_add_args(context, &startup_local_game_for_editor_tail, args);
}

static CoroutineLoopState startup_local_game_for_editor_tail(CoroutineLoop *context)
{
    TbBool trim_post_init = coroutine_args(context)[0];
    TbBool suspend = coroutine_args(context)[1];
    SYNCDBG(5,"Setting up uninitialized players as zombie players");
    setup_zombie_players();
    if (trim_post_init)
    {
        // §4's trimmed post_init_level(): keep just what correct rendering
        // needs, drop lua_on_game_start (arbitrary user Lua),
        // create_transferred_creatures_on_level and generation-speed setup.
        init_traps();
        init_all_creature_states();
        init_keepers_map_exploration();
    }
    else
    {
        // Playtest (§6): run the same full startup a normal level gets.
        post_init_level();
    }
    post_init_players();
    computer_players_set_dungeons(); // as after a load (P5-F21)
    post_init_packets();
    set_selected_level_number(0);
    // docs/refactor/editor/01-entry-and-editor-session.md §3, revised after
    // live testing: simulation_suspended, not GOF_Paused -- the latter also
    // blocks get_packet_control_mouse_clicks() (front_input.c) from
    // generating any click packet at all, which an editor session needs
    // (world clicks are how tools actually place/build). editor_open()
    // (kfx_editor) re-asserts this every frame once the session is active;
    // set here too so the sim is already frozen for the brief window
    // between this coroutine step finishing and editor_open() running.
    if (suspend)
        kfx_sim_state.simulation_suspended = true;

#ifdef FUNCTESTING
    set_flag(start_params.functest_flags, FTF_LevelLoaded);
#endif

    return CLS_CONTINUE;
}

/******************************************************************************/

void faststartup_network_game(CoroutineLoop *context)
{
    SYNCDBG(3,"Starting");
    reenter_video_mode();
    my_player_number = default_loc_player;
    kfx_sim_state.game_kind = GKind_LocalGame;
    if (!is_campaign_loaded())
    {
        if (!change_campaign(CampgnT_Default,""))
            ERRORLOG("Unable to load campaign");
    }
    startup_network_game(context, true);
    if (!context->error)
        coroutine_add(context, &set_not_has_quit);
}

CoroutineLoopState set_not_has_quit(CoroutineLoop *context)
{
    level_load_time_phase(LevelLoadTime_Total);
    get_my_player()->display_flags &= ~PlaF6_PlyrHasQuit;
    return CLS_CONTINUE;
}

void faststartup_saved_packet_game(void)
{
    reenter_video_mode();
    startup_saved_packet_game();
    {
        struct PlayerInfo *player;
        player = get_my_player();
        player->display_flags &= ~PlaF6_PlyrHasQuit;
    }
    ui_set_gui_visible(false);
    clear_flag(kfx_sim_state.operation_flags, GOF_ShowPanel);
}

/******************************************************************************/

/**
 * Clears the Game structure completely, and copies startup parameters
 * from start_params structure.
 */
void clear_complete_game(void)
{
    memset(&game, 0, sizeof(struct Game));
    // kfx_sim's own field group, migrated out of struct Game incrementally
    // (stage 6.7, docs/refactor/stage-06-kfx-sim.md) -- grows via sizeof()
    // as later increments add fields, no further edit needed here.
    memset(&kfx_sim_state, 0, sizeof(struct KfxSimState));
    // ariadne's own navigation-map cache (stage-06a-ariadne-pathfinding-
    // interface.md, physical-split notes) -- zeroed here purely to match
    // every sibling state struct's "cleared at complete-game-clear time"
    // invariant; init_navigation() always rebuilds it before it's read.
    memset(&kfx_pathfinding_state, 0, sizeof(struct KfxPathfindingState));
    // kfx_net's own field group, migrated out of struct Game (stage 8.3,
    // docs/refactor/stage-08-kfx-net.md) -- same "grows via sizeof()"
    // shape as kfx_sim_state above.
    memset(&kfx_net_state, 0, sizeof(struct KfxNetState));
    // kfx_game's own field group, migrated out of struct Game (stage 9.3,
    // docs/refactor/stage-09-kfx-game.md) -- same "grows via sizeof()"
    // shape as kfx_sim_state/kfx_net_state above.
    memset(&kfx_game_state, 0, sizeof(struct KfxGameState));
    // kfx_frontend's own field group, migrated out of struct Game/
    // keeperfx.hpp (stage 10.1, docs/refactor/stage-10-kfx-frontend.md) --
    // reset via UiPort since kfx_frontend owns the type.
    ui_reset_frontend_state();
    memset(&intralvl, 0, sizeof(struct IntralevelData));
    kfx_net_state.turns_packetoff = -1;
    kfx_sim_state.level_human_player = default_loc_player;
    kfx_net_state.packet_checksum_verify = start_params.packet_checksum_verify;
    // Set levels to 0, as we may not have the campaign loaded yet
    set_continue_level_number(first_singleplayer_level());
    if ((start_params.operation_flags & GOF_SingleLevel) != 0)
        set_selected_level_number(start_params.selected_level_number);
    else
        set_selected_level_number(first_singleplayer_level());
    kfx_sim_state.turns_per_second = start_params.num_fps;
    fps_limit_current = 0;
    fps_limit_main = start_params.num_fps_draw_main;
    fps_limit_secondary = start_params.num_fps_draw_secondary;
    kfx_sim_state.mode_flags = start_params.mode_flags;
    kfx_sim_state.easter_eggs_enabled = start_params.easter_egg;
    set_flag_value(kfx_sim_state.system_flags, GSF_AllowOnePlayer, start_params.one_player);
    kfx_sim_state.computer_chat_flags = start_params.computer_chat_flags;
    kfx_sim_state.operation_flags = start_params.operation_flags;
    snprintf(kfx_net_state.packet_fname,150, "%s", start_params.packet_fname);
    kfx_net_state.packet_save_enable = start_params.packet_save_enable;
    kfx_sim_state.replay_active = start_params.packet_load_enable;
    my_player_number = default_loc_player;
}

void init_seeds()
{
#if FUNCTESTING
    if (flag_is_set(start_params.functest_flags, FTF_Enabled))
    {
        ftest_srand();
    }
    else
#endif
    {
        // Unsynced seeds - these values will be different per-player in multiplayer
        uint64_t calender_time = (uint64_t)LbTimeSec();
        kfx_sim_state.unsync_random_seed = calender_time * 9007 + 9011;  // Use prime multipliers for different seeds
        kfx_sim_state.sound_random_seed = calender_time * 7919 + 7927;

        // If doing -packetload then use the replay's stored seed
        if ((kfx_net_state.packet_save_head.action_seed != 0) && (kfx_sim_state.replay_active == true)) {
            kfx_sim_state.action_random_seed = kfx_net_state.packet_save_head.action_seed;
        } else {
            kfx_sim_state.action_random_seed = calender_time * 9311 + 9319;
        }

        kfx_sim_state.ai_random_seed = kfx_sim_state.action_random_seed * 9377 + 9391;
        kfx_sim_state.player_random_seed = kfx_sim_state.action_random_seed * 9473 + 9479;
        
        initial_replay_seed = kfx_sim_state.action_random_seed;
    }
}
