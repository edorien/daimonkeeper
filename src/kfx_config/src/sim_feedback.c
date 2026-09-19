/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sim_feedback.c
 *     Callback-registration implementation. See sim_feedback.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "sim_feedback.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static long noop_report_error_stat(int stat_num) { return 0; }
static TbBool noop_show_onscreen_msg(int nturns, const char *msg) { return false; }
static TbBool noop_play_sound_message(SoundSmplTblID smpl_idx, long duration) { return false; }
static TbBool noop_output_room_message(PlayerNumber plyr_idx, RoomKind rkind, OutputMessageKind msg_kind) { return false; }
static TbBool noop_play_sound_message_far_from_thing(const struct Thing *thing, SoundSmplTblID smpl_idx, long duration) { return false; }
static TbBool noop_play_speech_ref(const struct SpeechRef *ref, long duration) { return false; }
static void noop_clear_sound_messages(void) {}
static void noop_process_sound_messages(void) {}
static void noop_clear_messages_from_player(char msg_type, PlayerNumber plyr_idx) {}
static void noop_targeted_message_add(char msg_type, PlayerNumber plyr_idx, PlayerNumber target_idx, unsigned long timeout, const char *msg) {}
static void noop_message_add(char msg_type, short idx, const char *msg) {}
static void noop_message_add_fmt(char msg_type, short idx, const char *fmt_str, ...) {}
static void noop_zero_messages(void) {}
static void noop_show_real_time_taken(void) {}
static void noop_thing_play_sample(struct Thing *thing, SoundSmplTblID smpl_idx, SoundPitch pitch, char repeats, unsigned char ctype, unsigned char flags, long priority, SoundVolume volume) {}
static void noop_stop_thing_playing_sample(struct Thing *thing, SoundSmplTblID smpl_idx) {}
static struct Thing *noop_create_ambient_sound(const struct Coord3d *pos, ThingModel model, PlayerNumber owner) { return NULL; }
static void noop_play_sound_if_close_to_receiver(struct Coord3d *soundpos, SoundSmplTblID smpl_idx) {}
static void noop_play_thing_walking(struct Thing *thing) {}
static TbBool noop_is_roomspace_key_pressed(void) { return false; }
static void noop_set_room_type_highlighted(char room_kind) {}
static void noop_set_visible_event_idx(EventIndex evidx) {}
static void noop_clear_all_event_button_states(void) {}
static void noop_clear_event_button_state(EventIndex evidx) {}
static void noop_mark_event_button_read(EventIndex evidx) {}
static TbBool noop_is_battle_creature_over_active(void) { return false; }
static void noop_hide_map_volume_box(void) {}
static void noop_reset_box_lag_compensation(void) {}
static unsigned char noop_tag_cursor_blocks_dig(struct PlayerInfo *player, NetUserId user, const struct Packet *pckt, struct RoomSpace *render_roomspace, MapSubtlCoord stl_x, MapSubtlCoord stl_y, TbBool full_slab) { return 0; }
static TbBool noop_tag_cursor_blocks_place_door(PlayerNumber plyr_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y) { return false; }
static TbBool noop_tag_cursor_blocks_place_room(NetUserId user, MapSubtlCoord stl_x, MapSubtlCoord stl_y, TbBool full_slab) { return false; }
static TbBool noop_tag_cursor_blocks_sell_area(PlayerNumber plyr_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y, TbBool full_slab) { return false; }
static void noop_set_engine_view(struct PlayerInfo *player, long val) {}
static void noop_setup_engine_window(long x1, long y1, long x2, long y2) {}
static long noop_light_create_light(struct InitLight *ilght) { return 0; }
static void noop_light_init_dungeon_heart(long lgt_id, long min_radius, long min_intensity) {}
static void noop_light_delete_light(long idx) {}
static void noop_light_turn_light_off(long num) {}
static void noop_light_turn_light_on(long num) {}
static unsigned char noop_light_get_light_intensity(long idx) { return 0; }
static void noop_light_set_light_intensity(long idx, unsigned char intensity) {}
static void noop_light_signal_update_in_area(long sx, long sy, long ex, long ey) {}
static void noop_light_set_light_never_cache(long lgt_id) {}
static long noop_light_is_light_allocated(long lgt_id) { return 0; }
static void noop_light_set_light_position(long lgt_id, struct Coord3d *pos) {}
static unsigned short noop_light_get_light_radius(long lgt_id) { return 0; }
static void noop_light_set_light_radius(long lgt_id, unsigned short radius) {}
static void noop_light_initialise(void) {}
static int noop_light_count_lights(void) { return 0; }
static TbBool noop_light_create_light_adv(VALUE *init_data) { return false; }
static void noop_process_dungeon_destroy(struct Thing *heartng) {}
static void noop_initialise_devastate_dungeon_from_heart(PlayerNumber plyr_idx) {}
static TbBool noop_load_texture_map_file(unsigned long tmapidx, LevelNumber lvnum, short fgroup) { return false; }
static const struct EventTypeInfo *noop_get_event_button_info(EventKind evkind) { return NULL; }
static void noop_frontstats_initialise(void) {}
static long noop_GetMouseX(void) { return 0; }
static long noop_GetMouseY(void) { return 0; }
static short noop_is_mouse_pressed_lrbutton(void) { return 0; }
static short noop_is_key_pressed(TbKeyCode key, TbKeyMods kmodif) { return 0; }
static TbBool noop_mouse_is_over_panel_map(ScreenCoord x, ScreenCoord y) { return false; }
static TbBool noop_is_left_button_held(void) { return false; }
static void noop_PaletteSetUserPalette(NetUserId user, unsigned char *pal) {}
static void noop_PaletteApplyPainToPlayer(struct PlayerInfo *player, long intense) {}
static unsigned long noop_toggle_status_menu(short visible) { return 0; }
static void noop_turn_off_roaming_menus(void) {}
static void noop_initialise_tab_tags_and_menu(MenuID menu_id) {}
static void noop_init_gui(void) {}
static void noop_set_gui_visible(TbBool visible) {}
static void noop_update_player_objectives(PlayerNumber plyr_idx) {}
static void noop_create_message_box(const char *title, const char *line1, const char *line2, const char *line3, const char *line4, const char *line5) {}
static void noop_turn_on_menu(MenuID idx) {}
static void noop_turn_off_menu(MenuID mnu_idx) {}
static void noop_sim_feedback_turn_off_query_menus(void) {}
static void noop_turn_off_all_menus(void) {}
static short noop_turn_off_all_window_menus(void) { return 0; }
static void noop_sim_feedback_turn_on_main_panel_menu(void) {}
static void noop_turn_off_all_panel_menus(void) {}
static void noop_turn_off_event_box_if_necessary(PlayerNumber plyr_idx, unsigned char event_idx) {}
static void noop_refresh_active_button_sprites_for_player(PlayerNumber plyr_idx) {}
static RoomIndex noop_find_next_room_of_type(PlayerNumber plyr_idx, RoomKind rkind) { return 0; }
static TbBool noop_packet_crtr_control_pressed(struct Packet *packet) { return false; }
static TbBool noop_output_message_far_from_thing(const struct Thing *thing, SoundSmplTblID smpl_idx, long duration) { return false; }
static TbBool noop_get_packet_load_enable(void) { return false; }
static PlayerNumber noop_get_local_plyr_idx(void) { return 0; }
static int noop_get_input_lag_turns(void) { return 0; }
static void noop_set_active_players_count(int count) {}
static long noop_get_isometric_view_zoom_level(void) { return 0; }
static long noop_get_frontview_zoom_level(void) { return 0; }
static TbBool noop_get_player_exists_flag(PlayerNumber plyr_idx) { return false; }
static TbBool noop_get_player_comp_flag(PlayerNumber plyr_idx) { return false; }
static void noop_increment_active_players_count(void) {}
static LevelNumber noop_get_loaded_level_number(void) { return 0; }
static LevelNumber noop_get_selected_level_number(void) { return 0; }
static LevelNumber noop_get_level_number(void) { return 0; }
static GameTurn noop_get_play_gameturn(void) { return 0; }
static void noop_update_time(void) {}
static void noop_get_game_time(struct GameTime *GT, unsigned long turns, unsigned long fps) { GT->Seconds = 0; GT->Minutes = 0; GT->Hours = 0; }
static TbBool noop_player_has_enemies_to_defeat(const struct PlayerInfo *player) { return false; }
static unsigned short noop_get_zoom_key_room_order(long idx) { return 0; }
static const struct Packet *noop_get_history_packet(NetUserId user, GameTurn turn) { return NULL; }
static void noop_setup_eye_lens(long nlens) {}
static TbBool noop_lens_is_ready(void) { return false; }
static TbPixel *noop_lens_get_render_target(void) { return NULL; }
static unsigned int noop_lens_get_render_target_width(void) { return 0; }
static unsigned int noop_lens_get_render_target_height(void) { return 0; }
static void noop_draw_lens_effect(TbPixel *dstbuf, long dstpitch, TbPixel *srcbuf, long srcpitch, long width, long height, long viewport_x, long effect) {}
static short noop_get_td_animation_sprite(short animation_sprite) { return 0; }
static void noop_process_keeper_sprite(short x, short y, unsigned short a3, short kspr_angle, unsigned char a5, long a6) {}
static void noop_engine(struct PlayerInfo *player, struct Camera *cam) {}
static TbBool noop_add_transfered_creature(PlayerNumber plyr_idx, ThingModel model, CrtrExpLevel exp_level, char *name) { return false; }
static void noop_clear_transfered_creatures(void) {}
static void noop_reset_ambient_sound_thing_idx(void) {}
static unsigned char noop_get_lens_mode(void) { return 0; }
static void noop_hide_tooltip(void) {}
static TbBool noop_timer_enabled(void) { return false; }
static void noop_set_timer_turns(unsigned long turns) {}
static TbBool noop_get_transferred_creature(PlayerNumber plyr_idx, int idx, ThingModel *model, CrtrExpLevel *exp_level, char *name_buf, size_t name_buf_size) { return false; }
static TbBool noop_activate_bonus_level_for_singleplayer(struct PlayerInfo *player, unsigned long sp_lvnum) { return false; }
static void noop_sync_local_camera(struct PlayerInfo *player) {}
static void noop_set_local_camera_destination(struct PlayerInfo *player) {}
static struct Camera *noop_get_local_camera(struct Camera *cam) { return cam; }
static void noop_move_local_camera_to_position(MapCoord x, MapCoord y) {}
static long noop_get_camera_zoom(struct Camera *cam) { return 0; }
static void noop_set_camera_zoom(struct Camera *cam, long val) {}
static void noop_view_zoom_camera_in(struct Camera *cam, long limit_max, long limit_min) {}
static void noop_view_zoom_camera_out(struct Camera *cam, long limit_max, long limit_min) {}
static void noop_view_set_camera_move_to_position(struct Camera *cam, MapCoord x, MapCoord y, MapCoordDelta *move_x, MapCoordDelta *move_y) {}
static TbBool noop_view_move_camera_to_position(struct Camera *cam, MapCoord x, MapCoord y, MapCoordDelta move_x, MapCoordDelta move_y) { return false; }
static void noop_init_player_cameras(struct PlayerInfo *player) {}
static TbBool noop_any_player_close_enough_to_see(const struct Coord3d *pos) { return false; }
static unsigned long noop_lightning_is_close_to_player(struct PlayerInfo *player, struct Coord3d *pos) { return 0; }

static const struct SimFeedbackCallbacks default_sim_feedback = {
    &noop_report_error_stat,
    &noop_show_onscreen_msg,
    &noop_play_sound_message,
    &noop_output_room_message,
    &noop_play_sound_message_far_from_thing,
    &noop_play_speech_ref,
    &noop_clear_sound_messages,
    &noop_process_sound_messages,
    &noop_clear_messages_from_player,
    &noop_targeted_message_add,
    &noop_message_add,
    &noop_message_add_fmt,
    &noop_zero_messages,
    &noop_show_real_time_taken,
    &noop_thing_play_sample,
    &noop_stop_thing_playing_sample,
    &noop_create_ambient_sound,
    &noop_play_sound_if_close_to_receiver,
    &noop_play_thing_walking,
    &noop_is_roomspace_key_pressed,
    &noop_is_roomspace_key_pressed,
    &noop_is_roomspace_key_pressed,
    &noop_is_roomspace_key_pressed,
    &noop_is_roomspace_key_pressed,
    &noop_set_room_type_highlighted,
    &noop_set_visible_event_idx,
    &noop_clear_all_event_button_states,
    &noop_clear_event_button_state,
    &noop_mark_event_button_read,
    &noop_is_battle_creature_over_active,
    &noop_hide_map_volume_box,
    &noop_reset_box_lag_compensation,
    &noop_tag_cursor_blocks_dig,
    &noop_tag_cursor_blocks_place_door,
    &noop_tag_cursor_blocks_place_room,
    &noop_tag_cursor_blocks_sell_area,
    &noop_set_engine_view,
    &noop_setup_engine_window,
    &noop_light_create_light,
    &noop_light_init_dungeon_heart,
    &noop_light_delete_light,
    &noop_light_turn_light_off,
    &noop_light_turn_light_on,
    &noop_light_get_light_intensity,
    &noop_light_set_light_intensity,
    &noop_light_signal_update_in_area,
    &noop_light_set_light_never_cache,
    &noop_light_is_light_allocated,
    &noop_light_set_light_position,
    &noop_light_get_light_radius,
    &noop_light_set_light_radius,
    &noop_light_initialise,
    &noop_light_count_lights,
    &noop_light_create_light_adv,
    &noop_process_dungeon_destroy,
    &noop_initialise_devastate_dungeon_from_heart,
    &noop_load_texture_map_file,
    &noop_get_event_button_info,
    &noop_frontstats_initialise,
    &noop_GetMouseX,
    &noop_GetMouseY,
    &noop_is_mouse_pressed_lrbutton,
    &noop_is_key_pressed,
    &noop_mouse_is_over_panel_map,
    &noop_is_left_button_held,
    &noop_PaletteSetUserPalette,
    &noop_PaletteApplyPainToPlayer,
    &noop_toggle_status_menu,
    &noop_turn_off_roaming_menus,
    &noop_initialise_tab_tags_and_menu,
    &noop_init_gui,
    &noop_set_gui_visible,
    &noop_update_player_objectives,
    &noop_create_message_box,
    &noop_turn_on_menu,
    &noop_turn_off_menu,
    &noop_sim_feedback_turn_off_query_menus,
    &noop_turn_off_all_menus,
    &noop_turn_off_all_window_menus,
    &noop_sim_feedback_turn_on_main_panel_menu,
    &noop_turn_off_all_panel_menus,
    &noop_turn_off_event_box_if_necessary,
    &noop_refresh_active_button_sprites_for_player,
    &noop_find_next_room_of_type,
    &noop_sync_local_camera,
    &noop_set_local_camera_destination,
    &noop_get_local_camera,
    &noop_move_local_camera_to_position,
    &noop_get_camera_zoom,
    &noop_set_camera_zoom,
    &noop_view_zoom_camera_in,
    &noop_view_zoom_camera_out,
    &noop_view_set_camera_move_to_position,
    &noop_view_move_camera_to_position,
    &noop_init_player_cameras,
    &noop_any_player_close_enough_to_see,
    &noop_lightning_is_close_to_player,
    &noop_packet_crtr_control_pressed,
    &noop_output_message_far_from_thing,
    &noop_get_packet_load_enable,
    &noop_get_local_plyr_idx,
    &noop_get_input_lag_turns,
    &noop_set_active_players_count,
    &noop_get_isometric_view_zoom_level,
    &noop_get_frontview_zoom_level,
    &noop_get_player_exists_flag,
    &noop_get_player_comp_flag,
    &noop_increment_active_players_count,
    &noop_get_loaded_level_number,
    &noop_get_selected_level_number,
    &noop_get_level_number,
    &noop_get_play_gameturn,
    &noop_update_time, &noop_get_game_time, &noop_player_has_enemies_to_defeat, &noop_get_zoom_key_room_order,
    &noop_get_history_packet,
    &noop_setup_eye_lens,
    &noop_lens_is_ready,
    &noop_lens_get_render_target,
    &noop_lens_get_render_target_width,
    &noop_lens_get_render_target_height,
    &noop_draw_lens_effect,
    &noop_get_td_animation_sprite,
    &noop_process_keeper_sprite,
    &noop_engine,
    &noop_add_transfered_creature,
    &noop_clear_transfered_creatures,
    &noop_reset_ambient_sound_thing_idx,
    &noop_get_lens_mode,
    &noop_hide_tooltip,
    &noop_timer_enabled,
    &noop_set_timer_turns,
    &noop_get_transferred_creature,
    &noop_activate_bonus_level_for_singleplayer,
};
const struct SimFeedbackCallbacks *sim_feedback = &default_sim_feedback;

void set_sim_feedback_callbacks(const struct SimFeedbackCallbacks *callbacks)
{
    sim_feedback = callbacks ? callbacks : &default_sim_feedback;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
