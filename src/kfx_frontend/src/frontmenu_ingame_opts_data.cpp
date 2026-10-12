/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_ingame_opts_data.cpp
 *     In-game options GUI, available under "escape" while in game.
 * @par Purpose:
 *     Structures to show and maintain option menus ingame.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 20 Apr 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "frontmenu_ingame_opts.h"
#include "globals.h"
#include "bflib_basics.h"

#include "bflib_guibtns.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"
#include "bflib_vidraw.h"

#include "gui_frontbtns.h"
#include "gui_draw.h"
#include "frontend.h"
#include "frontmenu_saves.h"
#include "config_settings.h"
#include "frontmenu_options.h"
#include "game_legacy.h"
#include "sprites.h"
#include "kfx_frontend_state.h"
#include "kfx_local_view.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif


void maintain_compsetting_button(struct GuiButton* gbtn);

/******************************************************************************/
struct MsgBoxInfo MsgBox;

// Non-NULL no-op callback so that the controller snapping logic does not ignore the button
static void no_op(struct GuiButton* gbtn) {}

// GCC's -Wmissing-field-initializers fires on a partially-designated
// GuiButtonInit aggregate in this C++ translation unit even though the
// omitted fields are the struct's own zero defaults; not a real risk here
// since every field is still named where it matters.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
struct GuiButtonInit options_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 10, .pos_x = 999, .pos_y = 10, .width = 155, .height = 32, .draw_call = gui_area_text, .sprite_idx = 1, .tooltip_stridx = GUIStr_MnuOptions },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .click_event = no_op, .scr_pos_x = 12, .scr_pos_y = 36, .pos_x = 12, .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_load, .tooltip_stridx = GUIStr_LoadGameDesc, .parent_menu = &load_menu, .maintain_call = maintain_loadsave },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .click_event = no_op, .scr_pos_x = 60, .scr_pos_y = 36, .pos_x = 60, .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_save, .tooltip_stridx = GUIStr_SaveGameDesc, .parent_menu = &save_menu, .maintain_call = maintain_loadsave },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .click_event = no_op, .scr_pos_x = 108, .scr_pos_y = 36, .pos_x = 108, .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_graphc, .tooltip_stridx = GUIStr_GraphicsMenuDesc, .parent_menu = &video_menu },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .click_event = no_op, .scr_pos_x = 156, .scr_pos_y = 36, .pos_x = 156, .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_sound, .tooltip_stridx = GUIStr_SoundMenuDesc, .parent_menu = &sound_menu },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .click_event = no_op, .scr_pos_x = 204, .scr_pos_y = 36, .pos_x = 204, .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_compsetting_button, .sprite_idx = GPS_options_cassist_btn_black_a, .tooltip_stridx = GUIStr_ComputerAssistDesc, .parent_menu = &autopilot_menu },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .click_event = no_op, .scr_pos_x = 252, .scr_pos_y = 36, .pos_x = 252, .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_exit, .tooltip_stridx = GUIStr_QuitGameDesc, .parent_menu = &quit_menu },
  { .gbtype = -1 },
};

struct GuiButtonInit quit_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 10, .pos_x = 999, .pos_y = 10, .width = 210, .height = 32, .draw_call = gui_area_text, .sprite_idx = 1, .tooltip_stridx = GUIStr_ConfirmYouSure },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .click_event = no_op, .scr_pos_x = 70, .scr_pos_y = 24, .pos_x = 72, .pos_y = 58, .width = 46, .height = 32, .draw_call = gui_area_normal_button, .sprite_idx = GBS_options_button_smd_no, .tooltip_stridx = GUIStr_ConfirmNo },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .click_event = gui_quit_game, .scr_pos_x = 136, .scr_pos_y = 24, .pos_x = 138, .pos_y = 58, .width = 46, .height = 32, .draw_call = gui_area_normal_button, .sprite_idx = GBS_options_button_smd_yes, .tooltip_stridx = GUIStr_ConfirmYes },
  { .gbtype = -1 },
};

struct GuiButtonInit error_box_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 10, .pos_x = 999, .pos_y = 10, .width = 155, .height = 32, .draw_call = gui_area_text, .sprite_idx = 1, .tooltip_stridx = GUIStr_Error },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 65, .pos_x = 999, .width = 250, .height = 32, .draw_call = gui_area_text, .tooltip_stridx = GUIStr_Empty, .content = { .str = gui_error_text } },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .scr_pos_x = 999, .scr_pos_y = 100, .pos_x = 999, .pos_y = 132, .width = 46, .height = 34, .draw_call = gui_area_normal_button, .sprite_idx = GBS_options_button_smd_yes, .tooltip_stridx = GUIStr_CloseWindow },
  { .gbtype = -1 },
};

struct GuiButtonInit instance_menu_buttons[] = {
  { .gbtype = -1 },
};

struct GuiButtonInit pause_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 999, .pos_x = 999, .pos_y = 999, .width = 140, .height = 32, .draw_call = gui_area_text, .tooltip_stridx = GUIStr_PausedMsg },
  { .gbtype = -1 },
};

struct GuiButtonInit autopilot_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 10, .pos_x = 999, .pos_y = 10, .width = 155, .height = 32, .draw_call = gui_area_text, .sprite_idx = 1, .tooltip_stridx = GUIStr_MnuComputer },
  { .gbtype = LbBtnT_RadioBtn, .click_event = gui_set_autopilot, .scr_pos_x = FE_ROW_Y(12, 48, 0), .scr_pos_y = 36, .pos_x = FE_ROW_Y(12, 48, 0), .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_options_cassist_btn_orange, .tooltip_stridx = GUIStr_AggressiveAssistDesc, .content = { .ptr = &kfx_local_view.comp_player_aggressive }, .maintain_call = maintain_compsetting_button },
  { .gbtype = LbBtnT_RadioBtn, .click_event = gui_set_autopilot, .btype_value = 1, .scr_pos_x = FE_ROW_Y(12, 48, 1), .scr_pos_y = 36, .pos_x = FE_ROW_Y(12, 48, 1), .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_options_cassist_btn_yellow, .tooltip_stridx = GUIStr_DefensiveAssistDesc, .content = { .ptr = &kfx_local_view.comp_player_defensive }, .maintain_call = maintain_compsetting_button },
  { .gbtype = LbBtnT_RadioBtn, .click_event = gui_set_autopilot, .btype_value = 2, .scr_pos_x = FE_ROW_Y(12, 48, 2), .scr_pos_y = 36, .pos_x = FE_ROW_Y(12, 48, 2), .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_options_cassist_btn_pink, .tooltip_stridx = GUIStr_ConstructionAssistDesc, .content = { .ptr = &kfx_local_view.comp_player_construct }, .maintain_call = maintain_compsetting_button },
  { .gbtype = LbBtnT_RadioBtn, .click_event = gui_set_autopilot, .btype_value = 3, .scr_pos_x = FE_ROW_Y(12, 48, 3), .scr_pos_y = 36, .pos_x = FE_ROW_Y(12, 48, 3), .pos_y = 36, .width = 46, .height = 64, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_options_cassist_btn_green, .tooltip_stridx = GUIStr_MoveOnlyAssistDesc, .content = { .ptr = &kfx_local_view.comp_player_creatrsonly }, .maintain_call = maintain_compsetting_button },
  { .gbtype = -1 },
};

struct GuiButtonInit video_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 10, .pos_x = 999, .pos_y = 10, .width = 155, .height = 32, .draw_call = gui_area_text, .sprite_idx = 1, .tooltip_stridx = GUIStr_MnuGraphicsOptions },
  { .gbtype = LbBtnT_ToggleBtn, .click_event = gui_video_shadows, .scr_pos_x = 28, .scr_pos_y = 38, .pos_x = 30, .pos_y = 38, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_grph_shadow0, .tooltip_stridx = GUIStr_OptionShadowsDesc, .content = { .ptr = &video_shadows }, .maxval = 4 },
  { .gbtype = LbBtnT_ToggleBtn, .click_event = gui_video_view_distance_level, .scr_pos_x = 76, .scr_pos_y = 38, .pos_x = 78, .pos_y = 38, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_grph_range0, .tooltip_stridx = GUIStr_OptionViewDistanceDesc, .content = { .ptr = &video_view_distance_level }, .maxval = 3 },
  { .gbtype = LbBtnT_ToggleBtn, .click_event = gui_video_rotate_mode, .scr_pos_x = 124, .scr_pos_y = 38, .pos_x = 126, .pos_y = 38, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_grph_pers_rot, .tooltip_stridx = GUIStr_OptionViewTypeDesc, .content = { .ptr = &settings.video_rotate_mode }, .maxval = 2 },
  { .gbtype = LbBtnT_ToggleBtn, .click_event = gui_video_cluedo_mode, .scr_pos_x = 28, .scr_pos_y = 100, .pos_x = 30, .pos_y = 100, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_grph_wall_hi, .tooltip_stridx = GUIStr_OptionWallHeightDesc, .content = { .ptr = &video_cluedo_mode }, .maxval = 1, .maintain_call = gui_video_cluedo_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_video_gamma_correction, .scr_pos_x = 76, .scr_pos_y = 100, .pos_x = 78, .pos_y = 100, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_grph_gamma, .tooltip_stridx = GUIStr_OptionGammaCorrectionDesc, .content = { .ptr = &video_gamma_correction } },
  { .gbtype = -1 },
};

struct GuiButtonInit sound_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 10, .pos_x = 999, .pos_y = 10, .width = 155, .height = 32, .draw_call = gui_area_text, .sprite_idx = 1, .tooltip_stridx = GUIStr_MnuSoundOptions },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 28, .pos_x = 10, .pos_y = 28, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_snd_music, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 80, .pos_x = 10, .pos_y = 80, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_options_button_snd_sounds, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 132, .pos_x = 10, .pos_y = 132, .width = 46, .height = 64, .draw_call = gui_area_no_anim_button, .sprite_idx = GBS_optionsbutton_snd_voice, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_HorizSlider, .id_num = BID_SOUND_VOL, .click_event = gui_set_sound_volume, .scr_pos_x = 66, .scr_pos_y = 58, .pos_x = 66, .pos_y = 58, .width = 190, .height = 30, .draw_call = gui_area_slider, .tooltip_stridx = GUIStr_OptionSoundFx, .maxval = 255 },
  { .gbtype = LbBtnT_HorizSlider, .id_num = BID_MUSIC_VOL, .click_event = gui_set_music_volume, .scr_pos_x = 66, .scr_pos_y = 110, .pos_x = 66, .pos_y = 110, .width = 190, .height = 30, .draw_call = gui_area_slider, .tooltip_stridx = GUIStr_OptionMusic, .maxval = 255 },
  { .gbtype = LbBtnT_HorizSlider, .id_num = BID_MENTOR_VOL, .click_event = gui_set_mentor_volume, .scr_pos_x = 66, .scr_pos_y = 162, .pos_x = 66, .pos_y = 162, .width = 190, .height = 30, .draw_call = gui_area_slider, .tooltip_stridx = GUIStr_OptionVoice, .maxval = 255 },
  { .gbtype = -1 },
};

struct GuiButtonInit message_box_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 10, .pos_x = 999, .pos_y = 10, .width = 155, .height = 32, .draw_call = gui_area_text, .sprite_idx = 1, .tooltip_stridx = GUIStr_Empty, .content = { .str = MsgBox.title } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 35, .pos_x = 999, .width = 250, .height = 32, .draw_call = gui_area_text, .tooltip_stridx = GUIStr_Empty, .content = { .str = MsgBox.line1 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 55, .pos_x = 999, .width = 250, .height = 32, .draw_call = gui_area_text, .tooltip_stridx = GUIStr_Empty, .content = { .str = MsgBox.line2 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 75, .pos_x = 999, .width = 250, .height = 32, .draw_call = gui_area_text, .tooltip_stridx = GUIStr_Empty, .content = { .str = MsgBox.line3 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 95, .pos_x = 999, .width = 250, .height = 32, .draw_call = gui_area_text, .tooltip_stridx = GUIStr_Empty, .content = { .str = MsgBox.line4 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 115, .pos_x = 999, .width = 250, .height = 32, .draw_call = gui_area_text, .tooltip_stridx = GUIStr_Empty, .content = { .str = MsgBox.line5 } },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .scr_pos_x = 999, .scr_pos_y = 115, .pos_x = 999, .pos_y = 132, .width = 46, .height = 34, .draw_call = gui_area_normal_button, .sprite_idx = GBS_options_button_smd_yes, .tooltip_stridx = GUIStr_CloseWindow },
  { .gbtype = -1 },
};
#pragma GCC diagnostic pop

struct GuiMenu options_menu =
 { GMnu_OPTIONS,      0, 1, options_menu_buttons,       POS_GAMECTR,POS_GAMECTR,308, 120, gui_pretty_background,       0, NULL,    NULL,                    0, 1, 0,};
struct GuiMenu instance_menu =
 { GMnu_INSTANCE,     0, 1, instance_menu_buttons,      POS_GAMECTR,POS_GAMECTR,318, 120, gui_pretty_background,       0, NULL,    NULL,                    0, 1, 0,};
struct GuiMenu quit_menu =
 { GMnu_QUIT,         0, 1, quit_menu_buttons,          POS_GAMECTR,POS_GAMECTR,264, 116, gui_pretty_background,       0, NULL,    NULL,                    0, 1, 0,};
struct GuiMenu error_box =
 { GMnu_ERROR_BOX,    0, 1, error_box_buttons,          POS_GAMECTR,POS_GAMECTR,280, 180, gui_pretty_background,       0, NULL,    NULL,                    0, 1, 0,};
struct GuiMenu autopilot_menu =
 { GMnu_AUTOPILOT,    0, 4, autopilot_menu_buttons,     POS_GAMECTR,POS_GAMECTR,224, 120, gui_pretty_background,       0, NULL,    NULL,                    0, 1, 0,};

struct GuiMenu video_menu =
 { GMnu_VIDEO, 0, 4, video_menu_buttons,         POS_GAMECTR,POS_GAMECTR,200, 180, gui_pretty_background,       0, NULL,    init_video_menu,         0, 1, 0,};
struct GuiMenu sound_menu =
 { GMnu_SOUND, 0, 4, sound_menu_buttons,         POS_GAMECTR,POS_GAMECTR,280, 225, gui_pretty_background,       0, NULL,    init_audio_menu,         0, 1, 0,};

struct GuiMenu message_box =
{ GMnu_MSG_BOX,    0, 1, message_box_buttons,          POS_GAMECTR,POS_GAMECTR,280, 180, gui_pretty_background,       0, NULL,    NULL,                    0, 1, 0,};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
