/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sim_feedback.h
 *     Header file for sim_feedback.c.
 * @par Purpose:
 *     Callback-registration interface letting kfx_sim report diagnostic
 *     error stats, on-screen warnings, and trigger sound effects/speech
 *     messages, without depending on gui_topmsg.h/gui_soundmsgs.h/sounds.h
 *     directly (those are kfx_frontend/kfx_game layer, above kfx_sim). Also
 *     covers the room-space key-binding queries and highlighted-room/
 *     visible-event-index writes that `roomspace.c`/`map_events.c` need
 *     from front_input.h/gui_draw.h/gui_frontmenu.h/frontmenu_ingame_evnt.h,
 *     and the battle-creature/volume-box/lag-compensation/cursor-tagging
 *     queries `roomspace_prediction.c` needs from
 *     frontmenu_ingame_evnt.h/engine_render.h/cursor_tag.h.
 *     See docs/refactor/stage-06-kfx-sim.md,
 *     docs/refactor/stage-06b-sim-frontend-leaks.md, and
 *     docs/refactor/stage-06c-roomspace-prediction-tangle.md.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_SIM_FEEDBACK_H
#define DK_SIM_FEEDBACK_H

#include "bflib_basics.h"
#include "compiler_compat.h"
#include "game_time.h" // struct GameTime
#include "bflib_sound.h"
#include "bflib_keybrd.h"
#include "bflib_netsp.h"
#include "bflib_video.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;
struct Coord3d;
struct SpeechRef;
struct InitLight;
struct PlayerInfo;
struct Packet;
struct RoomSpace;
struct Camera;
struct EventTypeInfo;
typedef struct VALUE VALUE;

// Physically split out of kfx_frontend's front_input.h (camera_data.h/
// packet_data.h/speech_ref.h precedent): a plain data struct returned by
// value from get_game_time(), needed complete at both the callback
// signature here and player_utils.c's call site. See
// docs/refactor/stage-13-enforce-and-document.md.
struct SimFeedbackCallbacks {
    /* gui_topmsg.h -- see enum ErrorStatisticEntries in globals.h */
    int64_t (*report_error_stat)(int64_t stat_num);
    TbBool (*show_onscreen_msg)(int64_t nturns, const char *msg);

    /* gui_soundmsgs.h */
    TbBool (*play_sound_message)(SoundSmplTblID smpl_idx, int64_t duration);
    TbBool (*output_room_message)(PlayerNumber plyr_idx, RoomKind rkind, OutputMessageKind msg_kind);
    TbBool (*play_sound_message_far_from_thing)(const struct Thing *thing, SoundSmplTblID smpl_idx, int64_t duration);
    TbBool (*play_speech_ref)(const struct SpeechRef *ref, int64_t duration);
    void (*clear_sound_messages)(void);
    void (*process_sound_messages)(void);

    /* gui_msgs.h -- see enum MessageTypes in globals.h */
    void (*clear_messages_from_player)(char msg_type, PlayerNumber plyr_idx);
    void (*targeted_message_add)(char msg_type, PlayerNumber plyr_idx, PlayerNumber target_idx, uint64_t timeout, const char *msg);
    void (*message_add)(char msg_type, int64_t idx, const char *msg);
    void (*message_add_fmt)(char msg_type, int64_t idx, const char *fmt_str, ...) KFX_PRINTF_FORMAT(3, 4);
    void (*zero_messages)(void);
    void (*show_real_time_taken)(void);

    /* sounds.h */
    void (*thing_play_sample)(struct Thing *thing, SoundSmplTblID smpl_idx, SoundPitch pitch, char repeats, unsigned char ctype, unsigned char flags, int64_t priority, SoundVolume volume);
    void (*stop_thing_playing_sample)(struct Thing *thing, SoundSmplTblID smpl_idx);
    struct Thing *(*create_ambient_sound)(const struct Coord3d *pos, ThingModel model, PlayerNumber owner);
    void (*play_sound_if_close_to_receiver)(struct Coord3d *soundpos, SoundSmplTblID smpl_idx);
    void (*play_thing_walking)(struct Thing *thing);

    /* front_input.h -- roomspace.c's key-binding queries, each equivalent
       to is_game_key_pressed(Gkey_*, clear_pressed=false, ignore_mods=true) */
    TbBool (*is_best_roomspace_key_pressed)(void);
    TbBool (*is_square_roomspace_key_pressed)(void);
    TbBool (*is_roomspace_incsize_key_pressed)(void);
    TbBool (*is_roomspace_decsize_key_pressed)(void);
    TbBool (*is_sell_trap_on_subtile_key_pressed)(void);

    /* gui_draw.h */
    void (*set_room_type_highlighted)(char room_kind);

    /* gui_frontmenu.h / frontmenu_ingame_evnt.h */
    void (*set_visible_event_idx)(EventIndex evidx);
    void (*clear_all_event_button_states)(void);
    void (*clear_event_button_state)(EventIndex evidx);
    void (*mark_event_button_read)(EventIndex evidx);

    /* frontmenu_ingame_evnt.h -- roomspace_prediction.c */
    TbBool (*is_battle_creature_over_active)(void);

    /* engine_render.h -- roomspace_prediction.c */
    void (*hide_map_volume_box)(void);
    void (*reset_box_lag_compensation)(void);

    /* cursor_tag.h -- roomspace_prediction.c/player_compchecks.c/
       player_comptask.c/roomspace.c/thing_doors.c */
    unsigned char (*tag_cursor_blocks_dig)(struct PlayerInfo *player, NetUserId user, const struct Packet *pckt, struct RoomSpace *render_roomspace, MapSubtlCoord stl_x, MapSubtlCoord stl_y, TbBool full_slab);
    TbBool (*tag_cursor_blocks_place_door)(PlayerNumber plyr_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    TbBool (*tag_cursor_blocks_place_room)(NetUserId user, MapSubtlCoord stl_x, MapSubtlCoord stl_y, TbBool full_slab);
    TbBool (*tag_cursor_blocks_sell_area)(PlayerNumber plyr_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y, TbBool full_slab);

    /* engine_redraw.h */
    void (*set_engine_view)(struct PlayerInfo *player, int64_t val);
    void (*setup_engine_window)(int64_t x1, int64_t y1, int64_t x2, int64_t y2);

    /* light_data.h -- per-thing/per-slab dynamic light management, called
       from kfx_sim (creature_states.c/creature_states_prisn.c/power_hand.c/
       slab_data.c/thing_data.c/thing_effects.c/thing_shots.c/
       thing_traps.c/creature_control.c/thing_creature.c/thing_list.c/
       player_instances.c/room_jobs.c) and kfx_config (config_objects.c/
       lvl_filesdk1.c, at config-reload/level-load time). */
    int64_t (*light_create_light)(struct InitLight *ilght);
    void (*light_init_dungeon_heart)(int64_t lgt_id, int64_t min_radius, int64_t min_intensity);
    void (*light_delete_light)(int64_t idx);
    void (*light_turn_light_off)(int64_t num);
    void (*light_turn_light_on)(int64_t num);
    unsigned char (*light_get_light_intensity)(int64_t idx);
    void (*light_set_light_intensity)(int64_t idx, unsigned char intensity);
    void (*light_signal_update_in_area)(int64_t sx, int64_t sy, int64_t ex, int64_t ey);
    void (*light_set_light_never_cache)(int64_t lgt_id);
    int64_t (*light_is_light_allocated)(int64_t lgt_id);
    void (*light_set_light_position)(int64_t lgt_id, struct Coord3d *pos);
    int64_t (*light_get_light_radius)(int64_t lgt_id);
    void (*light_set_light_radius)(int64_t lgt_id, int64_t radius);
    void (*light_initialise)(void);
    int64_t (*light_count_lights)(void);
    TbBool (*light_create_light_adv)(VALUE *init_data);

    /* game_loop.h -- kfx_sim's thing_objects.c (dungeon heart destruction)
       triggers kfx_game's top-level dungeon-destroyed/devastate-from-heart
       orchestration; found via scripts/check_layering_symbols.py
       (docs/refactor/todo/check-layering-symbol-level-blind-spot.md). */
    void (*process_dungeon_destroy)(struct Thing *heartng);
    void (*initialise_devastate_dungeon_from_heart)(PlayerNumber plyr_idx);

    /* engine_textures.h -- kfx_sim's lvl_filesdk1.c triggers loading the
       level's texture map at level-load time; kfx_render owns the
       texture data. Found via scripts/check_layering_symbols.py
       (docs/refactor/todo/check-layering-symbol-level-blind-spot.md). */
    TbBool (*load_texture_map_file)(uint64_t tmapidx, LevelNumber lvnum, int64_t fgroup);

    /* frontend.h -- kfx_sim's map_events.c reads two fields
       (turns_between_events/lifespan_turns) of this per-event-kind
       table; the table itself mixes UI fields (button sprite/tooltip,
       kfx_render-owned enum values) with these sim fields, so it stays
       kfx_frontend-owned rather than moving down. Found via
       scripts/check_layering_symbols.py (docs/refactor/todo/
       check-layering-symbol-level-blind-spot.md). */
    const struct EventTypeInfo *(*get_event_button_info)(EventKind evkind);

    /* front_lvlstats.h -- kfx_sim's player_utils.c triggers
       (re)initialising the level-stats screen at level end; kfx_frontend
       owns that screen's state. */
    void (*frontstats_initialise)(void);

    /* kjm_input.h -- raw mouse/keyboard query functions, called from
       kfx_render (engine_redraw.c/engine_render.c/vidfade.c) and
       kfx_sim (power_hand.c/roomspace.c/thing_data.c). */
    int64_t (*GetMouseX)(void);
    int64_t (*GetMouseY)(void);
    int64_t (*is_mouse_pressed_lrbutton)(void);
    int64_t (*is_key_pressed)(TbKeyCode key, TbKeyMods kmodif);
    TbBool (*mouse_is_over_panel_map)(ScreenCoord x, ScreenCoord y);
    TbBool (*is_left_button_held)(void); /* left_button_held global -- roomspace.c's only real need */

    /* vidfade.h -- palette-flash functions called from kfx_sim
       (player_instances.c/power_process.c/thing_creature.c/
       thing_effects.c/thing_shots.c/thing_stats.c). */
    void (*PaletteSetUserPalette)(NetUserId user, unsigned char *pal);
    void (*PaletteApplyPainToPlayer)(struct PlayerInfo *player, int64_t intense);

    /* frontend.h -- called from kfx_sim (creature_control.c/player_data.c/
       player_instances.c/player_utils.c/room_util.c/thing_creature.c/
       thing_data.c). */
    uint64_t (*toggle_status_menu)(int64_t visible);
    void (*turn_off_roaming_menus)(void);
    void (*initialise_tab_tags_and_menu)(MenuID menu_id);
    void (*init_gui)(void);
    void (*set_gui_visible)(TbBool visible);
    void (*update_player_objectives)(PlayerNumber plyr_idx);
    void (*create_message_box)(const char *title, const char *line1, const char *line2, const char *line3, const char *line4, const char *line5);

    /* gui_frontmenu.h -- called from kfx_sim (map_events.c/
       player_instances.c/player_utils.c/power_specials.c/
       thing_creature.c). */
    void (*turn_on_menu)(MenuID idx);
    void (*turn_off_menu)(MenuID mnu_idx);
    void (*turn_off_query_menus)(void);
    void (*turn_off_all_menus)(void);
    int64_t (*turn_off_all_window_menus)(void);
    void (*turn_on_main_panel_menu)(void);
    void (*turn_off_all_panel_menus)(void);
    void (*turn_off_event_box_if_necessary)(PlayerNumber plyr_idx, unsigned char event_idx);

    /* gui_frontmenu.h -- refresh_active_button_sprites_for_player wraps a
       whole GUI-panel-button-icon refresh loop that used to read
       kfx_frontend's active_buttons array directly from
       player_utils.c's set_player_colour(). */
    void (*refresh_active_button_sprites_for_player)(PlayerNumber plyr_idx);

    /* frontmenu_ingame_tabs.h -- thing_creature.c's only real need. */
    RoomIndex (*find_next_room_of_type)(PlayerNumber plyr_idx, RoomKind rkind);

    /* local_camera.h -- camera smoothing/interpolation is kfx_render's
       concern, but kfx_sim's player-instance/thing-navigation code
       needs to trigger it as a side effect of simulation events
       (player_instances.c/player_utils.c/power_process.c/
       thing_creature.c/thing_navigate.c/thing_objects.c). */
    void (*sync_local_camera)(struct PlayerInfo *player);
    void (*set_local_camera_destination)(struct PlayerInfo *player);
    struct Camera *(*get_local_camera)(struct Camera *cam);
    void (*move_local_camera_to_position)(MapCoord x, MapCoord y);

    /* engine_camera.h -- camera zoom/movement is kfx_render's concern,
       but kfx_sim triggers it as a side effect of simulation events
       (player_instances.c/player_utils.c/thing_creature.c/
       thing_effects.c/thing_shots.c). */
    int64_t (*get_camera_zoom)(struct Camera *cam);
    void (*set_camera_zoom)(struct Camera *cam, int64_t val);
    void (*view_zoom_camera_in)(struct Camera *cam, int64_t limit_max, int64_t limit_min);
    void (*view_zoom_camera_out)(struct Camera *cam, int64_t limit_max, int64_t limit_min);
    void (*view_set_camera_move_to_position)(struct Camera *cam, MapCoord x, MapCoord y, MapCoordDelta *move_x, MapCoordDelta *move_y);
    TbBool (*view_move_camera_to_position)(struct Camera *cam, MapCoord x, MapCoord y, MapCoordDelta move_x, MapCoordDelta move_y);
    void (*init_player_cameras)(struct PlayerInfo *player);
    TbBool (*any_player_close_enough_to_see)(const struct Coord3d *pos);
    uint64_t (*lightning_is_close_to_player)(struct PlayerInfo *player, struct Coord3d *pos);

    /* packets.h -- thing_creature.c's only real need (avoids a full
       struct Packet dereference; struct Packet stays forward-declared
       only). */
    TbBool (*packet_crtr_control_pressed)(struct Packet *packet);

    /* gui_soundmsgs.h */
    TbBool (*output_message_far_from_thing)(const struct Thing *thing, SoundSmplTblID smpl_idx, int64_t duration);

    /* kfx_net_state.h -- session state kfx_net (and kfx_game/kfx_frontend/
       kfx_apploop) own; engine_redraw.c/player_computer.c/
       roomspace_prediction.c only ever read it, and player_data.c's one
       write (resetting human_players_count on player removal) is the
       "config/sim writes into a value owned above" shape covered
       elsewhere in this codebase by setter-shaped callback entries. */
    TbBool (*get_packet_load_enable)(void);
    PlayerNumber (*get_local_plyr_idx)(void);
    int64_t (*get_input_lag_turns)(void);
    void (*set_human_players_count)(int64_t count);
    // player_utils.c's init_player_as_type()/init_players() -- narrow
    // reads/one increment of kfx_net_state.packet_save_head fields, same
    // "config/sim needs a value owned above" shape as the entries above.
    int64_t (*get_isometric_view_zoom_level)(void);
    int64_t (*get_frontview_zoom_level)(void);
    TbBool (*get_player_exists_flag)(PlayerNumber plyr_idx);
    TbBool (*get_player_comp_flag)(PlayerNumber plyr_idx);
    void (*increment_human_players_count)(void);
    // kfx_game_state.h -- get_loaded_level_number() is a static inline
    // there (no external symbol to bare-extern); player_utils.c reads
    // just the one field, same shape as the entries above.
    LevelNumber (*get_loaded_level_number)(void);
    LevelNumber (*get_selected_level_number)(void);
    LevelNumber (*get_level_number)(void);
    GameTurn (*get_play_gameturn)(void);

    /* front_input.h */
    void (*update_time)(void);
    void (*get_game_time)(struct GameTime *GT, uint64_t turns, uint64_t fps);
    int64_t (*get_zoom_key_room_order)(int64_t idx);

    /* net_exchange_gameplay.h */
    const struct Packet *(*get_history_packet)(NetUserId user, GameTurn turn);

    /* lens_api.h */
    void (*setup_eye_lens)(int64_t nlens);
    TbBool (*lens_is_ready)(void);
    TbPixel *(*lens_get_render_target)(void);
    uint64_t (*lens_get_render_target_width)(void);
    uint64_t (*lens_get_render_target_height)(void);
    void (*draw_lens_effect)(TbPixel *dstbuf, int64_t dstpitch, TbPixel *srcbuf, int64_t srcpitch, int64_t width, int64_t height, int64_t viewport_x, int64_t effect);

    /* engine_arrays.h */
    int64_t (*get_td_animation_sprite)(int64_t animation_sprite);

    /* engine_render.h */
    void (*process_keeper_sprite)(int64_t x, int64_t y, int64_t a3, int64_t kspr_angle, unsigned char a5, int64_t a6);
    void (*engine)(struct PlayerInfo *player, struct Camera *cam);

    /* game_saves.h -- transfer-creature power writes into kfx_game's
       cross-level struct IntralevelData (intralvl). */
    TbBool (*add_transfered_creature)(PlayerNumber plyr_idx, ThingModel model, CrtrExpLevel exp_level, char *name);
    void (*clear_transfered_creatures)(void);

    /* sounds.h -- clear_things_and_persons_data() resets kfx_game's
       ambient-sound-thing-index tracking alongside kfx_sim's own thing
       lists, so a level/torture-scene reset can't leave a stale index
       pointing at a thing that no longer exists. */
    void (*reset_ambient_sound_thing_idx)(void);

    /* engine_lenses.h */
    unsigned char (*get_lens_mode)(void);

    /* gui_tooltips.h -- hides the on-screen tooltip when the local
       player leaves Direct Control possession. */
    void (*hide_tooltip)(void);

    /* frontmenu_ingame_evnt.h -- player_utils.c checks whether the win/
       lose timer HUD element is enabled and, if so, writes the game's
       final turn count into it. */
    TbBool (*timer_enabled)(void);
    void (*set_timer_turns)(uint64_t turns);

    /* game_merge.h -- power_specials.c's create_transferred_creatures_on_level()
       reads kfx_game's cross-level struct IntralevelData (intralvl) by
       slot instead of taking the type by value. Mirrors
       add_transfered_creature's shape. */
    TbBool (*get_transferred_creature)(PlayerNumber plyr_idx, int64_t idx, ThingModel *model, CrtrExpLevel *exp_level, char *name_buf, size_t name_buf_size);

    /* game_merge.h -- power_specials.c's activate_bonus_level() marks a
       bonus level visible for the current singleplayer level. */
    TbBool (*activate_bonus_level_for_singleplayer)(struct PlayerInfo *player, uint64_t sp_lvnum);
};
void set_sim_feedback_callbacks(const struct SimFeedbackCallbacks *callbacks);
extern const struct SimFeedbackCallbacks *sim_feedback;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
