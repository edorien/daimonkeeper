/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_callbacks.h
 *     Header file for net_callbacks.c.
 * @par Purpose:
 *     Callback-registration interface letting kfx_net call into
 *     kfx_frontend/kfx_game/kfx_script functionality (frontend menu
 *     state, join/session UI feedback, cheat-menu bookkeeping, chat
 *     command execution, Lua resync payloads) without depending on
 *     frontend.h/front_network.h/gui_frontmenu.h/kjm_input.h/
 *     front_input.h/front_simple.h/console_cmd.h/lua_triggers.h/
 *     lua_base.h directly (those are kfx_frontend/kfx_game/kfx_script
 *     layer, above kfx_net). See docs/refactor/stage-08-kfx-net.md.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_NET_CALLBACKS_H
#define DK_NET_CALLBACKS_H

#include "bflib_basics.h"
#include "bflib_keybrd.h"
#include "bflib_sound.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct PlayerInfo;
struct CatalogueEntry;

struct NetCallbacks {
    /* frontend.h */
    void (*enter_net_session_screen)(void);
    void (*set_lobby_button_labels)(TbBool is_lan);
    void (*create_frontend_error_box)(long show_time_ms, const char *text);
    short (*frontend_save_continue_game)(short allow_lvnum_grow);
    unsigned long (*toggle_status_menu)(short visible);
    void (*set_gui_visible)(TbBool visible);
    unsigned char (*get_default_tag_mode)(void);
    TbBool (*is_frontend_starting_mp_level)(void);
    TbBool (*is_frontend_at_initial_state)(void);

    /* front_network.h -- service is a FrontendNetService value (net_main.h);
       passed as int since kfx_config sits below kfx_net. */
    void (*display_attempting_to_join_message)(int remaining_s);
    TbBool (*attempting_to_join_cancel_requested)(void);
    void (*reset_attempting_to_join_cancel)(void);
    void (*process_network_error)(long errcode);
    TbBool (*frontnet_service_selected)(int service);

    /* gui_frontmenu.h */
    void (*turn_off_all_menus)(void);
    void (*turn_off_query_menus)(void);
    void (*turn_on_main_panel_menu)(void);
    void (*turn_off_all_panel_menus)(void);
    void (*turn_on_menu)(MenuID idx);

    /* frontmenu_ingame_map.h */
    void (*panel_map_update)(long x, long y, long w, long h);

    /* frontmenu_ingame_tabs.h */
    void (*update_trap_tab_to_config)(void);
    void (*instant_instance_selected)(CrInstance check_inst_id);

    /* kjm_input.h */
    short (*is_key_pressed)(TbKeyCode key, TbKeyMods kmodif);
    void (*clear_key_pressed)(long key);

    /* front_input.h */
    TbBool (*process_cheat_heart_health_inputs)(HitPoints *value, HitPoints max_health);

    /* front_simple.h -- clears UsrAF_LightningPaletteIsActive and restores
       the player's normal palette in one step. */
    void (*clear_player_lightning_palette)(struct PlayerInfo *player);

    /* console_cmd.h */
    TbBool (*cmd_exec)(PlayerNumber plyr_idx, char *msg);

    /* lua_triggers.h */
    void (*lua_on_chatmsg)(PlayerNumber plyr_idx, char *msg);

    /* lua_base.h -- full-resync payload (net_resync.cpp). Narrowed
       toward the export()/import() pair stage-11's doc calls for (item
       4): lua_resync_export()/import() absorb the former active-check
       plus get/set_serialised_data(). lua_script_active()/
       lua_set_random_seed() stay separate rather than folding in too --
       net_resync.cpp imports the wire data BEFORE the game-state memcpy
       (so a failure leaves local state untouched) but resets the random
       seed AFTER it (it needs the freshly-synced value); collapsing all
       three into one call would force one side of that ordering to be
       wrong. lua_cleanup_serialized_data() also stays separate since
       it's a buffer-lifetime concern net_resync.cpp still owns (frees
       the export buffer after sending). */
    TbBool (*lua_script_active)(void);
    const char *(*lua_resync_export)(size_t *len);
    TbBool (*lua_resync_import)(const char *data, size_t len);
    void (*lua_set_random_seed)(unsigned int seed);
    void (*lua_cleanup_serialized_data)(void);

    /* game_legacy.h/kfx_game_state.h/kfx_frontend_state.h -- upper-state
       resync payload (net_resync.cpp), same export/import shape as the
       Lua pair above and for the same reason: kfx_net can't #include
       kfx_game's/kfx_frontend's headers to memcpy `game`/`kfx_game_state`/
       `kfx_frontend_state` wholesale as part of the raw-blob resync wire
       format (architecture.md §6.2) -- see
       docs/refactor/todo/remove-remaining-layering-violations.md. Unlike
       the Lua pair, these return a pointer to a fixed-size static buffer,
       not a freshly allocated one -- there's no cleanup callback because
       there's nothing to free. */
    const char *(*resync_export_game_state)(size_t *len);
    TbBool (*resync_import_game_state)(const char *data, size_t len);
    const char *(*resync_export_frontend_state)(size_t *len);
    TbBool (*resync_import_frontend_state)(const char *data, size_t len);

    /* game_session_loop.h (kfx_apploop, stage 12.5) -- yield points hit
       while kfx_net is blocked waiting on network I/O, letting the
       gameplay/frontend loop still draw/poll input/update timing during
       the wait. Were bare `extern void foo();` declarations straight
       into src/main.cpp before kfx_apploop existed; that was a real
       kfx_net -> app_entry violation invisible to check_layering.py
       (which only scans #include edges, not extern declarations). */
    void (*network_yield_poll_gameplay)(void); /* input poll only; sets exit_keeper if the OS asked to quit */
    void (*network_yield_waiting_gameplay_packets)(void);
    void (*network_yield_draw_frontend)(void);

    /* gui_soundmsgs.h */
    TbBool (*output_message)(SoundSmplTblID smpl_idx, long duration);

    /* gui_topmsg.h */
    long (*report_error_stat)(int stat_num);
    TbBool (*show_onscreen_msg)(int nturns, const char *msg);
    TbBool (*is_onscreen_msg_visible)(void);

    /* main_game.h */
    short (*winning_player_quitting)(struct PlayerInfo *player, int32_t *plyr_count);
    void (*reinit_level_after_load)(void);
    short (*complete_level)(struct PlayerInfo *player);
    short (*lose_level)(struct PlayerInfo *player);
    short (*resign_level)(struct PlayerInfo *player);

    /* game_saves.h */
    int (*load_game_chunks)(TbFileHandle fhandle, struct CatalogueEntry *centry);
    TbBool (*fill_game_catalogue_entry)(struct CatalogueEntry *centry, const char *textname);
    TbBool (*save_packet_chunks)(TbFileHandle fhandle, struct CatalogueEntry *centry);

    /* front_network.h -- reached transitively via net_game.h before
       net_game.h's own dead front_network.h include was removed
       (stage 13.3); both are genuine kfx_net -> kfx_frontend calls. */
    void (*draw_out_of_sync_box)(long a1, long a2, long box_width);
    void (*process_frontend_chat_message)(int player_id, const char *message);

    /* game_session_loop.h (kfx_apploop) -- net_exchange_common.c updates
       the host-packet-received timestamp kfx_apploop's multiplayer
       clock-adjust logic reads each frame; same same-file-bare-extern
       violation shape as network_yield_poll_gameplay above, found by
       scripts/check_layering_symbols.py (docs/refactor/todo/
       check-layering-symbol-level-blind-spot.md) rather than by
       inspection like the rest of this struct. */
    void (*set_host_packet_received)(long double value);

    /* frontend.h -- frontend_alliances (the lobby's alliance selection). Recorded into
       -packetsave replay headers by kfx_net's packets_misc.c (#5317); kfx_frontend owns
       the variable, kfx_net is below it. */
    char (*get_frontend_alliances)(void);
};
void set_net_callbacks(const struct NetCallbacks *callbacks);
extern const struct NetCallbacks *net_callbacks;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
