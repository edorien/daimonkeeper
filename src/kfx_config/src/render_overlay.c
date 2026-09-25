/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file render_overlay.c
 *     Callback-registration implementation. See render_overlay.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "render_overlay.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static void noop_void(void) {}
static void noop_set_int(int64_t val) {}
static void noop_gui_panel_sprite_left(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx) {}
static void noop_draw_slab64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height) {}
static void noop_sync_cheat_box_3(CrInstance active_instance_id) {}
static TbBool noop_get_bool(void) { return false; }
static double noop_get_interpolate_time(void) { return 0.0; }
static int64_t noop_get_long(void) { return 0; }
static int64_t noop_menu_is_active(int64_t idx) { return 0; }
static void noop_turn_on_menu(MenuID idx) {}
static void noop_sync_render_globals(void) {}
static TbBool noop_setup_heap_manager(void) { return false; }
static void noop_reset_heap_manager(void) {}
static void *noop_he_alloc(size_t size) { return NULL; }
static void noop_reload_parchment_file(TbBool hires) {}
static TbBool noop_point_to_overhead_map(const struct Camera *camera, int64_t screen_x, int64_t screen_y, int64_t *map_x, int64_t *map_y) { return false; }
static TbBool noop_can_process_creature_input(struct Thing *thing) { return false; }
static void noop_process_first_person_look(struct Thing *thing, const struct Packet *pckt, int64_t current_horizontal, int64_t current_vertical, int64_t *out_horizontal, int64_t *out_vertical, int64_t *out_roll) {}
static void noop_process_camera_controls(struct Camera *cam, const struct Packet *pckt, struct PlayerInfo *player, TbBool is_local_camera) {}
static void noop_process_camera_action(struct Camera *cams, const struct Packet *pckt) {}
static const struct Packet *noop_get_history_packet(NetUserId user, GameTurn turn) { return NULL; }
static void noop_set_packet_control(struct Packet *pckt, uint64_t flag) {}
static int64_t noop_light_create_light(struct InitLight *ilght) { return 0; }
static void noop_light_set_attached_slab(int64_t lgt_id, SlabCodedCoords slb_num) {}
static void noop_delete_lights_attached_to_slab_in_area(SlabCodedCoords place_slbnum,
    MapSubtlCoord start_stl_x, MapSubtlCoord start_stl_y,
    MapSubtlCoord end_stl_x, MapSubtlCoord end_stl_y) {}

static const struct RenderOverlayCallbacks default_render_overlay = {
    &noop_void,                   /* redraw_parchment_view */
    &noop_void,                   /* load_and_redraw_minimal_overhead_view */
    &noop_set_int,                /* set_parchment_loaded */
    &noop_get_bool,                /* is_parchment_loaded */
    &noop_reload_parchment_file,   /* reload_parchment_file */
    &noop_point_to_overhead_map,   /* point_to_overhead_map */
    &noop_get_long,               /* get_main_menu_width */
    &noop_gui_panel_sprite_left,  /* draw_gui_panel_sprite_left */
    &noop_draw_slab64k,           /* draw_slab64k */
    &noop_gui_panel_sprite_left,  /* draw_gui_panel_sprite_centered */
    &noop_gui_panel_sprite_left,  /* draw_button_sprite_left */
    &noop_void,                   /* message_draw */
    &noop_void,                   /* gui_draw_all_boxes */
    &noop_void,                   /* draw_tooltip */
    &noop_sync_cheat_box_3,       /* sync_cheat_box_3_active_option */
    &noop_get_bool,               /* cheat_or_menu_window_active */
    &noop_void,                   /* draw_eastegg */
    &noop_void,                   /* set_winfont */
    &noop_get_long,               /* get_status_panel_width */
    &noop_void,                   /* draw_gui */
    &noop_get_bool,               /* game_is_busy_doing_gui */
    &noop_get_bool,               /* game_is_busy_doing_gui_string_input */
    &noop_void,                   /* draw_whole_status_panel */
    &noop_void,                   /* draw_debug_overlays */
    &noop_get_bool,               /* bonus_script_or_variable_overlay_active */
    &noop_get_long,               /* get_battle_creature_over */
    &noop_get_long,               /* get_map_diagonal_length */
    &noop_get_bool,               /* get_unpausing_in_progress */
    &noop_can_process_creature_input,
    &noop_process_first_person_look,
    &noop_process_camera_controls,
    &noop_process_camera_action,
    &noop_get_history_packet,
    &noop_set_packet_control,

    &noop_void,                   /* frontend_load_data_from_cd */
    &noop_void,                   /* frontend_load_data_reset */
    &noop_menu_is_active,         /* menu_is_active */
    &noop_void,                   /* reinit_all_menus */
    &noop_turn_on_menu,           /* turn_on_menu */
    &noop_sync_render_globals,    /* sync_render_globals */
    &noop_setup_heap_manager,     /* setup_heap_manager */
    &noop_reset_heap_manager,     /* reset_heap_manager */
    &noop_he_alloc,                /* he_alloc */
    &noop_light_create_light,      /* light_create_light */
    &noop_light_set_attached_slab, /* light_set_attached_slab */
    &noop_delete_lights_attached_to_slab_in_area, /* delete_lights_attached_to_slab_in_area */
    &noop_get_bool,                /* get_lights_enabled */
    &noop_get_interpolate_time,
};
const struct RenderOverlayCallbacks *render_overlay = &default_render_overlay;

void set_render_overlay_callbacks(const struct RenderOverlayCallbacks *callbacks)
{
    render_overlay = callbacks ? callbacks : &default_render_overlay;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
