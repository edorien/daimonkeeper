/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_callbacks.c
 *     Callback-registration implementation. See net_callbacks.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "net_callbacks.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static void noop_enter_net_session_screen(void) {}
static void noop_set_lobby_button_labels(TbBool is_lan) {}
static void noop_create_frontend_error_box(long show_time_ms, const char *text) {}
static short noop_frontend_save_continue_game(short allow_lvnum_grow) { return 0; }
static unsigned long noop_toggle_status_menu(short visible) { return 0; }
static void noop_set_gui_visible(TbBool visible) {}
static unsigned char noop_get_default_tag_mode(void) { return 0; }
static TbBool noop_is_frontend_starting_mp_level(void) { return false; }
static TbBool noop_is_frontend_at_initial_state(void) { return false; }
static char noop_get_frontend_alliances(void) { return 0; }

static void noop_display_attempting_to_join_message(int remaining_s) {}
static TbBool noop_attempting_to_join_cancel_requested(void) { return false; }
static void noop_reset_attempting_to_join_cancel(void) {}
static void noop_process_network_error(long errcode) {}
static TbBool noop_frontnet_service_selected(int service) { return false; }

static void noop_turn_off_all_menus(void) {}
static void noop_turn_off_query_menus(void) {}
static void noop_turn_on_main_panel_menu(void) {}
static void noop_turn_off_all_panel_menus(void) {}
static void noop_turn_on_menu(MenuID idx) {}

static void noop_panel_map_update(long x, long y, long w, long h) {}

static void noop_update_trap_tab_to_config(void) {}
static void noop_instant_instance_selected(CrInstance check_inst_id) {}

static short noop_is_key_pressed(TbKeyCode key, TbKeyMods kmodif) { return 0; }
static void noop_clear_key_pressed(long key) {}

static TbBool noop_process_cheat_heart_health_inputs(HitPoints *value, HitPoints max_health) { return false; }

static void noop_clear_player_lightning_palette(struct PlayerInfo *player) {}

static TbBool noop_cmd_exec(PlayerNumber plyr_idx, char *msg) { return false; }

static void noop_lua_on_chatmsg(PlayerNumber plyr_idx, char *msg) {}

static TbBool noop_lua_script_active(void) { return false; }
static const char *noop_lua_resync_export(size_t *len) { *len = 0; return ""; }
static TbBool noop_lua_resync_import(const char *data, size_t len) { return true; }
static void noop_lua_set_random_seed(unsigned int seed) {}
static void noop_lua_cleanup_serialized_data(void) {}

static const char *noop_resync_export_game_state(size_t *len) { *len = 0; return ""; }
static TbBool noop_resync_import_game_state(const char *data, size_t len) { return true; }
static const char *noop_resync_export_frontend_state(size_t *len) { *len = 0; return ""; }
static TbBool noop_resync_import_frontend_state(const char *data, size_t len) { return true; }

static void noop_network_yield_poll_gameplay(void) {}
static void noop_network_yield_waiting_gameplay_packets(void) {}
static void noop_network_yield_draw_frontend(void) {}
static TbBool noop_output_message(SoundSmplTblID smpl_idx, long duration) { return false; }
static long noop_report_error_stat(int stat_num) { return 0; }
static TbBool noop_show_onscreen_msg(int nturns, const char *msg) { return false; }
static TbBool noop_is_onscreen_msg_visible(void) { return false; }
static short noop_winning_player_quitting(struct PlayerInfo *player, int32_t *plyr_count) { return 0; }
static void noop_reinit_level_after_load(void) {}
static short noop_complete_level(struct PlayerInfo *player) { return 0; }
static short noop_lose_level(struct PlayerInfo *player) { return 0; }
static short noop_resign_level(struct PlayerInfo *player) { return 0; }
static int noop_load_game_chunks(TbFileHandle fhandle, struct CatalogueEntry *centry) { return 0; }
static TbBool noop_fill_game_catalogue_entry(struct CatalogueEntry *centry, const char *textname) { return false; }
static TbBool noop_save_packet_chunks(TbFileHandle fhandle, struct CatalogueEntry *centry) { return false; }
static void noop_draw_out_of_sync_box(long a1, long a2, long box_width) {}
static void noop_process_frontend_chat_message(int player_id, const char *message) {}
static void noop_set_host_packet_received(long double value) {}

static const struct NetCallbacks default_net_callbacks = {
    &noop_enter_net_session_screen,
    &noop_set_lobby_button_labels,
    &noop_create_frontend_error_box,
    &noop_frontend_save_continue_game,
    &noop_toggle_status_menu,
    &noop_set_gui_visible,
    &noop_get_default_tag_mode,
    &noop_is_frontend_starting_mp_level,
    &noop_is_frontend_at_initial_state,

    &noop_display_attempting_to_join_message,
    &noop_attempting_to_join_cancel_requested,
    &noop_reset_attempting_to_join_cancel,
    &noop_process_network_error,
    &noop_frontnet_service_selected,

    &noop_turn_off_all_menus,
    &noop_turn_off_query_menus,
    &noop_turn_on_main_panel_menu,
    &noop_turn_off_all_panel_menus,
    &noop_turn_on_menu,

    &noop_panel_map_update,

    &noop_update_trap_tab_to_config,
    &noop_instant_instance_selected,

    &noop_is_key_pressed,
    &noop_clear_key_pressed,

    &noop_process_cheat_heart_health_inputs,

    &noop_clear_player_lightning_palette,

    &noop_cmd_exec,

    &noop_lua_on_chatmsg,

    &noop_lua_script_active,
    &noop_lua_resync_export,
    &noop_lua_resync_import,
    &noop_lua_set_random_seed,
    &noop_lua_cleanup_serialized_data,

    &noop_resync_export_game_state,
    &noop_resync_import_game_state,
    &noop_resync_export_frontend_state,
    &noop_resync_import_frontend_state,

    &noop_network_yield_poll_gameplay,
    &noop_network_yield_waiting_gameplay_packets,
    &noop_network_yield_draw_frontend,
    &noop_output_message,
    &noop_report_error_stat,
    &noop_show_onscreen_msg,
    &noop_is_onscreen_msg_visible,
    &noop_winning_player_quitting,
    &noop_reinit_level_after_load,
    &noop_complete_level,
    &noop_lose_level,
    &noop_resign_level,
    &noop_load_game_chunks,
    &noop_fill_game_catalogue_entry,
    &noop_save_packet_chunks,
    &noop_draw_out_of_sync_box,
    &noop_process_frontend_chat_message,
    &noop_set_host_packet_received,
    &noop_get_frontend_alliances,
};
const struct NetCallbacks *net_callbacks = &default_net_callbacks;

void set_net_callbacks(const struct NetCallbacks *callbacks)
{
    net_callbacks = callbacks ? callbacks : &default_net_callbacks;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
