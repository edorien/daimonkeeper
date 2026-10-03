/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_callbacks.c
 *     Callback-registration implementation. See game_callbacks.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "game_callbacks.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static int64_t noop_toggle_main_cheat_menu(void) { return 0; }
static int64_t noop_toggle_instance_cheat_menu(void) { return 0; }
static TbBool noop_toggle_secondary_cheat_menu(void) { return false; }
static TbBool noop_toggle_creature_cheat_menu(void) { return false; }
static TbBool noop_close_main_cheat_menu(void) { return false; }
static TbBool noop_close_instance_cheat_menu(void) { return false; }
static TbBool noop_close_secondary_cheat_menu(void) { return false; }
static TbBool noop_close_creature_cheat_menu(void) { return false; }
static void noop_create_error_box(TextStringId msg_idx) {}
static TbBool noop_is_fe_computer_players_active(void) { return false; }
static TbBool noop_is_fe_spectate_campaign_active(void) { return false; }
static void noop_set_gui_visible(TbBool visible) {}
static int64_t noop_is_menu_active(int64_t idx) { return 0; }

static void noop_set_timer_turns(uint64_t value) {}
static TbBool noop_is_timer_enabled(void) { return false; }
static void noop_toggle_debug_network_stats(void) {}
static TbBool noop_is_bonus_timer_enabled(void) { return false; }

static void noop_go_to_my_next_room_of_type(RoomKind rkind) {}
static int64_t noop_get_button_designation(int64_t btn_group, int64_t btn_item) { return -1; }
static void noop_gui_set_button_flashing(int64_t btn_idx, int64_t gameturns) {}

static struct GuiBox *noop_create_gui_box(int64_t x, int64_t y, struct GuiBoxOption *optn_list) { return NULL; }

static void noop_zero_all_messages(void) {}
static void noop_show_game_time_taken(uint64_t fps, uint64_t turns) {}

static TbBool noop_toggle_tooltip_land_coord(void) { return false; }

static void noop_get_high_score_entry(char *dest, size_t dest_size) { if (dest_size > 0) dest[0] = '\0'; }
static void noop_set_high_score_entry(const char *name) {}

static void noop_frontstats_initialise(void) {}

static void noop_setup_alliances(void) {}

static void noop_clear_all_messages(void) {}
static void noop_process_all_messages(void) {}
static void noop_script_play_message(TbBool param_is_string, char msgtype_id, int64_t msg_id, const char *filename) {}

static void noop_turn_on_ingame_menu(int64_t idx) {}
static void noop_turn_off_ingame_menu(int64_t mnu_idx) {}

static void noop_clear_top_message_stats(void) {}

static void noop_init_gui(void) {}

static void noop_set_level_objective(PlayerNumber plyr_idx, const char *msg_text) {}
static void noop_display_objectives(PlayerNumber plyr_idx, MapSubtlCoord x, MapSubtlCoord y) {}
static void noop_display_objectives_with_icon(PlayerNumber plyr_idx, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx) {}

static void noop_reset_gui_based_on_player_mode(void) {}

static void noop_update_panel_colors(void) {}
static TbBool noop_save_frontend_state(TbFileHandle fhandle) { return false; }
static TbBool noop_load_frontend_state(TbFileHandle fhandle) { return false; }
static void noop_reset_frontend_state(void) {}
static size_t noop_get_frontend_state_size(void) { return 0; }
static int64_t noop_get_intralvl_next_level(void) { return 0; }
static void noop_clear_intralvl_next_level(void) {}
static void noop_set_frontend_alliances(char alliances) {}

static const struct GameCallbacks default_game_callbacks = {
    &noop_toggle_main_cheat_menu,
    &noop_toggle_instance_cheat_menu,
    &noop_toggle_secondary_cheat_menu,
    &noop_toggle_creature_cheat_menu,
    &noop_close_main_cheat_menu,
    &noop_close_instance_cheat_menu,
    &noop_close_secondary_cheat_menu,
    &noop_close_creature_cheat_menu,
    &noop_create_error_box,
    &noop_is_fe_computer_players_active,
    &noop_is_fe_spectate_campaign_active,
    &noop_set_gui_visible,
    &noop_is_menu_active,

    &noop_set_timer_turns,
    &noop_is_timer_enabled,
    &noop_toggle_debug_network_stats,
    &noop_is_bonus_timer_enabled,

    &noop_go_to_my_next_room_of_type,
    &noop_get_button_designation,
    &noop_gui_set_button_flashing,

    &noop_create_gui_box,

    &noop_zero_all_messages,
    &noop_show_game_time_taken,

    &noop_toggle_tooltip_land_coord,

    &noop_get_high_score_entry,
    &noop_set_high_score_entry,

    &noop_frontstats_initialise,

    &noop_setup_alliances,

    &noop_clear_all_messages,
    &noop_process_all_messages,
    &noop_script_play_message,

    &noop_turn_on_ingame_menu,
    &noop_turn_off_ingame_menu,

    &noop_clear_top_message_stats,

    &noop_init_gui,

    &noop_set_level_objective, &noop_display_objectives, &noop_display_objectives_with_icon,

    &noop_reset_gui_based_on_player_mode,

    &noop_update_panel_colors,
    &noop_save_frontend_state,
    &noop_load_frontend_state,
    &noop_reset_frontend_state,
    &noop_get_frontend_state_size,
    &noop_get_intralvl_next_level,
    &noop_clear_intralvl_next_level,
    &noop_set_frontend_alliances,
};
const struct GameCallbacks *game_callbacks = &default_game_callbacks;

void set_game_callbacks(const struct GameCallbacks *callbacks)
{
    game_callbacks = callbacks ? callbacks : &default_game_callbacks;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
