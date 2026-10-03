/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ui_port_impl.c
 *     kfx_frontend's UiPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "ui_port_impl.h"
#include "gui_topmsg.h"
#include "gui_msgs.h"
#include "front_lvlstats.h"
#include "kjm_input.h"
#include "frontend.h"
#include "gui_frontmenu.h"
#include "frontmenu_ingame_tabs.h"
#include "front_input.h"
#include "frontmenu_ingame_evnt.h"
#include "gui_boxmenu.h"
#include "front_network.h"
#include "gui_soundmsgs.h"
#include "frontmenu_ingame_map.h"
#include "kfx_frontend_state.h"
#include "frame_compose.h"
#include "gui_parchment.h"
#include "gui_draw.h"
#include "gui_tooltips.h"
#include "front_highscore.h"
#include "config_strings.h"
#include "config_keeperfx.h"
#include "local_view.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Non-variadic wrapper -- show_onscreen_msg() itself is
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

// map_events.c can't reach kfx_frontend's event_button_info[] directly.
static const struct EventTypeInfo *get_event_button_info(EventKind evkind) { return &event_button_info[evkind]; }

// gui_tooltips.c owns tool_tip_box.
static void hide_tooltip(void)
{
    clear_flag(tool_tip_box.flags, TTip_Visible);
}

// frontmenu_ingame_evnt.c owns TimerTurns.
static void set_timer_turns(uint64_t turns)
{
    TimerTurns = turns;
}

// Wrappers for kfx_game's calls (the entries that were GameCallbacks).
static TbBool game_callbacks_is_fe_computer_players_active(void)
{
    return fe_computer_players != 0;
}

static TbBool game_callbacks_is_fe_spectate_campaign_active(void)
{
    return fe_spectate_campaign != 0;
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

static void game_callbacks_set_frontend_alliances(char alliances) { frontend_alliances = alliances; }

static char game_callbacks_get_frontend_alliances(void) { return frontend_alliances; }
// Wrappers for kfx_net's calls (the entries that were NetCallbacks).
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

// Wrappers for kfx_render's calls (the entries that were
// RenderOverlayCallbacks) -- bundle multiple gui/frontend
// calls together where engine_redraw.c always invoked them as one unit,
// and provide get/set accessors for globals a plain callback can't
// express directly.
static void render_overlay_set_parchment_loaded(int64_t val)
{
    parchment_loaded = val;
}

// gui_parchment.c owns parchment_loaded.
static TbBool is_parchment_loaded(void)
{
    return parchment_loaded;
}

// draw_gui_panel_sprite_left is itself a macro (expands to
// draw_gui_panel_sprite_left_player(...,my_player_number)), so this
// wrapper needs a distinct name.
static void render_overlay_draw_gui_panel_sprite_left(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx)
{
    draw_gui_panel_sprite_left(x, y, units_per_px, spridx);
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

static TbBool render_overlay_game_is_busy_doing_gui(void)
{
    return game_is_busy_doing_gui() != 0;
}

static TbBool render_overlay_game_is_busy_doing_gui_string_input(void)
{
    return game_is_busy_doing_gui_string_input() != 0;
}


const struct UiPort kfx_frontend_ui_port = {
    .report_error_stat = &erstat_inc,
    .show_onscreen_msg = &show_onscreen_msg_plain,
    .clear_messages_from_player = &clear_messages_from_player,
    .targeted_message_add = &targeted_message_add_plain,
    .message_add_vfmt = &message_add_vfmt,
    .message_add = &message_add,
    .zero_messages = &zero_messages,
    .show_real_time_taken = &show_real_time_taken,
    .set_room_type_highlighted = &sim_feedback_set_room_type_highlighted,
    .set_visible_event_idx = &sim_feedback_set_visible_event_idx,
    .clear_all_event_button_states = &sim_feedback_clear_all_event_button_states,
    .clear_event_button_state = &sim_feedback_clear_event_button_state,
    .mark_event_button_read = &sim_feedback_mark_event_button_read,
    .is_battle_creature_over_active = &sim_feedback_is_battle_creature_over_active,
    .get_event_button_info = &get_event_button_info,
    .frontstats_initialise = &frontstats_initialise,
    .mouse_is_over_panel_map = &mouse_is_over_panel_map,
    .toggle_status_menu = &toggle_status_menu,
    .turn_off_roaming_menus = &turn_off_roaming_menus,
    .initialise_tab_tags_and_menu = &initialise_tab_tags_and_menu,
    .init_gui = &init_gui,
    .set_gui_visible = &set_gui_visible,
    .update_player_objectives = &update_player_objectives,
    .create_message_box = &create_message_box,
    .turn_on_menu = &turn_on_menu,
    .turn_off_menu = &turn_off_menu,
    .turn_off_query_menus = &turn_off_query_menus,
    .turn_off_all_menus = &turn_off_all_menus,
    .turn_on_main_panel_menu = &turn_on_main_panel_menu,
    .turn_off_all_panel_menus = &turn_off_all_panel_menus,
    .turn_off_event_box_if_necessary = &turn_off_event_box_if_necessary,
    .refresh_active_button_sprites_for_player = &refresh_active_button_sprites_for_player,
    .find_next_room_of_type = &find_next_room_of_type,
    .update_time = &update_time,
    .get_game_time = &get_game_time,
    .get_zoom_key_room_order = &get_zoom_key_room_order,
    .hide_tooltip = &hide_tooltip,
    .timer_enabled = &timer_enabled,
    .set_timer_turns = &set_timer_turns,
    .toggle_main_cheat_menu = &toggle_main_cheat_menu,
    .toggle_instance_cheat_menu = &toggle_instance_cheat_menu,
    .toggle_secondary_cheat_menu = &toggle_secondary_cheat_menu,
    .toggle_creature_cheat_menu = &toggle_creature_cheat_menu,
    .close_main_cheat_menu = &close_main_cheat_menu,
    .close_instance_cheat_menu = &close_instance_cheat_menu,
    .close_secondary_cheat_menu = &close_secondary_cheat_menu,
    .close_creature_cheat_menu = &close_creature_cheat_menu,
    .create_error_box = &create_error_box,
    .is_fe_computer_players_active = &game_callbacks_is_fe_computer_players_active,
    .is_fe_spectate_campaign_active = &game_callbacks_is_fe_spectate_campaign_active,
    .menu_is_active = &menu_is_active,
    .toggle_debug_network_stats = &game_callbacks_toggle_debug_network_stats,
    .is_bonus_timer_enabled = &bonus_timer_enabled,
    .go_to_my_next_room_of_type = &go_to_my_next_room_of_type,
    .get_button_designation = &get_button_designation,
    .gui_set_button_flashing = &gui_set_button_flashing,
    .create_gui_box = &gui_create_box,
    .show_game_time_taken = &show_game_time_taken,
    .toggle_tooltip_land_coord = &game_callbacks_toggle_tooltip_land_coord,
    .get_high_score_entry = &game_callbacks_get_high_score_entry,
    .set_high_score_entry = &game_callbacks_set_high_score_entry,
    .setup_alliances = &setup_alliances,
    .script_play_message = &script_play_message,
    .clear_top_message_stats = &erstats_clear,
    .set_level_objective = &set_level_objective,
    .display_objectives_with_icon = &display_objectives_with_icon,
    .reset_gui_based_on_player_mode = &reset_gui_based_on_player_mode,
    .update_panel_colors = &update_panel_colors,
    .save_frontend_state = &save_frontend_state,
    .load_frontend_state = &load_frontend_state,
    .reset_frontend_state = &reset_frontend_state,
    .get_frontend_state_size = &get_frontend_state_size,
    .set_frontend_alliances = &game_callbacks_set_frontend_alliances,
    .frontend_save_continue_game = &frontend_save_continue_game,
    .get_frontend_alliances = &game_callbacks_get_frontend_alliances,
    .instant_instance_selected = &instant_instance_selected,
    .is_onscreen_msg_visible = &is_onscreen_msg_visible,
    .panel_map_update = &panel_map_update,
    .update_trap_tab_to_config = &update_trap_tab_to_config,
    .enter_net_session_screen = &net_callbacks_enter_net_session_screen,
    .set_lobby_button_labels = &net_callbacks_set_lobby_button_labels,
    .create_frontend_error_box = &create_frontend_error_box,
    .is_frontend_starting_mp_level = &net_callbacks_is_frontend_starting_mp_level,
    .is_frontend_at_initial_state = &net_callbacks_is_frontend_at_initial_state,
    .display_attempting_to_join_message = &display_attempting_to_join_message,
    .attempting_to_join_cancel_requested = &attempting_to_join_cancel_requested,
    .reset_attempting_to_join_cancel = &reset_attempting_to_join_cancel,
    .process_network_error = &process_network_error,
    .frontnet_service_selected = &net_callbacks_frontnet_service_selected,
    .resync_export_frontend_state = &resync_export_frontend_state,
    .resync_import_frontend_state = &resync_import_frontend_state,
    .redraw_gameplay_frame = &keeper_screen_redraw,
    .draw_out_of_sync_box = &draw_out_of_sync_box,
    .process_frontend_chat_message = &process_frontend_chat_message,
    .set_parchment_loaded = &render_overlay_set_parchment_loaded,
    .is_parchment_loaded = &is_parchment_loaded,
    .reload_parchment_file = &reload_parchment_file,
    .point_to_overhead_map = &point_to_overhead_map,
    .draw_panel_sprite_left = &render_overlay_draw_gui_panel_sprite_left,
    .draw_gui_panel_sprite_centered = &draw_gui_panel_sprite_centered,
    .draw_button_sprite_left = &draw_button_sprite_left,
    .get_status_panel_width = &render_overlay_get_status_panel_width,
    .game_is_busy_doing_gui = &render_overlay_game_is_busy_doing_gui,
    .game_is_busy_doing_gui_string_input = &render_overlay_game_is_busy_doing_gui_string_input,
    .frontend_load_data_from_cd = &frontend_load_data_from_cd,
    .frontend_load_data_reset = &frontend_load_data_reset,
    .update_room_tab_to_config = &update_room_tab_to_config,
    .update_powers_tab_to_config = &update_powers_tab_to_config,
    .update_creatr_model_activities_list = &update_creatr_model_activities_list,
    .update_panel_color_player_color = &update_panel_color_player_color,
    .setup_panel_colors = &setup_panel_colors,
    .reset_panel_map_background_cache = &reset_panel_map_background_cache,
    .local_view_transition = &local_view_transition,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
