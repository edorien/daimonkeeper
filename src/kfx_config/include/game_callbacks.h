/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_callbacks.h
 *     Header file for game_callbacks.c.
 * @par Purpose:
 *     Callback-registration interface letting kfx_game call into
 *     kfx_frontend functionality (cheat-menu toggles, debug overlays,
 *     GUI boxes, high-score entry, menu visibility) without depending on
 *     frontend.h/frontmenu_ingame_evnt.h/frontmenu_ingame_tabs.h/
 *     gui_boxmenu.h/gui_msgs.h/gui_tooltips.h/front_highscore.h/
 *     front_lvlstats.h/front_network.h/gui_soundmsgs.h/gui_frontmenu.h
 *     directly (those are kfx_frontend layer, above kfx_game). See
 *     docs/refactor/stage-09-kfx-game.md.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_GAME_CALLBACKS_H
#define DK_GAME_CALLBACKS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct GuiBox;
struct GuiBoxOption;

struct GameCallbacks {
    /* frontend.h -- cheat menu group */
    int64_t (*toggle_main_cheat_menu)(void);
    int64_t (*toggle_instance_cheat_menu)(void);
    TbBool (*toggle_secondary_cheat_menu)(void);
    TbBool (*toggle_creature_cheat_menu)(void);
    TbBool (*close_main_cheat_menu)(void);
    TbBool (*close_instance_cheat_menu)(void);
    TbBool (*close_secondary_cheat_menu)(void);
    TbBool (*close_creature_cheat_menu)(void);
    void (*create_error_box)(TextStringId msg_idx);
    TbBool (*is_fe_computer_players_active)(void);
    void (*set_gui_visible)(TbBool visible);
    int64_t (*is_menu_active)(int64_t idx);

    /* frontmenu_ingame_evnt.h -- debug/timer overlay state */
    void (*set_timer_turns)(uint64_t value);
    TbBool (*is_timer_enabled)(void);
    void (*toggle_debug_network_stats)(void);
    TbBool (*is_bonus_timer_enabled)(void);

    /* frontmenu_ingame_tabs.h */
    void (*go_to_my_next_room_of_type)(RoomKind rkind);
    int64_t (*get_button_designation)(int64_t btn_group, int64_t btn_item);
    void (*gui_set_button_flashing)(int64_t btn_idx, int64_t gameturns);

    /* gui_boxmenu.h */
    struct GuiBox *(*create_gui_box)(int64_t x, int64_t y, struct GuiBoxOption *optn_list);

    /* gui_msgs.h */
    void (*zero_all_messages)(void);
    void (*show_game_time_taken)(uint64_t fps, uint64_t turns);

    /* gui_tooltips.h */
    TbBool (*toggle_tooltip_land_coord)(void);

    /* front_highscore.h */
    void (*get_high_score_entry)(char *dest, size_t dest_size);
    void (*set_high_score_entry)(const char *name);

    /* front_lvlstats.h */
    void (*frontstats_initialise)(void);

    /* front_network.h */
    void (*setup_alliances)(void);

    /* gui_soundmsgs.h -- beyond sim_feedback's message/sound callbacks */
    void (*clear_all_messages)(void);
    void (*process_all_messages)(void);
    void (*script_play_message)(TbBool param_is_string, char msgtype_id, int64_t msg_id, const char *filename);

    /* gui_frontmenu.h -- MenuID is `long`, kept as the raw underlying
       type here since the typedef itself lives in kfx_frontend. */
    void (*turn_on_ingame_menu)(int64_t idx);
    void (*turn_off_ingame_menu)(int64_t mnu_idx);

    /* gui_topmsg.h */
    void (*clear_top_message_stats)(void); /* wired to erstats_clear() */

    /* frontend.h -- GUI (re)initialisation */
    void (*init_gui)(void);

    /* frontend.h -- objective messages */
    void (*set_level_objective)(PlayerNumber plyr_idx, const char *msg_text);
    void (*display_objectives)(PlayerNumber plyr_idx, MapSubtlCoord x, MapSubtlCoord y);
    void (*display_objectives_with_icon)(PlayerNumber plyr_idx, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx);

    /* gui_frontmenu.h */
    void (*reset_gui_based_on_player_mode)(void);

    /* frontmenu_ingame_map.h -- update_panel_color_player_color already
       covered by ConfigReloadCallbacks (config.h). */
    void (*update_panel_colors)(void);

    /* kfx_frontend_state.h -- game_saves.c/main_game.c need to
       save/load/reset kfx_frontend's entire state struct as a single
       raw blob (save-game format, level reset); kfx_frontend
       implements the read/write itself since it owns the type. */
    TbBool (*save_frontend_state)(TbFileHandle fhandle);
    TbBool (*load_frontend_state)(TbFileHandle fhandle);
    void (*reset_frontend_state)(void);
    size_t (*get_frontend_state_size)(void);

    /* game_merge.h -- config.c's next_singleplayer_level() reads and
       conditionally resets intralvl.next_level (a single field of
       kfx_game-owned struct IntralevelData). Kept as separate get/clear
       entries (not one atomic "take") since the reset is conditional on
       logic evaluated between the read and the reset. */
    int64_t (*get_intralvl_next_level)(void);
    void (*clear_intralvl_next_level)(void);

    /* frontend.h -- frontend_alliances: a replay (-packetload, #5317) restores the
       alliance selection it was recorded with before setup_alliances() runs. */
    void (*set_frontend_alliances)(char alliances);
};
void set_game_callbacks(const struct GameCallbacks *callbacks);
extern const struct GameCallbacks *game_callbacks;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
