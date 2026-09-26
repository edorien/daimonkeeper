/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file render_overlay.h
 *     Header file for render_overlay.c.
 * @par Purpose:
 *     Callback-registration interface letting engine_redraw.c (future
 *     kfx_render) draw GUI/frontend/debug overlays without including
 *     gui_*.h/frontmenu_*.h/frontend.h/packets.h directly (those are
 *     kfx_frontend/kfx_net layer, above kfx_render). See
 *     docs/refactor/stage-07-kfx-render.md.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_RENDER_OVERLAY_H
#define DK_RENDER_OVERLAY_H

#include "bflib_basics.h"
#include "bflib_netsp.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Camera;
struct Thing;
struct Packet;
struct PlayerInfo;
struct InitLight;

struct RenderOverlayCallbacks {
    /* gui_parchment.h */
    void (*redraw_parchment_view)(void);
    void (*load_and_redraw_minimal_overhead_view)(void); /* load_parchment_file() + redraw_minimal_overhead_view() */
    void (*set_parchment_loaded)(int64_t val);
    TbBool (*is_parchment_loaded)(void);
    void (*reload_parchment_file)(TbBool hires);
    TbBool (*point_to_overhead_map)(const struct Camera *camera, int64_t screen_x, int64_t screen_y, int64_t *map_x, int64_t *map_y);

    /* gui_frontmenu.h */
    int64_t (*get_main_menu_width)(void);

    /* gui_draw.h */
    void (*draw_gui_panel_sprite_left)(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx);
    void (*draw_slab64k)(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height);
    void (*draw_gui_panel_sprite_centered)(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx);
    void (*draw_button_sprite_left)(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx);

    /* gui_msgs.h / gui_boxmenu.h / gui_tooltips.h */
    void (*message_draw)(void);
    void (*gui_draw_all_boxes)(void);
    void (*draw_tooltip)(void);
    void (*sync_cheat_box_3_active_option)(CrInstance active_instance_id); /* gui_cheat_box_3 lookup/iterate */
    TbBool (*cheat_or_menu_window_active)(void); /* cheat_menu_is_active() || a_menu_window_is_active() */

    /* front_easter.h */
    void (*draw_eastegg)(void);

    /* frontend.h */
    void (*set_winfont)(void); /* LbTextSetFont(winfont) */
    int64_t (*get_status_panel_width)(void);
    void (*draw_gui)(void);
    TbBool (*game_is_busy_doing_gui)(void);

    /* front_input.h */
    TbBool (*game_is_busy_doing_gui_string_input)(void);

    /* frontmenu_ingame_tabs.h */
    void (*draw_whole_status_panel)(void);

    /* frontmenu_ingame_evnt.h */
    void (*draw_debug_overlays)(void); /* bonus/script/gameturn timers, frametime, network stats, consolelog */
    TbBool (*bonus_script_or_variable_overlay_active)(void); /* bonus_timer_enabled()||script_timer_enabled()||display_variable_enabled() */
    int64_t (*get_battle_creature_over)(void);

    /* frontmenu_ingame_map.h */
    int64_t (*get_map_diagonal_length)(void);

    /* packets.h */
    TbBool (*get_unpausing_in_progress)(void);
    TbBool (*can_process_creature_input)(struct Thing *thing);
    void (*process_first_person_look)(struct Thing *thing, const struct Packet *pckt, int64_t current_horizontal, int64_t current_vertical, int64_t *out_horizontal, int64_t *out_vertical, int64_t *out_roll);
    void (*process_camera_controls)(struct Camera *cam, const struct Packet *pckt, struct PlayerInfo *player);
    void (*process_camera_view_controls)(struct Camera *cam, const struct Packet *pckt, struct PlayerInfo *player);
    void (*process_camera_action)(struct Camera *cams, const struct Packet *pckt);
    const struct Packet *(*get_history_packet)(NetUserId user, GameTurn turn);
    void (*set_packet_control)(struct Packet *pckt, uint64_t flag);

    /* frontend.h -- called from kfx_render's vidmode.c (video-mode
       switch needs to reload frontend data / recheck which menus are
       open) and kfx_sim's thing_creature.c (menu_is_active only). */
    void (*frontend_load_data_from_cd)(void);
    void (*frontend_load_data_reset)(void);
    int64_t (*menu_is_active)(int64_t idx);
    void (*reinit_all_menus)(void);

    /* gui_frontmenu.h -- vidmode.c re-opens the video-options menu after
       a mode switch. */
    void (*turn_on_menu)(MenuID idx);

    /* vidmode.h -- power_hand.c needs bflib's render_fade_tables/
       render_ghost/render_alpha pointed at kfx_render's pixmap/
       alpha_sprite_table before drawing a hand-held sprite. */
    void (*sync_render_globals)(void);

    /* game_heap.h -- kfx_game owns the heap manager (game_heap.c);
       vidmode.c/engine_render.c call up into it around video-mode
       switches and texture allocation. */
    TbBool (*setup_heap_manager)(void);
    void (*reset_heap_manager)(void);
    void *(*he_alloc)(size_t size);

    /* light_data.h -- kfx_sim's map_blocks.c/thing_list.c need a single
       light's attached_slb, the static-lights-on-a-slab sweep, and the
       light_enabled flag, without struct Light/struct LightsShadows
       visible by value (stage 13.3, docs/refactor/
       stage-13-enforce-and-document.md). */
    int64_t (*light_create_light)(struct InitLight *ilght);
    void (*light_set_attached_slab)(int64_t lgt_id, SlabCodedCoords slb_num);
    void (*delete_lights_attached_to_slab_in_area)(SlabCodedCoords place_slbnum,
        MapSubtlCoord start_stl_x, MapSubtlCoord start_stl_y,
        MapSubtlCoord end_stl_x, MapSubtlCoord end_stl_y);
    TbBool (*get_lights_enabled)(void);

    /* game_session_loop.h (kfx_apploop) -- engine_render.c's
       interpolate()/interpolate_angle() need the current frame's
       interpolation fraction, computed once per frame by kfx_apploop's
       main loop. Found via scripts/check_layering_symbols.py
       (docs/refactor/todo/check-layering-symbol-level-blind-spot.md). */
    double (*get_interpolate_time)(void);
};
void set_render_overlay_callbacks(const struct RenderOverlayCallbacks *callbacks);
extern const struct RenderOverlayCallbacks *render_overlay;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
