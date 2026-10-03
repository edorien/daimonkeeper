/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontend.cpp
 *     Frontend menu implementation for Dungeon Keeper.
 * @par Purpose:
 *     Functions to display and maintain the game menu.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     10 Nov 2008 - 21 Apr 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "frontend.h"
#include "main_game.h"

#include <string.h>
#include "bflib_basics.h"
#include "globals.h"

#include "bflib_guibtns.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"
#include "bflib_dernc.h"
#include "bflib_datetm.h"
#include "bflib_keybrd.h"
#include "bflib_inputctrl.h"
#include "bflib_sndlib.h"
#include "bflib_mouse.h"
#include "bflib_vidraw.h"
#include "bflib_fileio.h"
#include "bflib_filelst.h"
#include "bflib_sound.h"
#include "net_lobby.h"
#include "config.h"
#include "config_strings.h"
#include "config_campaigns.h"
#include "config_creature.h"
#include "config_terrain.h"
#include "config_magic.h"
#include "config_spritecolors.h"
#include "scrcapt.h"
#include "gui_draw.h"
#include "kjm_input.h"
#include "vidmode.h"
#include "front_simple.h"
#include "front_input.h"
#include "front_fmvids.h"
#include "game_saves.h"
#include "game_campaign_progress.h" // Phase A: save/progress.cfg, new-menu only
#include "engine_render.h"
#include "engine_redraw.h"
#include "front_landview.h"
#include "front_credits.h"
#include "frontgui_screens.h"
#include "frontgui_ingame.h"
#include "front_torture.h"
#include "front_highscore.h"
#include "front_lvlstats.h"
#include "front_easter.h"
#include "front_network.h"
#include "net_game.h"
#include "frontmenu_net.h"
#include "frontmenu_options.h"
#include "frontmenu_specials.h"
#include "frontmenu_saves.h"
#include "frontmenu_select.h"
#include "frontmenu_landpreview.h"
#include "frontmenu_ingame_tabs.h"
#include "frontmenu_ingame_evnt.h"
#include "frontmenu_ingame_opts.h"
#include "lvl_filesdk1.h"
#include "thing_stats.h"
#include "thing_traps.h"
#include "power_hand.h"
#include "magic_powers.h"
#include "player_instances.h"
#include "local_camera.h"
#include "player_utils.h"
#include "config_players.h"
#include "gui_frontmenu.h"
#include "gui_frontbtns.h"
#include "gui_soundmsgs.h"
#include "vidfade.h"
#include "config_settings.h"
#include "config_strings.h"
#include "game_legacy.h"
#include "custom_sprites.h"
#include "sprites.h"
#include "moonphase.h"
#include "config_keeperfx.h"
#include "kfx_frontend_state.h"
#include "game_lifecycle.h"
#include "frontgui_skirmish_setup.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
TbClockMSec gui_message_timeout = 0;
char gui_message_text[TEXT_BUFFER_LENGTH];
static char path_string[178];
// vid_change_query_menu moved to kfx_render's vidmode.h/vidmode_data.cpp
// (stage 13.3, docs/refactor/stage-13-enforce-and-document.md).
TbBool right_click_tag_mode_toggle = false;
// default_tag_mode moved to kfx_sim's kfx_sim_state.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).

// GCC's -Wmissing-field-initializers fires on a partially-designated
// GuiButtonInit aggregate in this C++ translation unit even though the
// omitted fields are the struct's own zero defaults; not a real risk here
// since every field is still named where it matters.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
// Left-anchored, narrower main column (Start new game.../Multiplayer) plus a
// separate bottom-left row of 3 smaller buttons (Options/High score/Quit) --
// menu-v2 mockup layout (docs/refactor/gui/02-menu-v2-mockup-gap-analysis.md
// Phase 1), same renderer/chrome/backdrop as before. The narrow buttons use
// frontend_draw_button_icon (docs/refactor/gui/03-button-primitives.md)
// instead of frontend_draw_large_menu_button because the latter's chrome
// width is a hardcoded 3-way preset, not driven by .width -- narrowing
// .width alone would only clip the caption, not the button art.
#define FE_MAINMENU_COL_X    24
#define FE_MAINMENU_COL_W    260
#define FE_MAINMENU_ROW_H    42
#define FE_MAINMENU_ROW_STEP 48
#define FE_MAINMENU_ROW_Y0   90
#define FE_MAINMENU_SUBROW_Y   400
#define FE_MAINMENU_SUBROW_H   32
#define FE_MAINMENU_SUBROW_W   130
#define FE_MAINMENU_SUBROW_STEP 140
struct GuiButtonInit frontend_main_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MENU_TITLE, .scr_pos_x = 999, .scr_pos_y = 26, .pos_x = 999, .pos_y = 26, .width = 371, .height = 46, .draw_call = frontend_draw_large_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuMainMenu } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_start_new_game, .ptover_event = frontend_over_button, .btype_value = 3, .scr_pos_x = FE_MAINMENU_COL_X, .scr_pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 0), .pos_x = FE_MAINMENU_COL_X, .pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 0), .width = FE_MAINMENU_COL_W, .height = FE_MAINMENU_ROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuStartNewGame }, .maintain_call = frontend_main_menu_start_game_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_load_mappacks, .ptover_event = frontend_over_button, .btype_value = 34, .scr_pos_x = FE_MAINMENU_COL_X, .scr_pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 1), .pos_x = FE_MAINMENU_COL_X, .pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 1), .width = FE_MAINMENU_COL_W, .height = FE_MAINMENU_ROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuFreePlayLevels }, .maintain_call = frontend_mappacks_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_start_skirmish, .ptover_event = frontend_over_button, .scr_pos_x = FE_MAINMENU_COL_X, .scr_pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 2), .pos_x = FE_MAINMENU_COL_X, .pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 2), .width = FE_MAINMENU_COL_W, .height = FE_MAINMENU_ROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuSkirmish }, .maintain_call = frontend_main_menu_skirmish_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_load_continue_game, .ptover_event = frontend_over_button, .scr_pos_x = FE_MAINMENU_COL_X, .scr_pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 3), .pos_x = FE_MAINMENU_COL_X, .pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 3), .width = FE_MAINMENU_COL_W, .height = FE_MAINMENU_ROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuContinueGame }, .maintain_call = frontend_continue_game_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_change_state, .ptover_event = frontend_over_button, .btype_value = 2, .scr_pos_x = FE_MAINMENU_COL_X, .scr_pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 4), .pos_x = FE_MAINMENU_COL_X, .pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 4), .width = FE_MAINMENU_COL_W, .height = FE_MAINMENU_ROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuLoadGame }, .maintain_call = frontend_main_menu_load_game_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_netservice_change_state, .ptover_event = frontend_over_button, .btype_value = 4, .scr_pos_x = FE_MAINMENU_COL_X, .scr_pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 5), .pos_x = FE_MAINMENU_COL_X, .pos_y = FE_ROW_Y(FE_MAINMENU_ROW_Y0, FE_MAINMENU_ROW_STEP, 5), .width = FE_MAINMENU_COL_W, .height = FE_MAINMENU_ROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuMultiplayer }, .maintain_call = frontend_main_menu_netservice_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_change_state, .ptover_event = frontend_over_button, .btype_value = 27, .scr_pos_x = FE_ROW_Y(FE_MAINMENU_COL_X, FE_MAINMENU_SUBROW_STEP, 0), .scr_pos_y = FE_MAINMENU_SUBROW_Y, .pos_x = FE_ROW_Y(FE_MAINMENU_COL_X, FE_MAINMENU_SUBROW_STEP, 0), .pos_y = FE_MAINMENU_SUBROW_Y, .width = FE_MAINMENU_SUBROW_W, .height = FE_MAINMENU_SUBROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuOptions_97 }, .maintain_call = frontend_main_menu_options_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_ldcampaign_change_state, .ptover_event = frontend_over_button, .btype_value = 18, .scr_pos_x = FE_ROW_Y(FE_MAINMENU_COL_X, FE_MAINMENU_SUBROW_STEP, 1), .scr_pos_y = FE_MAINMENU_SUBROW_Y, .pos_x = FE_ROW_Y(FE_MAINMENU_COL_X, FE_MAINMENU_SUBROW_STEP, 1), .pos_y = FE_MAINMENU_SUBROW_Y, .width = FE_MAINMENU_SUBROW_W, .height = FE_MAINMENU_SUBROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuHighScoreTable_104 }, .maintain_call = frontend_main_menu_highscores_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_change_state, .ptover_event = frontend_over_button, .btype_value = 9, .scr_pos_x = FE_ROW_Y(FE_MAINMENU_COL_X, FE_MAINMENU_SUBROW_STEP, 2), .scr_pos_y = FE_MAINMENU_SUBROW_Y, .pos_x = FE_ROW_Y(FE_MAINMENU_COL_X, FE_MAINMENU_SUBROW_STEP, 2), .pos_y = FE_MAINMENU_SUBROW_Y, .width = FE_MAINMENU_SUBROW_W, .height = FE_MAINMENU_SUBROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuQuit }, .maintain_call = frontend_main_menu_quit_maintain },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_y = 455, .pos_y = 455, .width = 371, .height = 46, .draw_call = frontend_draw_product_version, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = -1, .tooltip_stridx = GUIStr_Empty },
};

struct GuiButtonInit frontend_statistics_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MENU_TITLE, .scr_pos_x = 999, .scr_pos_y = 30, .pos_x = 999, .pos_y = 30, .width = 371, .height = 46, .draw_call = frontend_draw_large_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuStatistics } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 90, .pos_x = 999, .pos_y = 90, .width = 450, .height = 162, .draw_call = frontstats_draw_main_stats, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .scr_pos_y = 260, .pos_x = 999, .pos_y = 260, .width = 450, .height = 136, .draw_call = frontstats_draw_scrolling_stats, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontstats_leave, .ptover_event = frontend_over_button, .btype_value = 18, .scr_pos_x = 999, .scr_pos_y = 404, .pos_x = 999, .pos_y = 404, .width = 371, .height = 46, .draw_call = frontend_draw_large_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuOk } },
  { .gbtype = -1, .tooltip_stridx = GUIStr_Empty },
};

// Title and back button left-anchored to match Phase 1's main-menu column
// (docs/refactor/gui/02-menu-v2-mockup-gap-analysis.md Phase 4) -- table/
// scrollbar layout below is untouched (cosmetic repositioning only, no
// game data here to verify a full-cluster reflow doesn't misalign them).
struct GuiButtonInit frontend_high_score_score_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MENU_TITLE, .scr_pos_x = FE_MAINMENU_COL_X, .scr_pos_y = 30, .pos_x = FE_MAINMENU_COL_X, .pos_y = 30, .width = 300, .height = 46, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuHighScoreTable } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 145, .scr_pos_y = 72, .pos_x = 145, .pos_y = 72, .width = 220, .height = 26, .draw_call = frontend_draw_highscores_scroll_box_tab, .tooltip_stridx = GUIStr_Empty, .content = { 28 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 120, .scr_pos_y = 73, .pos_x = 120, .pos_y = 73, .width = 400, .height = 26, .draw_call = frontend_draw_high_scores_mappack, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuLevels } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 80, .scr_pos_y = 97, .pos_x = 80, .pos_y = 97, .width = 450, .height = 286, .draw_call = frontend_draw_high_score_table, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = highscore_scroll_up, .ptover_event = frontend_over_button, .scr_pos_x = 530, .scr_pos_y = 96, .pos_x = 530, .pos_y = 96, .width = 26, .height = 14, .draw_call = frontend_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 17 }, .maintain_call = frontend_highscore_scroll_up_maintain },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = highscore_scroll_down, .ptover_event = frontend_over_button, .scr_pos_x = 530, .scr_pos_y = 374, .pos_x = 530, .pos_y = 374, .width = 26, .height = 14, .draw_call = frontend_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 18 }, .maintain_call = frontend_highscore_scroll_down_maintain },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = highscore_scroll, .scr_pos_x = 533, .scr_pos_y = 112, .pos_x = 533, .pos_y = 112, .width = 20, .height = 260, .draw_call = frontend_draw_highscores_scroll_tab, .tooltip_stridx = GUIStr_Empty, .content = { 40 }, .maintain_call = frontend_highscore_scroll_tab_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_quit_high_score_table, .ptover_event = frontend_over_button, .btype_value = 3, .scr_pos_x = FE_MAINMENU_COL_X, .scr_pos_y = 404, .pos_x = FE_MAINMENU_COL_X, .pos_y = 404, .width = 180, .height = FE_MAINMENU_ROW_H, .draw_call = frontend_draw_button_icon, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuOk }, .maintain_call = frontend_maintain_high_score_ok_button },
  { .gbtype = -1, .tooltip_stridx = GUIStr_Empty },
};

struct GuiButtonInit frontend_error_box_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .pos_x = 999, .width = 450, .height = 92, .draw_call = frontend_draw_error_text_box, .tooltip_stridx = GUIStr_Empty, .content = { .str = gui_message_text }, .maintain_call = frontend_maintain_error_text_box },
  { .gbtype = -1, .tooltip_stridx = GUIStr_Empty },
};
#pragma GCC diagnostic pop


struct GuiMenu frontend_main_menu =
 { GMnu_FEMAIN,             0, 1, frontend_main_menu_buttons, POS_SCRCTR,POS_SCRCTR, 640, 480, NULL, 0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu frontend_statistics_menu =
 { GMnu_FESTATISTICS,       0, 1, frontend_statistics_buttons,POS_SCRCTR,POS_SCRCTR, 640, 480, NULL, 0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu frontend_high_score_table_menu =
 { GMnu_FEHIGH_SCORE_TABLE, 0, 1, frontend_high_score_score_buttons,POS_SCRCTR,POS_SCRCTR, 640, 480, NULL, 0, NULL,NULL,                  0, 0, 0,};
struct GuiMenu frontend_error_box = // Error box has no background defined - the buttons drawing adds it
 { GMnu_FEERROR_BOX,        0, 1, frontend_error_box_buttons,POS_GAMECTR,POS_GAMECTR, 450,  92, NULL,                        0, NULL,    NULL,                    0, 1, 0,};

// Note: update size in .h file when changing this array.
struct GuiMenu *menu_list[] = {
    NULL,
    &main_menu,
    &room_menu,
    &spell_menu,
    &trap_menu,
    &creature_menu,
    &event_menu,
    &query_menu,
    &options_menu,
    &instance_menu,
    &quit_menu,//10
    &load_menu,
    &save_menu,
    &video_menu,
    &sound_menu,
    &error_box,
    &text_info_menu,
    &hold_audience_menu,
    &frontend_main_menu,
    &frontend_load_menu,
    &frontend_net_service_menu,//20
    &frontend_net_session_menu,
    &frontend_net_start_menu,
    NULL, // Modem
    NULL, // Serial
    &frontend_statistics_menu,
    &frontend_high_score_table_menu,
    &dungeon_special_menu,
    &resurrect_creature_menu,
    &transfer_creature_menu,
    &armageddon_menu,//30
    &creature_query_menu1,
    &creature_query_menu3,
    &creature_query_menu4,
    &battle_menu,
    &creature_query_menu2,
    &frontend_define_keys_menu,
    &autopilot_menu,
    &spell_lost_menu,
    &frontend_option_menu,
    &frontend_select_level_menu,//40
    &frontend_select_campaign_menu,
    &frontend_error_box,
    &frontend_add_session_box,
    &frontend_select_mappack_menu,
    &message_box,
    &spell_menu2,
    &room_menu2,
    &trap_menu2,
    &frontend_select_mp_mappack_menu,
    NULL,
};

/** Array used for mapping buttons to text messages.
 *  Index in this array is accepted as value of button 'content' property.
 *  If adding entries here, you should also update FRONTEND_BUTTON_INFO_COUNT.
 */
struct FrontEndButtonData frontend_button_info[FRONTEND_BUTTON_INFO_COUNT] = {
    [0] = { 0, 0 },
    [FEBtn_MnuMainMenu] = { GUIStr_MnuMainMenu, 0 },
    [FEBtn_MnuStartNewGame] = { GUIStr_MnuStartNewGame, 1 },
    [FEBtn_MnuLoadGame] = { GUIStr_MnuLoadGame, 1 },
    [FEBtn_MnuMultiplayer] = { GUIStr_MnuMultiplayer, 1 },
    [FEBtn_MnuQuit] = { GUIStr_MnuQuit, 1 },
    [FEBtn_MnuReturnToMain] = { GUIStr_MnuReturnToMain, 1 },
    [FEBtn_MnuLoadGame_7] = { GUIStr_MnuLoadGame, 0 },
    [FEBtn_MnuContinueGame] = { GUIStr_MnuContinueGame, 1 },
    [FEBtn_MnuPlayIntro] = { GUIStr_MnuPlayIntro, 1 },
    [FEBtn_NetServiceMenu] = { GUIStr_NetServiceMenu, 0 },
    [FEBtn_NetSessionMenu] = { GUIStr_NetSessionMenu, 0 },
    [FEBtn_MnuOnlineLobbies] = { GUIStr_MnuOnlineLobbies, 0 },
    [FEBtn_NetJoinGame] = { GUIStr_NetJoinGame, 1 },
    [FEBtn_NetCreateGame] = { GUIStr_NetCreateGame, 1 },
    [FEBtn_NetStartGame] = { GUIStr_NetStartGame, 1 },
    [FEBtn_MnuCancel] = { GUIStr_MnuCancel, 1 },
    [17] = { GUIStr_Empty, 1 },
    [18] = { GUIStr_Empty, 1 },
    [FEBtn_NetName] = { GUIStr_NetName, 1 },
    [20] = { GUIStr_Empty, 1 },
    [21] = { GUIStr_Empty, 1 },
    [FEBtn_MnuLevel] = { GUIStr_MnuLevel, 1 },
    [23] = { GUIStr_Empty, 1 },
    [24] = { GUIStr_Empty, 1 },
    [25] = { GUIStr_Empty, 1 },
    [26] = { GUIStr_Empty, 1 },
    [27] = { GUIStr_Empty, 1 },
    [28] = { GUIStr_Empty, 1 },
    [FEBtn_NetSessions] = { GUIStr_NetSessions, 2 },
    [FEBtn_MnuGames] = { GUIStr_MnuGames, 2 },
    [FEBtn_MnuPlayers] = { GUIStr_MnuPlayers, 2 },
    [FEBtn_MnuLevels] = { GUIStr_MnuLevels, 2 },
    [FEBtn_NetServices] = { GUIStr_NetServices, 2 },
    [FEBtn_NetMessages] = { GUIStr_NetMessages, 2 },
    [35] = { GUIStr_Empty, 1 },
    [36] = { GUIStr_Empty, 1 },
    [37] = { GUIStr_Empty, 1 },
    [38] = { GUIStr_Empty, 1 },
    [39] = { GUIStr_Empty, 1 },
    [40] = { GUIStr_Empty, 1 },
    [41] = { GUIStr_Empty, 1 },
    [42] = { GUIStr_Empty, 1 },
    [43] = { GUIStr_Empty, 1 },
    [44] = { GUIStr_Empty, 1 },
    [45] = { GUIStr_Empty, 1 },
    [46] = { GUIStr_Empty, 1 },
    [47] = { GUIStr_Empty, 1 },
    [48] = { GUIStr_Empty, 1 },
    [49] = { GUIStr_Empty, 1 },
    [50] = { GUIStr_Empty, 1 },
    [51] = { GUIStr_Empty, 1 },
    [52] = { GUIStr_Empty, 1 },
    [FEBtn_NetModemMenu] = { GUIStr_NetModemMenu, 0 },
    [FEBtn_NetSerialMenu] = { GUIStr_NetSerialMenu, 0 },
    [FEBtn_NetComPort] = { GUIStr_NetComPort, 2 },
    [FEBtn_NetSpeed] = { GUIStr_NetSpeed, 2 },
    [57] = { GUIStr_Empty, 1 },
    [58] = { GUIStr_Empty, 1 },
    [59] = { GUIStr_Empty, 1 },
    [60] = { GUIStr_Empty, 1 },
    [FEBtn_NetIrq] = { GUIStr_NetIrq, 1 },
    [62] = { GUIStr_Empty, 1 },
    [63] = { GUIStr_Empty, 1 },
    [64] = { GUIStr_Empty, 1 },
    [65] = { GUIStr_Empty, 1 },
    [FEBtn_NetInit] = { GUIStr_NetInit, 1 },
    [FEBtn_NetHangup] = { GUIStr_NetHangup, 1 },
    [FEBtn_NetDial] = { GUIStr_NetDial, 1 },
    [FEBtn_NetAnswer] = { GUIStr_NetAnswer, 1 },
    [70] = { GUIStr_Empty, 1 },
    [FEBtn_NetPhoneNumber] = { GUIStr_NetPhoneNumber, 1 },
    [FEBtn_NetContinue] = { GUIStr_NetContinue, 1 },
    [FEBtn_NetContinue_73] = { GUIStr_NetContinue, 1 },
    [74] = { GUIStr_Empty, 1 },
    [75] = { GUIStr_Empty, 1 },
    [76] = { GUIStr_Empty, 1 },
    [77] = { GUIStr_Empty, 1 },
    [78] = { GUIStr_Empty, 1 },
    [79] = { GUIStr_Empty, 1 },
    [80] = { GUIStr_Empty, 1 },
    [81] = { GUIStr_Empty, 1 },
    [FEBtn_Credits] = { GUIStr_Credits, 1 },
    [FEBtn_MnuOk] = { GUIStr_MnuOk, 1 },
    [FEBtn_MnuStatistics] = { GUIStr_MnuStatistics, 0 },
    [FEBtn_MnuHighScoreTable] = { GUIStr_MnuHighScoreTable, 0 },
    [FEBtn_TeamChooseGame] = { GUIStr_TeamChooseGame, 0 },
    [FEBtn_TeamGameType] = { GUIStr_TeamGameType, 2 },
    [FEBtn_NetStart] = { GUIStr_NetStart, 1 },
    [89] = { GUIStr_Empty, 1 },
    [90] = { GUIStr_Empty, 1 },
    [91] = { GUIStr_Empty, 1 },
    [FEBtn_DefineKeys] = { GUIStr_DefineKeys, 0 },
    [93] = { GUIStr_Empty, 1 },
    [94] = { GUIStr_Empty, 1 },
    [FEBtn_DefineKeys_95] = { GUIStr_DefineKeys, 1 },
    [FEBtn_MnuOptions] = { GUIStr_MnuOptions, 0 },
    [FEBtn_MnuOptions_97] = { GUIStr_MnuOptions, 1 },
    [FEBtn_MnuRetToOptions] = { GUIStr_MnuRetToOptions, 1 },
    [FEBtn_MnuSoundOptions] = { GUIStr_MnuSoundOptions, 1 },
    [FEBtn_MouseOptions] = { GUIStr_MouseOptions, 1 },
    [FEBtn_Sensitivity] = { GUIStr_Sensitivity, 1 },
    [FEBtn_MnuInvertMouse] = { GUIStr_MnuInvertMouse, 1 },
    [FEBtn_MnuComputer] = { GUIStr_MnuComputer, 1 },
    [FEBtn_MnuHighScoreTable_104] = { GUIStr_MnuHighScoreTable, 1 },
    [105] = { GUIStr_Empty, 0 },
    [FEBtn_MnuFreePlayLevels] = { GUIStr_MnuFreePlayLevels, 1 },
    [FEBtn_MnuFreePlayLevels_107] = { GUIStr_MnuFreePlayLevels, 0 },
    [FEBtn_MnuLandSelection] = { GUIStr_MnuLandSelection, 0 },
    [FEBtn_MnuCampaigns] = { GUIStr_MnuCampaigns, 2 },
    [FEBtn_MnuAddComputer] = { GUIStr_MnuAddComputer, 1 },
    [FEBtn_MnuReturnToFreePlay] = { GUIStr_MnuReturnToFreePlay, 1 },
    [FEBtn_MnuMapPacks] = { GUIStr_MnuMapPacks, 2 },
    [FEBtn_MnuMpMapPacks] = { GUIStr_MnuMpMapPacks, 2 },
    [FEBtn_MnuReturnToLobby] = { GUIStr_MnuReturnToLobby, 1 },
    [FEBtn_MnuEnterLand] = { GUIStr_MnuEnterLand, 1 },
    [FEBtn_MnuPlayLevel] = { GUIStr_MnuPlayLevel, 1 },
    // Reuses the net-service list's own "Play one player" string -- no new
    // translation needed, and it's the same feature the button used to be
    // labelled with when it lived in that list.
    [FEBtn_MnuSkirmish] = { GUIStr_NetServiceSkirmish, 1 },
};

// bttn_sprite, tooltip_stridx, msg_stridx, lifespan_turns, turns_between_events, replace_event_kind_button;
struct EventTypeInfo event_button_info[] = {
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_Empty,                       GUIStr_Empty,                      1,   1, EvKind_Nothing},
  {GPS_message_rpanel_msg_exclam2_act,    GUIStr_EventDnHeartAttackedDesc,    GUIStr_EventHeartAttacked,       300, 250, EvKind_Nothing},
  {GPS_message_rpanel_msg_battle_act,     GUIStr_EventFightDesc,              GUIStr_EventFight,                -1,   0, EvKind_FriendlyFight},
  {GPS_message_rpanel_msg_questn_act,     GUIStr_EventObjective,              GUIStr_Empty,                     -1,   0, EvKind_Objective},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventBreachDesc,             GUIStr_EventBreach,              300,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_room_act,       GUIStr_EventNewRoomResrchDesc,      GUIStr_EventNewRoomResearched,  1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_creatr_act,     GUIStr_EventNewCreatureDesc,        GUIStr_EventNewCreature,        1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_spell_act,      GUIStr_EventNewSpellResrchDesc,     GUIStr_EventNewSpellResearched, 1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_manufct_act,    GUIStr_EventNewTrapDesc,            GUIStr_EventNewTrap,            1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_manufct_act,    GUIStr_EventNewDoorDesc,            GUIStr_EventNewDoor,            1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventCreatrScavngDesc,       GUIStr_EventScavengingDetected, 1200,   0, EvKind_Nothing}, // EvKind_CreatrScavenged
  {GPS_message_rpanel_msg_inforb_act,     GUIStr_EventTreasrRoomFullDesc,     GUIStr_EventTreasureRoomFull,   1200, 500, EvKind_Nothing},
  {GPS_message_rpanel_msg_payday_act,     GUIStr_EventCreaturePaydayDesc,     GUIStr_EventCreaturePayday,     1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_inforb_act,     GUIStr_EventAreaDiscoveredDesc,     GUIStr_EventAreaDiscovered,     1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_inforb_act,     GUIStr_EventSpellPickedUpDesc,      GUIStr_EventNewSpellPickedUp,   1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_inforb_act,     GUIStr_EventRoomTakenOverDesc,      GUIStr_EventNewRoomTakenOver,   1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventCreatrAnnoyedDesc,      GUIStr_EventCreatureAnnoyed,    1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventNoMoreLivingSetDesc,    GUIStr_EventNoMoreLivingSpace,  1200, 500, EvKind_Nothing},
  {GPS_message_rpanel_msg_alarm_act,      GUIStr_EventAlarmTriggeredDesc,     GUIStr_EventAlarmTriggered,      300, 200, EvKind_Nothing},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventRoomUnderAttackDesc,    GUIStr_EventRoomUnderAttack,     300, 250, EvKind_Nothing},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventNeedTreasrRoomDesc,     GUIStr_EventTreasureRoomNeeded,  300, 500, EvKind_Nothing}, // EvKind_NeedTreasureRoom
  {GPS_message_rpanel_msg_inforg_act,     GUIStr_EventInformationDesc,        GUIStr_Empty,                   1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventRoomLostDesc,           GUIStr_EventRoomLost,           1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventCreaturesHungryDesc,    GUIStr_EventCreaturesHungry,     300, 500, EvKind_Nothing},
  {GPS_message_rpanel_msg_inforb_act,     GUIStr_EventTrapCrateFoundDesc,     GUIStr_EventTrapCrateFound,      300,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_inforb_act,     GUIStr_EventDoorCrateFoundDesc,     GUIStr_EventDoorCrateFound,      300,   0, EvKind_Nothing}, // EvKind_DoorCrateFound
  {GPS_message_rpanel_msg_bonusbox_act,   GUIStr_EventDnSpecialFoundDesc,     GUIStr_EventDnSpecialFound,      300,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_inforg_act,     GUIStr_EventInformationDesc,        GUIStr_Empty,                   1200,   0, EvKind_Nothing},
  {GPS_message_rpanel_msg_battle_act,     GUIStr_EventFightDesc,              GUIStr_EventFight,                -1,   0, EvKind_EnemyFight},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventWorkRoomUnreachblDesc,  GUIStr_EventWorkRoomUnreachbl,  1200, 500, EvKind_Nothing}, // EvKind_WorkRoomUnreachable
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventStorgRoomUnreachblDesc, GUIStr_EventStorgRoomUnreachbl, 1200, 500, EvKind_Nothing}, // EvKind_StorageRoomUnreachable
  {0,                                     GUIStr_Empty,                       GUIStr_Empty,                     50,  10, EvKind_Nothing}, // EvKind_PrisonerStarving
  {0,                                     GUIStr_Empty,                       GUIStr_Empty,                   1200,  50, EvKind_Nothing}, // EvKind_TorturedHurt
  {0,                                     GUIStr_Empty,                       GUIStr_Empty,                   1200,  50, EvKind_Nothing}, // EvKind_EnemyDoor
  {GPS_message_rpanel_msg_inforb_act,     GUIStr_EventSecretDoorDiscovDesc,   GUIStr_EventSecretDoorDiscovered,300, 200, EvKind_Nothing},
  {GPS_message_rpanel_msg_exclam_act,     GUIStr_EventSecretDoorSpottedDesc,  GUIStr_EventSecretDoorSpotted,   300, 200, EvKind_Nothing},
};

const uint64_t alliance_grid[4][4] = {
  {0x00, 0x01, 0x02, 0x04,},
  {0x01, 0x00, 0x08, 0x10,},
  {0x02, 0x08, 0x00, 0x20,},
  {0x04, 0x10, 0x20, 0x00,},
};

#if (BFDEBUG_LEVEL > 0)
// Declarations for font testing screen (debug version only)
// testfont/testfont_palette moved to kfx_render's vidmode.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).
int64_t num_chars_in_font = 128;
#endif

int64_t status_panel_width = 140;
// struct MsgBoxInfo MsgBox;

char info_tag;
char room_tag;
char spell_tag;
char trap_tag;
char creature_tag;
char input_string[8][SAVE_TEXTNAME_LEN + 1];
char gui_error_text[256];
int64_t net_number_of_services;
int64_t net_comport_index_active;
int64_t net_speed_index_active;
int64_t net_number_of_players;
int64_t net_number_of_enum_players;
int64_t net_level_highlighted;
struct NetMessage net_message[NET_MESSAGES_COUNT];
int64_t net_number_of_messages;
struct GuiButton active_buttons[ACTIVE_BUTTONS_COUNT];
int64_t frontend_mouse_over_button_start_time;
int64_t old_menu_mouse_x;
int64_t old_menu_mouse_y;
unsigned char menu_ids[3];
// new_objective moved to kfx_sim's kfx_sim_state.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).
int64_t frontend_menu_state;
int64_t skip_high_score_screen;
int64_t load_game_scroll_offset;
unsigned char video_gamma_correction;

// docs/refactor/editor/01-entry-and-editor-session.md §2 -- see frontend.h's
// own comment on why these are plain globals, not KfxFrontendState fields.
LevelNumber editor_pending_lvnum = 0;
TbBool editor_pending_is_new = false;
MapSlabCoord editor_pending_new_map_w = 85;
MapSlabCoord editor_pending_new_map_h = 85;
int64_t editor_pending_new_map_texture = 0;
TbBool editor_pending_relaunch = false;
TbBool editor_pending_playtest = false;
TbBool editor_playtest_running = false;
TbBool content_tool_play_running = false;
int64_t content_tool_return_tool = -1;
int64_t content_tool_reopen_tool = -1;

void frontend_request_editor_relaunch(LevelNumber lvnum, TbBool is_new,
    MapSlabCoord new_map_w, MapSlabCoord new_map_h, int64_t new_map_texture)
{
    editor_pending_lvnum = lvnum;
    editor_pending_is_new = is_new;
    editor_pending_new_map_w = new_map_w;
    editor_pending_new_map_h = new_map_h;
    editor_pending_new_map_texture = new_map_texture;
    editor_pending_relaunch = true;
}

// docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md --
// set_selected_level_number() is the same primitive the `-level`
// command-line argument uses (main.cpp) to steer which level a normal
// FeSt_START_KPRLEVEL game-start loads (main_game.c's get_selected_level_number()
// callers).
namespace {
uint8_t s_playtest_pack = 0;
char s_playtest_campaign[DISKPATH_SIZE] = "";
}

void frontend_request_editor_playtest(LevelNumber lvnum, uint8_t pack, const char *campaign_fname)
{
    set_selected_level_number(lvnum);
    s_playtest_pack = pack;
    snprintf(s_playtest_campaign, sizeof(s_playtest_campaign), "%s", campaign_fname != nullptr ? campaign_fname : "");
    editor_pending_playtest = true;
}

// *** SPRITES ***
// font_sprites/frontend_font/button_sprites/winfont moved to kfx_render's
// vidmode.h (stage 13.3, docs/refactor/stage-13-enforce-and-document.md).

enum TbFontRole resolve_font_role(const struct TbSpriteSheet *font)
{
    if (font == frontend_font[0]) return FontRole_Frontend0;
    if (font == frontend_font[1]) return FontRole_Frontend1;
    if (font == frontend_font[2]) return FontRole_Frontend2;
    if (font == frontend_font[3]) return FontRole_Frontend3;
    if (font == winfont) return FontRole_Win;
    if (font == font_sprites) return FontRole_Sprites;
    if (font == frontstory_font) return FontRole_Story;
    return FontRole_Unknown;
}
uint64_t playing_bad_descriptive_speech;
uint64_t playing_good_descriptive_speech;
int64_t scrolling_index;
double scrolling_offset;
char frontend_alliances;
char busy_doing_gui;
int64_t gui_last_left_button_pressed_id;
int64_t gui_last_right_button_pressed_id;
int64_t fe_computer_players;
int64_t fe_spectate_campaign;
int64_t fe_external_campaign;
int64_t old_mouse_over_button;
int64_t frontend_mouse_over_button;

/******************************************************************************/
int64_t menu_is_active(int64_t idx)
{
  return (menu_id_to_number(idx) >= 0);
}

TbBool a_menu_window_is_active(void)
{
  if (no_of_active_menus <= 0)
    return false;
  int64_t i;
  int64_t k;
  for (i=0; i<no_of_active_menus; i++)
  {
      k = menu_stack[i];
      if (!is_toggleable_menu(k))
        return true;
  }
  return false;
}

int64_t frontend_font_char_width(int64_t fnt_idx,char c)
{
    int64_t i;
    i = (int64_t)c - 31;
    if (i >= 0) {
        return get_sprite(frontend_font[fnt_idx], i)->SWidth;
    }
    return 0;
}

int64_t frontend_font_string_width(int64_t fnt_idx, const char *str)
{
    LbTextSetFont(frontend_font[fnt_idx]);
    return LbTextStringWidth(str);
}



void add_message(int64_t plyr_idx, char *msg)
{
    struct NetMessage *nmsg;
    int64_t i;
    int64_t k;
    i = net_number_of_messages;
    if (i >= NET_MESSAGES_COUNT)
    {
      for (k=0; k < (NET_MESSAGES_COUNT-1); k++)
      {
        memcpy(&net_message[k], &net_message[k+1], sizeof(struct NetMessage));
      }
      i = NET_MESSAGES_COUNT-1;
    }
    nmsg = &net_message[i];
    nmsg->plyr_idx = plyr_idx;
    nmsg->connection_id = net_user_info[plyr_idx].connection_id;
    snprintf(nmsg->text, NET_MESSAGE_LEN, "%s", msg);
    i++;
    net_number_of_messages = i;
    if (net_message_list.scroll_offset+4 < i)
      net_message_list.scroll_offset = i-4;
}

/**
 * Makes error box with message from GUI strings collection.
 *
 * @param msg_idx
 */
void create_error_box(TextStringId msg_idx)
{
    if (!kfx_net_state.packet_load_enable)
    {
        //change the length into  when gui_error_text will not be exported
        snprintf(gui_error_text, sizeof(gui_error_text), "%s", get_string(msg_idx));
        turn_on_menu(GMnu_ERROR_BOX);
    }
}


void create_message_box(const char *title, const char *line1, const char *line2, const char *line3, const char* line4, const char* line5)
{
    memset(&MsgBox,0, sizeof(MsgBox));
    snprintf(MsgBox.title, sizeof(MsgBox.title), "%s", title);
    snprintf(MsgBox.line1, sizeof(MsgBox.line1), "%s", line1);
    snprintf(MsgBox.line2, sizeof(MsgBox.line2), "%s", line2);
    snprintf(MsgBox.line3, sizeof(MsgBox.line3), "%s", line3);
    snprintf(MsgBox.line4, sizeof(MsgBox.line4), "%s", line4);
    snprintf(MsgBox.line5, sizeof(MsgBox.line5), "%s", line5);
    turn_on_menu(GMnu_MSG_BOX);
}

int64_t game_is_busy_doing_gui(void)
{
    struct UserState *ustate = get_local_user_state();
    if (battle_creature_over > 0) {
        return true;
    }
    if (ustate->one_click_lock_cursor) {
        return false;
    }
    if (!busy_doing_gui) {
        return false;
    }
    return true;
}

// force finish text entry
// (if text field is empty, reverts as though esc was pressed.)
void finish_button_area_input(void)
{
    if (input_button == NULL)
        return;
    TbKeyCode prev_key = lbInkey;
    lbInkey = KC_RETURN;
    get_button_area_input(input_button, input_button->id_num);
    if (input_button != NULL)
    {
        lbInkey = KC_ESCAPE;
        get_button_area_input(input_button, input_button->id_num);
    }
    lbInkey = prev_key;
}

TbBool get_button_area_input(struct GuiButton *gbtn, int64_t modifiers)
{
    if (input_button == NULL)
    {
        if (LbIsTextInputActive())
            LbStopTextInput();
        return false;
    }

    char *str;
    TbKeyCode key;
    str = gbtn->content.str;
    key = lbInkey;
    if (key == KC_RETURN)
    {
        if ((str[0] != '\0') || (modifiers == -3))
        {
            gbtn->button_state_left_pressed = 0;
            (gbtn->click_event)(gbtn);
            input_button = 0;
            LbStopTextInput();
            if ((gbtn->flags & LbBtnF_Clickable) != 0)
            {
                struct GuiMenu *gmnu;
                gmnu = get_active_menu(gbtn->gmenu_idx);
                gmnu->visual_state = 3;
                remove_from_menu_stack(gmnu->ident);
            }
        }
    } else
    if (key == KC_ESCAPE)
    { // Stop the input, revert the string to what it was before
        snprintf(str, gbtn->maxval, "%s", backup_input_field);
        input_button = 0;
        input_field_pos = 0;
        LbStopTextInput();
    } else
    if (key == KC_BACK)
    { // Delete the last char
        if (input_field_pos > 0) {
            input_field_pos--;
            LbLocTextStringDelete(str, input_field_pos, 1);
        }
    } else
    if (key == KC_DELETE)
    { // Delete the next char
        if (input_field_pos < LbLocTextStringLength(str)) {
            LbLocTextStringDelete(str, input_field_pos, 1);
        }
    } else
    if ((key == KC_HOME) || (key == KC_PGUP))
    { // move to first char
        input_field_pos = 0;
    } else
    if ((key == KC_END) || (key == KC_PGDOWN))
    { // move to last char
        input_field_pos = LbLocTextStringLength(str);
    } else
    if (key == KC_LEFT)
    { // move one char left
        if (input_field_pos > 0)
            input_field_pos--;
    } else
    if (key == KC_RIGHT)
    { // move one char right
        if (input_field_pos < LbLocTextStringLength(str))
            input_field_pos++;
    } else
    if (LbLocTextStringSize(str) < abs(gbtn->maxval))
    {
        char insert_text[64] = "";
        if (add_input_text_to_message(insert_text, sizeof(insert_text), winfont, gbtn->width * pixel_size))
        {
            if (insert_text[0] != '\0')
            {
                if (LbLocTextStringInsert(str, insert_text, input_field_pos, gbtn->maxval) != NULL) {
                    input_field_pos += LbLocTextStringLength(insert_text);
                }
            }
        }
    }
    clear_key_pressed(key);
    return true;
}

void maintain_loadsave(struct GuiButton *gbtn)
{
    if (!network_is_active())
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
}

void maintain_zoom_to_event(struct GuiButton *gbtn)
{
    struct Event *event;
    if (my_visible_event_idx)
    {
      event = &(kfx_sim_state.event[my_visible_event_idx]);
      if ((event->mappos_x != 0) || (event->mappos_y != 0))
      {
        gbtn->flags |= LbBtnF_Enabled;
        return;
      }
    }
    gbtn->flags &= ~LbBtnF_Enabled;
}

void maintain_scroll_up(struct GuiButton *gbtn)
{
    struct TextScrollWindow * scrollwnd;
    scrollwnd = (struct TextScrollWindow *)gbtn->content.ptr;
    gbtn->flags ^= (gbtn->flags ^ LbBtnF_Enabled * (scrollwnd->start_y < 0)) & LbBtnF_Enabled;
    if (!check_current_gui_layer(GuiLayer_OneClick))
    {
        if (wheel_scrolled_up && (is_game_key_pressed(Gkey_RotateMod, false, true)))
        {
            scrollwnd->action = 1;
        }
    }
}

void maintain_scroll_down(struct GuiButton *gbtn)
{
    struct TextScrollWindow * scrollwnd;
    scrollwnd = (struct TextScrollWindow *)gbtn->content.ptr;
    gbtn->flags ^= (gbtn->flags ^ LbBtnF_Enabled
        * (scrollwnd->window_height - scrollwnd->text_height + 2 < scrollwnd->start_y)) & LbBtnF_Enabled;
    if (!check_current_gui_layer(GuiLayer_OneClick))
    {
        if (wheel_scrolled_down && (is_game_key_pressed(Gkey_RotateMod, false, true)))
        {
            scrollwnd->action = 2;
        }
    }
}

// Sizes main-menu buttons to their caption text instead of a fixed guess --
// added after visually confirming the earlier fixed 260/130px widths were
// both too wide for the narrow column and too narrow (overlapping) for the
// Options/High score/Quit row. Measured against the button's *enabled*
// (non-hover) font index so width doesn't jitter as the mouse moves over
// it; frontend_button_caption_font swaps to a different frontend_font[]
// entry on hover, but those are style/colour variants of the same glyph
// set, not a different-width font.
static int64_t frontend_menu_button_caption_width(uint64_t febtn_idx, int64_t units_per_px)
{
    int64_t fntidx = (febtn_idx < FRONTEND_BUTTON_INFO_COUNT) ? frontend_button_info[febtn_idx].font_index : 3;
    LbTextSetFont(frontend_font[fntidx]);
    int64_t text_idx = (febtn_idx < FRONTEND_BUTTON_INFO_COUNT) ? frontend_button_info[febtn_idx].capstr_idx : (int64_t)GUIStr_Empty;
    const char *text = get_string(text_idx);
    return LbTextStringWidthM(text, units_per_px);
}

// Minimum button width that fits febtn_idx's caption without clipping,
// using the same left/right inset frontend_draw_button_icon positions its
// text with -- then rounded up to what frontend_draw_button_chrome_
// flexible will actually render (frontend_button_chrome_fit_width), since
// the chrome only grows in whole-middle-tile steps. Skipping that
// quantization was the bug behind the first pass at this: gbtn->width was
// set to the raw (unrounded) target, which frequently rounded to the same
// rendered chrome as before, so the button never visibly resized, and
// sibling buttons positioned off that same unrounded number sat too close
// to (or on top of) the wider, rounded-up chrome that actually got drawn.
int64_t frontend_menu_button_natural_width(uint64_t febtn_idx, int64_t units_per_px)
{
    int64_t inset = 20 * units_per_px / 16;
    int64_t target = frontend_menu_button_caption_width(febtn_idx, units_per_px) + 2 * inset;
    return frontend_button_chrome_fit_width(GFS_hugebutton_a05l, units_per_px, target);
}

// Uniform width for the main column (Start New Game/Continue/Free Play/
// Skirmish/Load/Multiplayer): sized to the longest of the six captions so
// they stay a consistent column width instead of each hugging its own text.
static int64_t frontend_main_menu_column_width(struct GuiButton *gbtn)
{
    static const uint64_t captions[] = {
        FEBtn_MnuStartNewGame, FEBtn_MnuContinueGame, FEBtn_MnuFreePlayLevels,
        FEBtn_MnuSkirmish, FEBtn_MnuLoadGame, FEBtn_MnuMultiplayer,
    };
    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    int64_t max_w = 0;
    for (uint64_t i = 0; i < sizeof(captions)/sizeof(captions[0]); i++) {
        int64_t w = frontend_menu_button_natural_width(captions[i], units_per_px);
        if (w > max_w)
            max_w = w;
    }
    return max_w;
}

void frontend_main_menu_start_game_maintain(struct GuiButton *gbtn)
{
    gbtn->width = frontend_main_menu_column_width(gbtn);
}

void frontend_continue_game_maintain(struct GuiButton *gbtn)
{
    gbtn->width = frontend_main_menu_column_width(gbtn);
    if (kfx_frontend_state.continue_game_option_available != 0)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
}

void frontend_main_menu_load_game_maintain(struct GuiButton *gbtn)
{
    gbtn->width = frontend_main_menu_column_width(gbtn);
    if (number_of_saved_games > 0)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
}

void frontend_mappacks_maintain(struct GuiButton *gbtn)
{
    gbtn->width = frontend_main_menu_column_width(gbtn);
    if (mappacks_list.items_num > 0)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
}

void frontend_main_menu_netservice_maintain(struct GuiButton *gbtn)
{
    gbtn->width = frontend_main_menu_column_width(gbtn);
    gbtn->flags |= LbBtnF_Enabled;
}

void frontend_main_menu_skirmish_maintain(struct GuiButton *gbtn)
{
    gbtn->width = frontend_main_menu_column_width(gbtn);
    if (mp_mappacks_list.items_num > 0)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
}

// Options/High score table/Quit row: each sized to its own caption (they
// don't share a column), Options+High score left-anchored with a gap
// between them, Quit right-anchored on the opposite side of the menu.
#define FE_MAINMENU_SUBROW_GAP 24
#define FE_MAINMENU_MENU_W 640 // matches frontend_main_menu's declared GuiMenu width

void frontend_main_menu_options_maintain(struct GuiButton *gbtn)
{
    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    int64_t x = FE_MAINMENU_COL_X;
    gbtn->width = frontend_menu_button_natural_width(FEBtn_MnuOptions_97, units_per_px);
    gbtn->pos_x = x;
    gbtn->scr_pos_x = x;
    gbtn->flags |= LbBtnF_Enabled;
}

void frontend_main_menu_highscores_maintain(struct GuiButton *gbtn)
{
    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    int64_t x = FE_MAINMENU_COL_X + frontend_menu_button_natural_width(FEBtn_MnuOptions_97, units_per_px)
        + FE_MAINMENU_SUBROW_GAP * units_per_px / 16;
    gbtn->width = frontend_menu_button_natural_width(FEBtn_MnuHighScoreTable_104, units_per_px);
    gbtn->pos_x = x;
    gbtn->scr_pos_x = x;
    gbtn->flags |= LbBtnF_Enabled;
}

void frontend_main_menu_quit_maintain(struct GuiButton *gbtn)
{
    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    int64_t w = frontend_menu_button_natural_width(FEBtn_MnuQuit, units_per_px);
    int64_t x = (FE_MAINMENU_MENU_W - FE_MAINMENU_COL_X) - w;
    gbtn->width = w;
    gbtn->pos_x = x;
    gbtn->scr_pos_x = x;
}

TbBool frontend_is_player_allied(int64_t idx1, int64_t idx2)
{
    if (idx1 == idx2)
      return true;
    if ((idx1 < 0) || (idx1 >= PLAYER_GOOD))
      return false;
    if ((idx2 < 0) || (idx2 >= PLAYER_GOOD))
      return false;
    return ((frontend_alliances & alliance_grid[idx1][idx2]) != 0);
}

void frontend_set_alliance(int64_t idx1, int64_t idx2)
{
    if (frontend_is_player_allied(idx1, idx2))
      frontend_alliances &= ~alliance_grid[idx1][idx2];
    else
      frontend_alliances |= alliance_grid[idx1][idx2];
}

TbResult frontend_load_data(void)
{
    char *fname;
    TbResult ret;
    int64_t len;
    // TODO: There is no "frontend_unload_data", find a better spot for this
    free_spritesheet(&frontend_sprite);
    ret = Lb_SUCCESS;
    frontend_background = (unsigned char *)kfx_sim_state.map;
#ifdef SPRITE_FORMAT_V2
    fname = prepare_file_fmtpath(FGrp_LoData,"front-%" PRId64 ".raw",(int64_t)(64));
#else
    fname = prepare_file_path(FGrp_LoData,"front.raw");
#endif
    len = LbFileLoadAt(fname, frontend_background);
    if (len < 307200) {
        ret = Lb_FAIL;
    }
    if (len > sizeof(kfx_sim_state.map)) {
        WARNLOG("Reused memory area exceeded for frontend background.");
    }
    char dat_fname[2048];
    char tab_fname[2048];
#ifdef SPRITE_FORMAT_V2
    strcpy(dat_fname, prepare_file_fmtpath(FGrp_LoData,"frontbit-%" PRId64 ".dat",(int64_t)(64)));
    strcpy(tab_fname, prepare_file_fmtpath(FGrp_LoData,"frontbit-%" PRId64 ".tab",(int64_t)(64)));
#else
    strcpy(dat_fname, prepare_file_path(FGrp_LoData,"frontbit.dat"));
    strcpy(tab_fname, prepare_file_path(FGrp_LoData,"frontbit.tab"));
 #endif
    frontend_sprite = load_spritesheet(dat_fname, tab_fname);
    if (!frontend_sprite) {
        ERRORLOG("Cannot load frontend sprites.");
        return Lb_FAIL;
    }
    return ret;
}

void activate_room_build_mode(RoomKind rkind, TextStringId tooltip_id)
{
    struct PlayerInfo *player = get_my_player();
    set_players_packet_action(player, PckA_SetPlyrState, PSt_BuildRoom, rkind, 0, 0);
    struct RoomConfigStats *roomst;
    roomst = get_room_kind_stats(rkind);
    kfx_sim_state.chosen_room_kind = rkind;
    kfx_sim_state.chosen_room_spridx = roomst->bigsym_sprite_idx;
    kfx_sim_state.chosen_room_tooltip = tooltip_id;
}

int64_t player_state_to_packet(PlayerState work_state, PowerKind pwkind, TbBool already_in)
{
    switch (work_state)
    {
    case PSt_CallToArms:
        if (already_in)
            return PckA_PwrCTADis;
        else
            return PckA_SetPlyrState;
    case PSt_SightOfEvil:
        if (already_in)
            return PckA_PwrSOEDis;
        else
            return PckA_SetPlyrState;
    case PSt_CtrlDirect:
    case PSt_FreeCtrlDirect:
    case PSt_CreateDigger:
    case PSt_CastPowerOnSubtile:
    case PST_CastPowerOnTarget:
        return PckA_SetPlyrState;
    case PST_CastGenericLevelPower:
        return PckA_GenericLevelPower;
    case PSt_None:
        switch (pwkind)
        {
        case PwrK_OBEY:
            return PckA_UsePwrObey;
        case PwrK_HOLDAUDNC:
            return PckA_HoldAudience;
        case PwrK_ARMAGEDDON:
            return PckA_UsePwrArmageddon;
        default:
            break;
        }
        return PckA_None;
    default:
        return PckA_None;
    }
}

TbBool set_players_packet_change_spell(struct PlayerInfo *player,PowerKind pwkind)
{
    if (power_is_instinctive(kfx_sim_state.chosen_spell_type) && (kfx_sim_state.chosen_spell_type != 0))
        return false;
    const struct PowerConfigStats *powerst;
    powerst = get_power_model_stats(pwkind);
    TbBool already_in;
    already_in = (powerst->work_state != PSt_None) && (player->work_state == powerst->work_state);
    int64_t pcktype;
    pcktype = player_state_to_packet(powerst->work_state, pwkind, already_in);
    if (pcktype != PckA_None)
    {
        set_players_packet_action(player, pcktype, powerst->work_state, pwkind, 0, 0);
        if (!already_in) {
            play_non_3d_sample(powerst->select_sample_idx);
        }
    }
    return true;
}

TbBool is_special_power(PowerKind pwkind)
{
    return ((pwkind == PwrK_HOLDAUDNC) || (pwkind == PwrK_ARMAGEDDON));
}

/**
 * Sets a new chosen special spell (Armageddon or Hold Audience).
 */
void choose_special_spell(PowerKind pwkind, TextStringId tooltip_id)
{
    struct Dungeon *dungeon;
    const struct PowerConfigStats *powerst;

    if (!is_special_power(pwkind)) {
        WARNLOG("Bad power kind");
        return;
    }

    dungeon = get_players_num_dungeon(my_player_number);
    set_chosen_power(pwkind, tooltip_id);
    powerst = get_power_model_stats(pwkind);

    if (dungeon->total_money_owned >= powerst->cost[0]) {
        play_non_3d_sample_no_overlap(powerst->select_sample_idx); // Play the spell speech
        switch (pwkind)
        {
        case PwrK_ARMAGEDDON:
            turn_on_menu(GMnu_ARMAGEDDON);
            break;
        case PwrK_HOLDAUDNC:
            turn_on_menu(GMnu_HOLD_AUDIENCE);
            break;
        }
    }
}

/**
 * Sets a new chosen spell.
 * Fills packet with the previous spell disable action.
 */
void choose_spell(PowerKind pwkind, TextStringId tooltip_id)
{
    struct PlayerInfo *player;

    pwkind = pwkind % kfx_config_state.conf.magic_conf.power_types_count;

    if (is_special_power(pwkind)) {
        choose_special_spell(pwkind, tooltip_id);
        return;
    }

    player = get_my_player();

    // Disable previous spell
    if (!set_players_packet_change_spell(player, pwkind)) {
        WARNLOG("Inconsistency when switching spell %" PRId64 " to %" PRId64,
            (int64_t)kfx_sim_state.chosen_spell_type, (int64_t)pwkind);
    }

    set_chosen_power(pwkind, tooltip_id);
}

void frontend_draw_scroll_tab(struct GuiButton *gbtn, int64_t scroll_offset, int64_t first_elem, int64_t last_elem)
{
    const struct TbSprite *spr;
    int64_t i;
    int64_t k;
    int64_t n;
    int64_t units_per_px;
    units_per_px = simple_frontend_sprite_width_units_per_px(gbtn, GFS_slider_indicator_std, 100);
    spr = get_frontend_sprite(GFS_slider_indicator_std);
    i = last_elem - first_elem;
    k = gbtn->height - spr->SHeight * units_per_px / 16;
    if (i <= 1)
        n = 0;
    else
        n = (scroll_offset * (k << 8) / (i - 1)) >> 8;
    LbSpriteDrawResized(gbtn->scr_pos_x, n+gbtn->scr_pos_y, units_per_px, spr);
}

int64_t frontend_scroll_tab_to_offset(struct GuiButton *gbtn, int64_t scr_pos, int64_t first_elem, int64_t last_elem)
{
    int64_t elem_num;
    elem_num = last_elem - first_elem;
    if (elem_num < 1) {
        return 0;
    }
    int64_t bar_pos;
    bar_pos = scr_pos - gbtn->scr_pos_y;
    if (bar_pos < 0) bar_pos = 0;
    if (bar_pos >= gbtn->height) bar_pos = gbtn->height-1;
    int64_t scroll_offset;
    scroll_offset = bar_pos * elem_num / gbtn->height;
    return scroll_offset;
}

void gui_quit_game(struct GuiButton *gbtn)
{
    struct PlayerInfo *player = get_my_player();
    set_players_packet_action(player, PckA_QuitToMainMenu, 0, 0, 0, 0);
}

void draw_slider64k(int64_t scr_x, int64_t scr_y, int64_t units_per_px, int64_t width)
{
    draw_bar64k(scr_x, scr_y, units_per_px, width);
    // Inner size
    ScreenCoord x = scr_x;
    ScreenCoord y = scr_y;
    TbBool low_res = (MyScreenHeight < 400);
    if (low_res)
    {
        x -= 16;
        y -= 5;
    }
    int64_t base_x = x + 32*units_per_px/16;
    int64_t base_y = y + 10*units_per_px/16;
    int64_t base_w = width - 64*units_per_px/16;
    int64_t end_x = base_x + base_w - 64*units_per_px/16;
    if (low_res)
    {
        end_x += 32;
    }
    int64_t cur_x = base_x;
    int64_t cur_y = base_y;
    // int end_x = base_x + base_w - 64*units_per_px/16;
    const struct TbSprite *spr = get_button_sprite(GBS_borders_bar_std_l);
    LbSpriteDrawResized(cur_x, cur_y, units_per_px, spr);
    cur_x += spr->SWidth*units_per_px/16;
    spr = get_button_sprite(GBS_borders_bar_std_c);
    while (cur_x < end_x)
    {
        LbSpriteDrawResized(cur_x, cur_y, units_per_px, spr);
        cur_x += spr->SWidth*units_per_px/16;
    }
    cur_x = end_x;
    LbSpriteDrawResized(cur_x/pixel_size, cur_y/pixel_size, units_per_px, spr);
    cur_x += spr->SWidth*units_per_px/16;
    spr = get_button_sprite(GBS_borders_bar_std_r);
    LbSpriteDrawResized(cur_x/pixel_size, cur_y/pixel_size, units_per_px, spr);
}

void gui_area_slider(struct GuiButton *gbtn)
{
    if ((gbtn->flags & LbBtnF_Enabled) == 0) {
        return;
    }
    int64_t units_per_px = (gbtn->height*16 + 30/2) / 30;
    int64_t bs_units_per_px = simple_button_sprite_height_units_per_px(gbtn, GBS_frontend_button_std_c, 100);
    int64_t bar_width = gbtn->width;
    if (MyScreenHeight < 400)
    {
        bar_width += 32;
    }
    draw_slider64k(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, bar_width);
    int64_t shift_x = (gbtn->width - 64*units_per_px/16) * gbtn->slide_val >> 8;
    const struct TbSprite *spr;
    if (gbtn->flags != 0) {
        spr = get_button_sprite(GBS_guisymbols_jewel_on);
    } else {
        spr = get_button_sprite(GBS_guisymbols_jewel_off);
    }
    LbSpriteDrawResized(gbtn->scr_pos_x + shift_x + 24*units_per_px/16, gbtn->scr_pos_y + 6*units_per_px/16, bs_units_per_px, spr);
}

#if (BFDEBUG_LEVEL > 0)
// Code for font testing screen (debug version only)
TbBool fronttestfont_draw(void)
{
  const struct TbSprite *spr;
  uint64_t i;
  uint64_t k;
  int64_t w;
  int64_t h;
  int64_t x;
  int64_t y;
  SYNCDBG(9,"Starting");
  TbPixel* const wscr = RendererGetFramebuffer();
  for (y=0; y < lbDisplay.GraphicsScreenHeight; y++)
    for (x=0; x < lbDisplay.GraphicsScreenWidth; x++)
    {
        wscr[y*lbDisplay.GraphicsScreenWidth+x] = TbPixel_RGB(0, 0, 0);
    }
  LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenHeight/pixel_size, MyScreenWidth/pixel_size);
  // Drawing
  w = 32;
  h = 48;
  for (i=31; i < num_chars_in_font+31; i++)
  {
    k = (i-31);
    SYNCDBG(9,"Drawing char %" PRIu64,(uint64_t)(i));
    x = (k%32)*w + 2;
    y = (k/32)*h + 2;
    if (lbFontPtr != NULL)
      spr = LbFontCharSprite(lbFontPtr,i);
    else
      spr = NULL;
    if (spr != NULL)
    {
      LbDrawBox(x, y, spr->SWidth+2, spr->SHeight+2, resolve_indexed_pixel(255, RendererGetActivePalette()));
      LbSpriteDraw(x+1, y+1, spr);
    }
//TODO SPRITES enhance font support
  }
  // Displaying the new frame
  return true;
}

TbBool fronttestfont_input(void)
{
  const uint64_t keys[] = {KC_Z,KC_1,KC_2,KC_3,KC_4,KC_5,KC_6,KC_7,KC_8,KC_9,KC_0};
  int64_t i;
  for (i=0; i < sizeof(keys)/sizeof(keys[0]); i++)
  {
    if (lbKeyOn[keys[i]])
    {
      lbKeyOn[keys[i]] = 0;
      num_chars_in_font = num_sprites(testfont[i]);
      SYNCDBG(9,"Characters in font %" PRId64 ": %" PRId64,(int64_t)(i),(int64_t)(num_chars_in_font));
      if (i < 4)
        RendererPaletteSet(frontend_palette);//testfont_palette[0]
      else
        RendererPaletteSet(testfont_palette[1]);
      LbTextSetFont(testfont[i]);
      return true;
    }
  }
  return false;
}
#endif


void frontend_draw_icon(struct GuiButton *gbtn)
{
    int64_t units_per_px;
    units_per_px = simple_frontend_sprite_width_units_per_px(gbtn, gbtn->sprite_idx, 100);
    const struct TbSprite *spr = get_frontend_sprite(gbtn->sprite_idx);
    LbSpriteDrawResized(gbtn->scr_pos_x, gbtn->scr_pos_y, units_per_px, spr);
}

void frontend_draw_slider(struct GuiButton *gbtn)
{
    if ((gbtn->flags & LbBtnF_Enabled) == 0) {
        return;
    }
    const int64_t fs_units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_slider_horiz_c, 100);
    const double scale = double(fs_units_per_px) / 16;

    const auto left_sprite = get_frontend_sprite(GFS_slider_horiz_l); // 40 units wide
    LbSpriteDrawResized(gbtn->scr_pos_x, gbtn->scr_pos_y, fs_units_per_px, left_sprite);

    // Draw center sprite draw as many times as necessary
    const auto center_sprite = get_frontend_sprite(GFS_slider_horiz_c); // 110 units wide
    const int64_t right_sprite_x = (gbtn->scr_pos_x + gbtn->width) - (40 * scale);
    for (int64_t x = gbtn->scr_pos_x + (40 * scale); x < right_sprite_x; x += (110 * scale))
    {
        LbSpriteDrawResized(x, gbtn->scr_pos_y, fs_units_per_px, center_sprite);
    }

    const auto right_sprite = get_frontend_sprite(GFS_slider_horiz_r); // 40 units wide
    LbSpriteDrawResized(right_sprite_x, gbtn->scr_pos_y, fs_units_per_px, right_sprite);

    const int64_t knob_position = gbtn->slide_val * (gbtn->width - int64_t(64 * scale)) >> 8;
    const auto knob_sprite = (gbtn->button_state_left_pressed != 0) ?
        get_frontend_sprite(GFS_slider_indicator_act) : get_frontend_sprite(GFS_slider_indicator_std);
    LbSpriteDrawResized(
        (gbtn->scr_pos_x + knob_position + (24 * scale)) / pixel_size,
        (gbtn->scr_pos_y + (3 * scale)) / pixel_size,
        fs_units_per_px,
        knob_sprite
    );
}

void frontend_draw_small_slider(struct GuiButton *gbtn)
{
    if ((gbtn->flags & LbBtnF_Enabled) == 0) {
        return;
    }
    int64_t fs_units_per_px;
    fs_units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_slider_horiz_c, 100);
    int64_t scr_x;
    int64_t scr_y;
    scr_x = gbtn->scr_pos_x;
    scr_y = gbtn->scr_pos_y;
    const struct TbSprite *spr;
    spr = get_frontend_sprite(GFS_slider_horiz_l);
    LbSpriteDrawResized(scr_x, scr_y, fs_units_per_px, spr);
    scr_x += spr->SWidth * fs_units_per_px / 16;
    spr = get_frontend_sprite(GFS_slider_horiz_c);
    LbSpriteDrawResized(scr_x, scr_y, fs_units_per_px, spr);
    scr_x += spr->SWidth * fs_units_per_px / 16;
    spr = get_frontend_sprite(GFS_slider_horiz_r);
    LbSpriteDrawResized(scr_x, scr_y, fs_units_per_px, spr);
    int64_t val;
    val = gbtn->slide_val * (gbtn->width - 64*fs_units_per_px/16) >> 8;
    if (gbtn->button_state_left_pressed != 0) {
        spr = get_frontend_sprite(GFS_slider_indicator_act);
    } else {
        spr = get_frontend_sprite(GFS_slider_indicator_std);
    }
    LbSpriteDrawResized((gbtn->scr_pos_x + val + 24*fs_units_per_px/16) / pixel_size, (gbtn->scr_pos_y + 3*fs_units_per_px/16) / pixel_size, fs_units_per_px, spr);
}

void gui_area_text(struct GuiButton *gbtn)
{
    if ((gbtn->flags & LbBtnF_Enabled) == 0) {
        return;
    }
    int64_t bs_units_per_px = simple_button_sprite_height_units_per_px(gbtn, GBS_frontend_button_std_c, 94);
    int64_t width = gbtn->width;
    TbBool low_res = (MyScreenHeight < 400);
    if (low_res)
    {
        width += 32;
    }
    switch (gbtn->sprite_idx)
    {
    case 1:
        if ( gbtn->button_state_left_pressed || gbtn->button_state_right_pressed )
        {
            draw_bar64k(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, width);
            int64_t lit_width = gbtn->width + 6*units_per_pixel/16;
            if (low_res)
            {
                lit_width += 32;
            }
            draw_lit_bar64k(gbtn->scr_pos_x - 6*units_per_pixel/16, gbtn->scr_pos_y - 6*units_per_pixel/16, bs_units_per_px, lit_width);
        }
        else
        {
            draw_bar64k(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, width);
        }
        break;
    case 2:
        draw_bar64k(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, width);
        break;
    }
    if ((gbtn->tooltip_stridx != GUIStr_Empty) && (gbtn->tooltip_stridx != -GUIStr_Empty))
    {
        if (gbtn->tooltip_stridx > 0)
            snprintf(gui_textbuf,sizeof(gui_textbuf), "%s", get_string(gbtn->tooltip_stridx));
        else
            snprintf(gui_textbuf,sizeof(gui_textbuf), "%s", get_string(-gbtn->tooltip_stridx));
        draw_button_string(gbtn, (gbtn->width*32 + 16)/gbtn->height, gui_textbuf);
    } else
    if (gbtn->content.str != NULL)
    {
        snprintf(gui_textbuf,sizeof(gui_textbuf), "%s", gbtn->content.str);
        // Since this button can have various width, but its height is always 32,
        // unscaled width is deduced based on height scale
        draw_button_string(gbtn, (gbtn->width*32 + 16)/gbtn->height, gui_textbuf);
    }
}

void frontend_init_options_menu(struct GuiMenu *gmnu)
{
    frontend_options_menu_init_sliders(gmnu);
    if (!is_campaign_loaded())
    {
        if (!change_campaign(CampgnT_Default,""))
        {
            ERRORLOG("Unable to load campaign");
        }
    }
}

void frontend_set_player_number(int64_t plr_num)
{
    struct PlayerInfo *player;
    my_player_number = plr_num;
    player = get_my_player();
    player->id_number = plr_num;
    setup_engine_window(0, 0, MyScreenWidth, MyScreenHeight);
}

const char *frontend_button_caption_text(const struct GuiButton *gbtn)
{
    uint64_t febtn_idx;
    int64_t text_idx;
    febtn_idx = gbtn->content.lval;
    if (febtn_idx < FRONTEND_BUTTON_INFO_COUNT)
        text_idx = frontend_button_info[febtn_idx].capstr_idx;
    else
        text_idx = GUIStr_Empty;
    return get_string(text_idx);
}

int64_t frontend_button_caption_font(const struct GuiButton *gbtn, int64_t mouse_over_btn_idx)
{
    uint64_t febtn_idx;
    int64_t font_idx;
    febtn_idx = gbtn->content.lval;
    if (febtn_idx < FRONTEND_BUTTON_INFO_COUNT)
        font_idx = frontend_button_info[febtn_idx].font_index;
    else
        font_idx = 3;
    if ((febtn_idx != 0) && (mouse_over_btn_idx == febtn_idx))
        font_idx = 2;
    return font_idx;
}

void frontend_draw_text(struct GuiButton *gbtn)
{
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    int64_t font_idx;
    if ((gbtn->flags & LbBtnF_Enabled) == 0)
        font_idx = 3;
    else
        font_idx = frontend_button_caption_font(gbtn, frontend_mouse_over_button);
    LbTextSetFont(frontend_font[font_idx]);
    int64_t tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / LbTextLineHeight();
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, gbtn->height);
    LbTextDrawResized(0, 0, tx_units_per_px, frontend_button_caption_text(gbtn));
}

void frontend_change_state(struct GuiButton *gbtn)
{
    frontend_set_state(gbtn->btype_value & LbBFeF_IntValueMask);
}

void frontend_draw_enter_text(struct GuiButton *gbtn)
{
    int64_t font_idx;
    font_idx = 1;
    if (gbtn == input_button) {
        font_idx = 2;
    } else
    if ((gbtn->flags & LbBtnF_Enabled) == 0) {
        font_idx = 3;
    } else
    if ((gbtn->content.str != NULL) && ((gbtn->btype_value & LbBFeF_IntValueMask) == frontend_mouse_over_button)) {
        font_idx = 2;
    }
    char *srctext;
    srctext = gbtn->content.str;
    while (LbTextStringWidth(srctext) > 240)
        srctext[strlen(srctext)-2] = 0;
    char text[2048];
    // Prepare text buffer
    TbBool print_with_cursor = 0;
    if (gbtn == input_button)
    {
        if ((LbTimerClock() / 200 & 1) != 0)
            print_with_cursor = 1;
    }
    snprintf(text, sizeof(text), "%s%s", srctext, print_with_cursor?"_":"");
    LbTextSetFont(frontend_font[font_idx]);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    int64_t tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / LbTextLineHeight();
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, (240 + LbTextCharWidth('_')) * tx_units_per_px / 16, gbtn->height);
    LbTextDrawResized(0, 0, tx_units_per_px, text);
}

void frontend_draw_small_menu_button(struct GuiButton *gbtn)
{
    const char *text;
    text = frontend_button_caption_text(gbtn);
    frontend_draw_button(gbtn, 0, text, Lb_TEXT_HALIGN_CENTER);
}

void frontend_toggle_computer_players(struct GuiButton *gbtn)
{
    struct ScreenPacket *nspck;
    nspck = &net_screen_packet[my_player_number];
    if (screen_packet_action(nspck) == NetAct_None)
    {
        screen_packet_set_action(nspck, NetAct_SetComputerPlayers);
        nspck->action_par1 = (fe_computer_players == 0);
    }
}

void frontend_draw_computer_players(struct GuiButton *gbtn)
{
    int64_t font_idx;
    font_idx = frontend_button_caption_font(gbtn,frontend_mouse_over_button);
    LbTextSetFont(frontend_font[font_idx]);
    const char *text;
    if (fe_computer_players) {
        text = get_string(GUIStr_On);
    } else {
        text = get_string(GUIStr_Off);
    }
    int64_t tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / LbTextLineHeight();
    int64_t ln_height;
    ln_height = LbTextLineHeight() * tx_units_per_px / 16;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, ln_height);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    LbTextDrawResized(0, 0, tx_units_per_px, frontend_button_caption_text(gbtn));
    RendererSetDrawFlags(Lb_TEXT_HALIGN_RIGHT);
    LbTextDrawResized(0, 0, tx_units_per_px, text);
    RendererSetDrawFlags(0);
}


void frontend_draw_mp_mappack(struct GuiButton *gbtn)
{
    int64_t font_idx;
    font_idx = frontend_button_caption_font(gbtn,frontend_mouse_over_button);
    LbTextSetFont(frontend_font[font_idx]);
    const char *text;
    text = campaign.display_name;
    
    int64_t tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / LbTextLineHeight();
    int64_t ln_height;
    ln_height = LbTextLineHeight() * tx_units_per_px / 16;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, ln_height);
    
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    LbTextDrawResized(0, 0, tx_units_per_px, text);
    RendererSetDrawFlags(0);
}

void set_packet_start(struct GuiButton *gbtn)
{
    struct ScreenPacket *nspck;
    nspck = &net_screen_packet[my_player_number];
    if (screen_packet_action(nspck) == NetAct_None)
        screen_packet_set_action(nspck, NetAct_OpenLandView);
}

void draw_scrolling_button_string(struct GuiButton *gbtn, const char *text)
{
  struct TextScrollWindow *scrollwnd;
  int64_t flg_mem;
  int64_t text_height;
  int64_t area_height;
  flg_mem = RendererGetDrawFlags();
  RendererClearDrawFlags(Lb_TEXT_ONE_COLOR);
  RendererAddDrawFlags(Lb_TEXT_HALIGN_CENTER);
  LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, gbtn->height);
  scrollwnd = (struct TextScrollWindow *)gbtn->content.ptr;
  if (scrollwnd == NULL)
  {
      ERRORLOG("Cannot have a TEXT_SCROLLING box type without a pointer to a TextScrollWindow");
      LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenHeight/pixel_size, MyScreenWidth/pixel_size);
      return;
  }
  area_height = gbtn->height;
  scrollwnd->window_height = area_height;
  text_height = scrollwnd->text_height;
  int64_t tx_units_per_px;
  if (dbc_initialized && dbc_enabled)
  {
      tx_units_per_px = scale_value_by_horizontal_resolution((MyScreenWidth >= 640) ? 16 : 32);
  }
  else
  {
      tx_units_per_px = scale_ui_value_lofi(16);
  }
  if (text_height == 0)
  {
      text_height = text_string_height(tx_units_per_px, text);
      SYNCDBG(18,"Computed message height %" PRId64 " for \"%s\"",(int64_t)(text_height),text);
      scrollwnd->text_height = text_height;
  }
  SYNCDBG(18,"Message h=%" PRId64 " Area h=%" PRId64,(int64_t)(text_height),(int64_t)(area_height));
  // If the text is smaller that the area we have for it - just place it at center
  if (text_height <= area_height)
  {
    scrollwnd->start_y = (area_height - text_height) / 2;
  } else
  // Otherwise - we must take scrollbars into account
  {
    // Maintain scrolling actions
    switch ( scrollwnd->action )
    {
    case 1:
      scrollwnd->start_y += 12*units_per_pixel/16;
      break;
    case 2:
      scrollwnd->start_y -= 12*units_per_pixel/16;
      break;
    case 3:
      scrollwnd->start_y += area_height;
      break;
    case 4:
    case 5:
      scrollwnd->start_y -= area_height;
      break;
    }
    if (scrollwnd->action == 5)
    {
      if (scrollwnd->start_y < -text_height)
      {
        scrollwnd->start_y = 0;
      }
    } else
    if (scrollwnd->action != 0)
    {
      if (scrollwnd->start_y < gbtn->height-text_height)
      {
        scrollwnd->start_y = gbtn->height-text_height;
      } else
      if (scrollwnd->start_y > 0)
      {
        scrollwnd->start_y = 0;
      }
    }
    scrollwnd->action = 0;
  }
  // Finally, draw the text
  LbTextDrawResized(0, scrollwnd->start_y, tx_units_per_px, text);
  // And restore default drawing options
  LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenHeight/pixel_size, MyScreenWidth/pixel_size);
  RendererSetDrawFlags(flg_mem);
}

void gui_area_scroll_window(struct GuiButton *gbtn)
{
    struct TextScrollWindow *scrollwnd;
    if ((gbtn->flags & LbBtnF_Enabled) == 0) {
        return;
    }
    scrollwnd = (struct TextScrollWindow *)gbtn->content.ptr;
    if (scrollwnd == NULL) {
        ERRORLOG("Button doesn't point to a TextScrollWindow data item");
        return;
    }
    draw_scrolling_button_string(gbtn, scrollwnd->text);
}

void gui_go_to_event(struct GuiButton *gbtn)
{
    if (my_visible_event_idx) {
        struct Event *event = &kfx_sim_state.event[my_visible_event_idx];
        move_local_camera_to_position(event->mappos_x, event->mappos_y);
    }
}

void gui_close_objective(struct GuiButton *gbtn)
{
    turn_off_event_box_if_necessary(my_player_number, my_visible_event_idx);
}

void gui_scroll_text_up(struct GuiButton *gbtn)
{
    struct TextScrollWindow *scroll_window;
    scroll_window = (struct TextScrollWindow *)gbtn->content.ptr;
    scroll_window->action = 1;
}

void gui_scroll_text_down(struct GuiButton *gbtn)
{
    struct TextScrollWindow *scroll_window;
    scroll_window = (struct TextScrollWindow *)gbtn->content.ptr;
    scroll_window->action = 2;
}

/** frontend_ldcampaign_change_state's actual work for the Main Menu's
 * High Score Table button (its only caller, always targeting
 * FeSt_HIGH_SCORES) minus the state-transition call itself -- see
 * frontend_start_new_game_resolve's comment for why.
 */
int64_t frontend_ldcampaign_change_state_resolve(void)
{
  if (!is_campaign_loaded())
  {
    if (!change_campaign(CampgnT_Default,""))
      return -1;
  }
  return FeSt_HIGH_SCORES;
}

/**
 * Changes state based on a parameter inside GuiButton.
 * But first, loads the default campaign if no campaign is loaded yet.
 */
void frontend_ldcampaign_change_state(struct GuiButton *gbtn)
{
  int64_t next_state = frontend_ldcampaign_change_state_resolve();
  if (next_state >= 0)
      frontend_set_state((FrontendMenuState)next_state);
}

/** frontend_netservice_change_state's actual work for the Main Menu's
 * Multiplayer button (its only caller, always targeting FeSt_NET_SERVICE)
 * minus the state-transition call itself -- see
 * frontend_start_new_game_resolve's comment for why.
 */
int64_t frontend_netservice_change_state_resolve(void)
{
    TbBool set_cmpg;
    set_cmpg = false;
    if (!is_campaign_loaded())
    {
        set_cmpg = true;
    } else
    if (campaign.multi_levels_count < 1)
    {
        set_cmpg = true;
    }
    if (set_cmpg)
    {
        if (!change_campaign(CampgnT_MultiplayerMappack,""))
          return -1;
    }
    return FeSt_NET_SERVICE;
}

/**
 * Changes state based on a parameter inside GuiButton.
 * But first, loads the default campaign if no campaign is loaded,
 * or the loaded one has no MP maps.
 */
void frontend_netservice_change_state(struct GuiButton *gbtn)
{
    int64_t next_state = frontend_netservice_change_state_resolve();
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}

/** Moves Skirmish out of the network-service list onto its own Main Menu
 * button -- previously reached via Multiplayer -> Net Service -> "Play
 * one player", the last row frontnet_service_setup() (front_network.c)
 * used to append to net_service[] when GSF_AllowOnePlayer is set (removed
 * now that this button covers it directly). Replicates exactly what that
 * row's own special case in frontnet_service_select_by_index()
 * (frontmenu_net.c) used to do, minus the state transition itself -- see
 * frontend_start_new_game_resolve's comment for the convention. Routes
 * straight into the same merged mappack+level+preview screen Free play
 * uses (FeSt_MAPPACK_SELECT) instead of the old plain-mappack-list ->
 * NETLAND_VIEW flow -- see frontend_mappack_list_load's own comment
 * (frontmenu_select.c) for how that screen now tells skirmish and normal
 * free play apart.
 */
int64_t frontend_start_skirmish_resolve(void)
{
    frontend_set_player_number(default_loc_player);
    fe_network_active = 0;
    net_service_index_selected = FrontendNetSvc_Skirmish;
    return FeSt_MAPPACK_SELECT;
}

void frontend_start_skirmish(struct GuiButton *gbtn)
{
    int64_t next_state = frontend_start_skirmish_resolve();
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}

TbBool frontend_start_new_campaign(const char *cmpgn_fname)
{
    struct PlayerInfo *player;
    int64_t i;
    SYNCDBG(7,"Starting");
    memset(&intralvl, 0, sizeof(struct IntralevelData));
    if (!change_campaign(CampgnT_Campaign, cmpgn_fname))
        return false;
    set_continue_level_number(first_singleplayer_level());
    for (i=0; i < PLAYERS_COUNT; i++)
    {
        player = get_player(i);
        player->display_flags &= ~PlaF6_PlyrHasQuit;
    }
    player = get_my_player();
    clear_transfered_creatures();
    calculate_moon_phase(false,false);
    hide_all_bonus_levels(player);
    update_extra_levels_visibility();
    return true;
}

/** frontend_start_new_game's actual work, minus the state-transition call
 * itself: returns the FrontendMenuState to transition to (as int, -1 =
 * starting the campaign failed). Split out so the ImGui Main Menu screen
 * can request the transition itself (frontend_set_state() is unsafe to
 * call synchronously from inside an active ImGui window -- see
 * frontgui_screens.cpp's request_frontend_state comment) while the legacy
 * click_event below keeps calling frontend_set_state() directly, unchanged.
 */
int64_t frontend_start_new_game_resolve(void)
{
    const char *cmpgn_fname;
    SYNCDBG(6,"Clicked");
    // Check if we can just start the game without campaign selection screen
    if (campaigns_list.items_num < 1)
      cmpgn_fname = "";
    else
    if (campaigns_list.items_num == 1)
      cmpgn_fname = campaigns_list.items[0].fname;
    else
      cmpgn_fname = NULL;
    if (cmpgn_fname != NULL)
    { // If there's only one campaign, then start it
      if (!frontend_start_new_campaign(cmpgn_fname))
      {
        ERRORLOG("Unable to start new campaign");
        return -1;
      }
      return FeSt_CAMPAIGN_INTRO;
    } else
    { // If there's more campaigns, go to selection screen
      return FeSt_CAMPAIGN_SELECT;
    }
}

void frontend_start_new_game(struct GuiButton *gbtn)
{
    int64_t next_state = frontend_start_new_game_resolve();
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}

void frontend_load_mappacks(struct GuiButton *gbtn)
{
    SYNCDBG(6,"Clicked");
    // Both single- and multi-mappack cases land on the same merged Free
    // play screen now (mappack list + level list together,
    // docs/refactor/gui/04-phase2-landview-panel-investigation.md) -- no
    // need to pre-select a mappack and skip to a levels-only screen here,
    // frontend_mappack_list_load already auto-highlights the first (or
    // only) mappack and its first level on entry.
    //
    // FeSt_MAPPACK_SELECT also serves Skirmish now (frontend_start_skirmish_resolve()),
    // which flags itself via net_service_index_selected == FrontendNetSvc_Skirmish
    // before transitioning here -- reset it explicitly on the normal Free
    // play entry point too, so a prior Skirmish visit this session can't
    // leak into a later normal Free play one (frontend_mappack_list_load()
    // would otherwise source the wrong campaign list).
    net_service_index_selected = FrontendNetSvc_Online;
    frontend_set_state(FeSt_MAPPACK_SELECT);
}

void frontend_load_mp_mappacks(struct GuiButton *gbtn)
{
    SYNCDBG(6,"Clicked");
    frontend_set_state(FeSt_MP_MAPPACK_SELECT);
}

/**
 * Writes the continue game file.
 * If allow_lvnum_grow is true and my_player has won the singleplayer level,
 * then next level is written into continue file. This should be the case
 * if complete_level() wasn't called yet.
 */
int64_t frontend_save_continue_game(int64_t allow_lvnum_grow)
{
    struct PlayerInfo *player;
    struct Dungeon *dungeon;
    int64_t victory_state;
    int64_t flg_mem;
    LevelNumber lvnum;
    struct UserState *ustate = get_user_state(get_local_user());
    lvnum = get_loaded_level_number();
    SYNCDBG(6,"Starting");
    player = get_my_player();
    dungeon = get_players_dungeon(player);
    // Do not allow current level to grow if it wasn't just beaten
    if (get_loaded_level_number() != get_continue_level_number()) {
        allow_lvnum_grow = false;
    }
    // Save some of the data from clearing
    victory_state = player->victory_state;
    memcpy(scratch, &dungeon->lvstats, sizeof(struct LevelStats));
    flg_mem = ((ustate->additional_flags & UsrAF_UnlockedLordTorture) != 0);
    // clear all data
    clear_game_for_save();
    // Restore saved data
    player->victory_state = victory_state;
    memcpy(&dungeon->lvstats, scratch, sizeof(struct LevelStats));
    set_flag_value(ustate->additional_flags, UsrAF_UnlockedLordTorture, flg_mem);
    // Skirmish/Free Play completion tracking ("scenario" levels in the
    // player-facing sense): a separate, much lighter record than
    // singleplayer campaign progress below -- no transfer creatures/bonus
    // tracking/next_level, just "was this level ever won". Recorded here,
    // not skipped by the early-return right below, since that return is
    // specifically about the singleplayer continue-game path.
    // !network_is_active() is what excludes real networked multiplayer --
    // Skirmish-vs-AI is is_multiplayer_level()==true but network_is_active()
    // ==false, so it's still tracked; an actual network game is not.
    if (!network_is_active()
     && ((kfx_sim_state.operation_flags & GOF_SingleLevel) == 0)
     && (!kfx_net_state.packet_load_enable)
     && (player->victory_state == VicS_WonLevel)
     && (is_freeplay_level(lvnum) || is_multiplayer_level(lvnum)))
        campaign_progress_record_pack_level_completed(lvnum);
    // Only save continue if level was won, not a free play level, not a multiplayer level and not in packet mode
    if (network_is_active()
     || ((kfx_sim_state.operation_flags & GOF_SingleLevel) != 0)
     || (kfx_net_state.packet_load_enable)
     || (is_freeplay_level(lvnum))
     || (is_multiplayer_level(lvnum)))
        return false;
    // Select the continue level (move the campaign forward)
    if ((allow_lvnum_grow) && (player->victory_state == VicS_WonLevel)) {
        // If level number growth makes sense, do it
        SYNCDBG(7,"Progressing the campaign");
        lvnum = move_campaign_to_next_level();
    } else {
        SYNCDBG(7,"No change in campaign position, victory state %" PRId64,(int64_t)player->victory_state);
        lvnum = get_continue_level_number();
    }
    // fx1contn.sav is retired (was the `-classicmenu` continue-save format,
    // now removed) -- progress.cfg via campaign_progress_record_level_completed()
    // is the only continue-game path left.
    return campaign_progress_record_level_completed(lvnum);
}

/** frontend_load_continue_game's actual work, minus the state-transition
 * call itself -- see frontend_start_new_game_resolve's comment for why.
 */
int64_t frontend_load_continue_game_resolve(void)
{
  // Continue Game routes to Campaign Select instead of loading one specific
  // saved level -- mirrors frontend_start_new_game_resolve()'s own "more
  // than one campaign -> go to selection screen" case exactly, needing no
  // pre-selected campaign/level state: entering FeSt_CAMPAIGN_SELECT
  // (frontend_campaign_list_load()) already builds its own list fresh.
  return FeSt_CAMPAIGN_SELECT;
}

void frontend_load_continue_game(struct GuiButton *gbtn)
{
    int64_t next_state = frontend_load_continue_game_resolve();
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}

void frontend_load_game_maintain(struct GuiButton *gbtn)
{
    int64_t game_index=load_game_scroll_offset+(gbtn->content.lval)-45;
    if (game_index < number_of_saved_games)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
}

void do_button_click_actions(struct GuiButton *gbtn, unsigned char *s, Gf_Btn_Callback callback)
{
    SYNCDBG(9,"Starting for button type %" PRId64,(int64_t)gbtn->gbtype);
    if (gbtn->gbtype == LbBtnT_RadioBtn)
    {
        //TODO: pointers comparison should be avoided
        if (s == &gbtn->button_state_right_pressed)
            return;
    }
    if ((gbtn->flags & LbBtnF_Enabled) != 0)
    {
        switch (gbtn->gbtype)
        {
        case LbBtnT_NormalBtn:
        case LbBtnT_ToggleBtn:
        case LbBtnT_EditBox:
        case LbBtnT_Hotspot:
            *s = 1;
            break;
        case LbBtnT_RadioBtn:
            if ((gbtn->content.ptr != NULL) && (!*s))
            {
                unsigned char *rbstate;
                rbstate = (unsigned char *)gbtn->content.ptr;
                do_sound_button_click(gbtn);
                struct GuiMenu *amnu;
                amnu = get_active_menu(gbtn->gmenu_idx);
                clear_radio_buttons(amnu);
                *rbstate = 1;
                *s = 1;
                update_radio_button_data(amnu);
            }
            if (callback != NULL) {
                callback(gbtn);
            }
            break;
        }
    }
}

void do_button_press_actions(struct GuiButton *gbtn, unsigned char *s, Gf_Btn_Callback callback)
{
    SYNCDBG(9,"Starting for button type %" PRId64,(int64_t)gbtn->gbtype);
    if (gbtn->gbtype == LbBtnT_RadioBtn)
    {
        //TODO: pointers comparison should be avoided
        if (s == &gbtn->button_state_right_pressed)
            return;
    }
    if ((gbtn->flags & LbBtnF_Enabled) != 0)
    {
        switch (gbtn->gbtype)
        {
        case LbBtnT_HoldableBtn:
            if ((*s > 5) && (callback != NULL)) {
                callback(gbtn);
            } else {
                (*s)++;
            }
            break;
        case LbBtnT_Hotspot:
            if (callback != NULL) {
                callback(gbtn);
            }
            break;
        case LbBtnT_NormalBtn:
        case LbBtnT_ToggleBtn:
        case LbBtnT_EditBox:
        case LbBtnT_RadioBtn:
            break;
        }
    }
}

static void autofill_savegame_name(struct GuiButton *gbtn)
{
    int64_t max_len = gbtn->maxval;
    if ((max_len <= 0) || (max_len > SAVE_TEXTNAME_LEN))
        max_len = SAVE_TEXTNAME_LEN;
        
    const char* lv_name = NULL;
    LevelNumber lvnum = get_loaded_level_number();
    struct LevelInformation* lvinfo = get_level_info(lvnum);
    if (lvinfo != NULL)
    {
      if (lvinfo->name_stridx > 0)
          lv_name = get_string(lvinfo->name_stridx);
      else
          lv_name = lvinfo->name;
    } else
      lv_name = level_name;
    

    if (gbtn->content.str != NULL)
        memset(gbtn->content.str, 0, max_len);


    struct TbDate curr_date;
    struct TbTime curr_time;
    char datetime_buf[12];
    if (LbDateTime(&curr_date, &curr_time) >= Lb_OK)
    {
        uint64_t year2 = (uint64_t)(curr_date.Year % 100);
        snprintf(datetime_buf, sizeof(datetime_buf), "%02" PRIu64 "%02" PRIu64 "%02" PRIu64 "-%02" PRIu64 "%02" PRIu64,
            (uint64_t)(year2), (uint64_t)curr_date.Month, (uint64_t)curr_date.Day,
            (uint64_t)curr_time.Hour, (uint64_t)curr_time.Minute);
    }

    size_t lv_name_len = 0;
    if ((lv_name != NULL) && (lv_name[0] != '\0'))
        lv_name_len = strnlen(lv_name, (size_t)max_len - strlen(datetime_buf) - 1);

    snprintf(gbtn->content.str, max_len, "%.*s-%s", (int)((int64_t)lv_name_len), lv_name, datetime_buf);
    
    gbtn->content.str[max_len - 1] = '\0';

    gbtn->button_state_left_pressed = 0;
    (gbtn->click_event)(gbtn);
    if ((gbtn->flags & LbBtnF_Clickable) != 0)
    {
        struct GuiMenu *gmnu = get_active_menu(gbtn->gmenu_idx);
        gmnu->visual_state = 3;
        remove_from_menu_stack(gmnu->ident);
    }
}

void do_button_release_actions(struct GuiButton *gbtn, unsigned char *s, Gf_Btn_Callback callback)
{
  SYNCDBG(17,"Starting");
  int64_t i;
  struct GuiMenu *gmnu;
  switch ( gbtn->gbtype )
  {
  case LbBtnT_NormalBtn:
  case LbBtnT_HoldableBtn:
      if ((*s != 0) && (callback != NULL))
      {
          do_sound_button_click(gbtn);
          callback(gbtn);
      }
      *s = 0;
      break;
  case LbBtnT_ToggleBtn:
      i = *(unsigned char *)gbtn->content.ptr;
      i++;
      if (i > gbtn->maxval)
          i = 0;
      *(unsigned char *)gbtn->content.ptr = i;
      if ((*s != 0) && (callback != NULL))
      {
          do_sound_button_click(gbtn);
          callback(gbtn);
      }
      *s = 0;
      break;
  case LbBtnT_RadioBtn:
      //TODO: pointers comparison should be avoided
      if (s == &gbtn->button_state_right_pressed)
        return;
      break;
  case LbBtnT_EditBox:
      if ((last_used_input_device == ID_Controller)  && (gbtn->content.str != NULL) && menu_is_active(GMnu_SAVE))
      {
            autofill_savegame_name(gbtn);
            break;
      }
      input_button = gbtn;
      LbStartTextInput();
      setup_input_field(input_button, get_string(GUIStr_MnuUnused));
      break;
  default:
      break;
  }

  if (s == &gbtn->button_state_left_pressed)
  {
    gmnu = get_active_menu(gbtn->gmenu_idx);
    if (gbtn->parent_menu != NULL)
      create_menu(gbtn->parent_menu);
    if ((gbtn->flags & LbBtnF_Clickable) && (gbtn->gbtype != LbBtnT_EditBox))
    {
      if (callback == NULL)
        do_sound_menu_click();
      gmnu->visual_state = 3;
    }
  }
  SYNCDBG(17,"Finished");
}

/**
 * Returns if the menu is toggleable. If it's not, then it is always
 * visible until it's deleted.
 */
int64_t is_toggleable_menu(int64_t mnu_idx)
{
  switch (mnu_idx)
  {
  case GMnu_MAIN:
  case GMnu_ROOM:
  case GMnu_ROOM2:
  case GMnu_SPELL:
  case GMnu_SPELL2:
  case GMnu_TRAP:
  case GMnu_TRAP2:
  case GMnu_CREATURE:
  case GMnu_EVENT:
  case GMnu_QUERY:
      return true;
  case GMnu_TEXT_INFO:
  case GMnu_DUNGEON_SPECIAL:
  case GMnu_CREATURE_QUERY1:
  case GMnu_CREATURE_QUERY2:
  case GMnu_CREATURE_QUERY3:
  case GMnu_CREATURE_QUERY4:
  case GMnu_BATTLE:
  case GMnu_SPELL_LOST:
      return true;
  case GMnu_OPTIONS:
  case GMnu_INSTANCE:
  case GMnu_QUIT:
  case GMnu_LOAD:
  case GMnu_SAVE:
  case GMnu_VIDEO:
  case GMnu_SOUND:
  case GMnu_ERROR_BOX:
  case GMnu_HOLD_AUDIENCE:
  case GMnu_FEMAIN:
  case GMnu_FELOAD:
  case GMnu_FENET_SERVICE:
  case GMnu_FENET_SESSION:
  case GMnu_FENET_START:
  case GMnu_FESTATISTICS:
  case GMnu_FEHIGH_SCORE_TABLE:
  case GMnu_RESURRECT_CREATURE:
  case GMnu_TRANSFER_CREATURE:
  case GMnu_ARMAGEDDON:
  case GMnu_FEDEFINE_KEYS:
  case GMnu_AUTOPILOT:
  case GMnu_FEOPTION:
  case GMnu_FELEVEL_SELECT:
  case GMnu_MAPPACK_SELECT:
  case GMnu_FECAMPAIGN_SELECT:
  case GMnu_FEERROR_BOX:
  case GMnu_MP_MAPPACK_SELECT:
      return false;
  default:
      return true;
  }
}

int64_t create_button(struct GuiMenu *gmnu, struct GuiButtonInit *gbinit, int64_t units_per_px)
{
    struct GuiButton *gbtn;
    int64_t gidx;
    int64_t i;
    gidx = guibutton_get_unused_slot();
    if (gidx == -1) {
        // No free buttons
        return -1;
    }
    gbtn = &active_buttons[gidx];
    gbtn->flags |= LbBtnF_Active;
    struct GuiMenu *gmnuinit;
    gmnuinit = gmnu->menu_init;
    gbtn->gmenu_idx = gmnu->number;
    gbtn->gbtype = gbinit->gbtype;
    gbtn->id_num = gbinit->id_num;
    gbtn->flags ^= (gbtn->flags ^ LbBtnF_Clickable * (gbinit->button_flags & 0xff)) & LbBtnF_Clickable;
    gbtn->flags ^= (gbtn->flags ^ LbBtnF_NoClickAway * ((gbinit->button_flags >> 1) & 1)) & LbBtnF_NoClickAway;
    gbtn->click_event = gbinit->click_event;
    gbtn->rclick_event = gbinit->rclick_event;
    gbtn->ptover_event = gbinit->ptover_event;
    gbtn->btype_value = gbinit->btype_value;
    gbtn->width = (gbinit->width * units_per_px + 8) / 16;
    gbtn->height = (gbinit->height * units_per_px + 8) / 16;
    gbtn->draw_call = gbinit->draw_call;
    gbtn->sprite_idx = get_player_colored_icon_idx(gbinit->sprite_idx,my_player_number);
    gbtn->tooltip_stridx = gbinit->tooltip_stridx;
    gbtn->parent_menu = gbinit->parent_menu;
    gbtn->content = gbinit->content;
    gbtn->maxval = gbinit->maxval;
    gbtn->maintain_call = gbinit->maintain_call;
    gbtn->flags |= LbBtnF_Enabled;
    gbtn->flags &= ~LbBtnF_MouseOver;
    gbtn->button_state_left_pressed = 0;
    gbtn->flags |= LbBtnF_Visible;
    gbtn->flags ^= (gbtn->flags ^ LbBtnF_Toggle * (gbinit->button_flags >> 8)) & LbBtnF_Toggle;
    if ((gbinit->scr_pos_x == 999) || (gbinit->pos_x == 999))
    {
        i = gmnu->pos_x + ((gmnuinit->width >> 1) - (gbinit->width >> 1)) * units_per_px / 16;
        gbtn->scr_pos_x = i;
        gbtn->pos_x = i;
    } else
    {
        gbtn->pos_x = gmnu->pos_x + (gbinit->pos_x * units_per_px + 8) / 16;
        gbtn->scr_pos_x = gmnu->pos_x + (gbinit->scr_pos_x * units_per_px + 8) / 16;
    }
    if ((gbinit->scr_pos_y == 999) || (gbinit->pos_y == 999))
    {
        i = gmnu->pos_y + (((gmnuinit->height >> 1) - (gbinit->height >> 1)) * units_per_px + 8) / 16;
        gbtn->scr_pos_y = i;
        gbtn->pos_y = i;
    } else
    {
        gbtn->pos_y = (gbinit->pos_y * units_per_px + 8) / 16 + gmnu->pos_y;
        gbtn->scr_pos_y = gmnu->pos_y + (gbinit->scr_pos_y * units_per_px + 8) / 16;
    }
    if (gbtn->gbtype == LbBtnT_RadioBtn)
    {
        struct TextScrollWindow *scrollwnd;
        scrollwnd = (struct TextScrollWindow *)gbtn->content.ptr;
        if ((scrollwnd != NULL) && (scrollwnd->text[0] == 1))
        {
            gbtn->button_state_left_pressed = 1;
            gbtn->button_state_right_pressed = 0;
        } else
        {
            gbtn->button_state_left_pressed = 0;
            gbtn->button_state_right_pressed = 0;
        }
    } else
    {
        gbtn->button_state_left_pressed = 0;
        gbtn->button_state_right_pressed = 0;
    }
    SYNCDBG(11,"Created button %" PRId64 " at (%" PRId64 ",%" PRId64 ") size (%" PRId64 ",%" PRId64 ")",(int64_t)(gidx),
        (int64_t)(gbtn->pos_x),(int64_t)(gbtn->pos_y),(int64_t)(gbtn->width),(int64_t)(gbtn->height));
    return gidx;

}

int64_t compute_menu_position_x(int64_t desired_pos,int64_t menu_width, int64_t units_per_px)
{
  int64_t scaled_width;
  scaled_width = (menu_width * units_per_px + 8) / 16;
  int64_t pos;
  switch (desired_pos)
  {
  case POS_MOUSMID: // Place menu centered over mouse
      pos = GetMouseX() - (scaled_width >> 1);
      break;
  case POS_GAMECTR: // Player-based positioning
      pos = (local_state.engine_window_x) + (local_state.engine_window_width >> 1) - (scaled_width >> 1);
      break;
  case POS_MOUSPRV: // Place menu centered over previous mouse position
      pos = old_menu_mouse_x - (scaled_width >> 1);
      break;
  case POS_SCRCTR:
      pos = (MyScreenWidth >> 1) - (scaled_width >> 1);
      break;
  case POS_SCRBTM:
      pos = MyScreenWidth - scaled_width;
      break;
  default: // Desired position have direct coordinates
      pos = ((desired_pos*(int64_t)units_per_pixel)>>4)*((int64_t)pixel_size);
      if (pos+scaled_width > lbDisplay.PhysicalScreenWidth*((int64_t)pixel_size))
        pos = lbDisplay.PhysicalScreenWidth*((int64_t)pixel_size)-scaled_width;
/* Helps not to touch left panel - disabling, as needs additional conditions
      if (pos < status_panel_width)
        pos = status_panel_width;
*/
      break;
  }
  // Clipping position X
  if (desired_pos == POS_GAMECTR)
  {
    if (pos+scaled_width > MyScreenWidth)
      pos = MyScreenWidth-scaled_width;
    if (pos < local_state.engine_window_x)
      pos = local_state.engine_window_x;
  } else
  {
    if (pos+scaled_width > MyScreenWidth)
      pos = MyScreenWidth-scaled_width;
    if (pos < 0)
      pos = 0;
  }
  return pos;
}

int64_t compute_menu_position_y(int64_t desired_pos,int64_t menu_height, int64_t units_per_px)
{
    int64_t scaled_height;
    scaled_height = (menu_height * units_per_px + 8) / 16;
    int64_t pos;
    switch (desired_pos)
    {
    case POS_MOUSMID: // Place menu centered over mouse
        pos = GetMouseY() - (scaled_height >> 1);
        break;
    case POS_GAMECTR: // Player-based positioning
        pos = (local_state.engine_window_height >> 1) - ((scaled_height+20*units_per_px/16) >> 1);
        break;
    case POS_MOUSPRV: // Place menu centered over previous mouse position
        pos = old_menu_mouse_y - (scaled_height >> 1);
        break;
    case POS_SCRCTR:
        pos = (MyScreenHeight >> 1) - (scaled_height >> 1);
        break;
    case POS_SCRBTM:
        pos = MyScreenHeight - scaled_height;
        break;
    default: // Desired position have direct coordinates
        pos = ((desired_pos*((int64_t)units_per_pixel))>>4)*((int64_t)pixel_size);
        if (pos+scaled_height > lbDisplay.PhysicalScreenHeight*((int64_t)pixel_size))
          pos = lbDisplay.PhysicalScreenHeight*((int64_t)pixel_size)-scaled_height;
        break;
    }
    // Clipping position Y
    if (pos+scaled_height > MyScreenHeight)
      pos = MyScreenHeight-scaled_height;
    if (pos < 0)
      pos = 0;
    return pos;
}

MenuNumber create_menu(struct GuiMenu *gmnu)
{
    MenuNumber mnu_num;
    struct GuiMenu *amnu;
    Gf_Mnu_Callback callback;
    struct GuiButtonInit *btninit;
    int64_t i;
    SYNCDBG(18,"Starting menu ID %" PRId64,(int64_t)(gmnu->ident));
    mnu_num = menu_id_to_number(gmnu->ident);
    if (mnu_num >= 0)
    {
        amnu = get_active_menu(mnu_num);
        amnu->visual_state = 1;
        amnu->fade_time = gmnu->fade_time;
        amnu->is_turned_on = ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) || (!is_toggleable_menu(gmnu->ident));
        SYNCDBG(18,"Menu number %" PRId64 " already active",(int64_t)mnu_num);
        return mnu_num;
    }
    add_to_menu_stack(gmnu->ident);
    mnu_num = first_available_menu();
    if (mnu_num == -1)
    {
        ERRORLOG("Too many menus open");
        return -1;
    }
    SYNCDBG(18,"Menu number %" PRId64 " added to stack",(int64_t)mnu_num);
    amnu = get_active_menu(mnu_num);
    amnu->visual_state = 1;
    amnu->number = mnu_num;
    amnu->menu_init = gmnu;
    amnu->ident = gmnu->ident;
    if (amnu->ident == GMnu_MAIN)
    {
        old_menu_mouse_x = GetMouseX();
        old_menu_mouse_y = GetMouseY();
    }
    // Make scale factor
    int64_t units_per_px;
    units_per_px = min((int64_t)units_per_pixel,units_per_pixel_min*16/10);
    // Decrease scale factor if for some reason resulting size would exceed screen (wierd aspec ratio support)
    if (gmnu->width * units_per_px > LbScreenWidth() * 16)
        units_per_px = LbScreenWidth() * 16 / gmnu->width;
    if (gmnu->height * units_per_px > LbScreenHeight() * 16)
        units_per_px = LbScreenHeight() * 16 / gmnu->height;
    // Setting position X
    amnu->pos_x = compute_menu_position_x(gmnu->pos_x,gmnu->width,units_per_px);
    // Setting position Y
    amnu->pos_y = compute_menu_position_y(gmnu->pos_y,gmnu->height,units_per_px);

    amnu->fade_time = gmnu->fade_time;
    if (amnu->fade_time < 1) {
        ERRORLOG("Fade time %" PRId64 " is less than 1.",(int64_t)amnu->fade_time);
    }
    amnu->buttons = gmnu->buttons;
    amnu->width = (gmnu->width * units_per_px + 8) / 16;
    amnu->height = (gmnu->height * units_per_px + 8) / 16;
    amnu->draw_cb = gmnu->draw_cb;
    amnu->create_cb = gmnu->create_cb;
    amnu->is_monopoly_menu = gmnu->is_monopoly_menu;
    amnu->is_active_panel = gmnu->is_active_panel;
    amnu->is_turned_on = ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) || (!is_toggleable_menu(gmnu->ident));
    callback = amnu->create_cb;
    if (callback != NULL)
        callback(amnu);
    btninit = gmnu->buttons;
    for (i=0; btninit[i].gbtype != -1; i++)
    {
        if (create_button(amnu, &btninit[i], units_per_px) == -1)
        {
          ERRORLOG("Cannot Allocate button");
          return -1;
        }
    }
    update_radio_button_data(amnu);
    init_slider_bars(amnu);
    init_menu_buttons(amnu);
    SYNCDBG(18,"Created menu ID %" PRId64 " at slot %" PRId64 ", pos (%" PRId64 ",%" PRId64 ") size (%" PRId64 ",%" PRId64 ")",(int64_t)gmnu->ident,
        (int64_t)mnu_num,(int64_t)amnu->pos_x,(int64_t)amnu->pos_y,(int64_t)amnu->width,(int64_t)amnu->height);
    return mnu_num;
}

/**
 * Sets the status menu visiblity.
 *
 * Doesn't change anything if the current menu visibility is the same as the passed parameter.
 *
 * @param visible If TRUE show the menu, if FALSE hide the menu
 * @return The visibility of the menu before this function was called (used to store the user's previous setting when the menu is forcibly hidden).
 */
uint64_t toggle_status_menu(int64_t visible)
{
  static TbBool room_on = false;
  static TbBool room_2_on = false;
  static TbBool spell_on = false;
  static TbBool spell_2_on = false;
  static TbBool spell_lost_on = false;
  static TbBool trap_on = false;
  static TbBool trap_2_on = false;
  static TbBool creat_on = false;
  static TbBool event_on = false;
  static TbBool query_on = false;
  static TbBool creature_query1_on = false;
  static TbBool creature_query2_on = false;
  static TbBool creature_query3_on = false;
  static TbBool creature_query4_on = false;
  static TbBool objective_on = false;
  static TbBool battle_on = false;
  static TbBool special_on = false;

  int64_t k = menu_id_to_number(GMnu_MAIN);
  if (k < 0) return 0;
  // Update pannel width
  status_panel_width = get_active_menu(k)->width;
  uint64_t i = get_active_menu(k)->is_turned_on;
  if (visible != i)
  {
    if ( visible )
    {
      set_menu_visible_on(GMnu_MAIN);
      if ( room_on )
        set_menu_visible_on(GMnu_ROOM);
      if ( room_2_on )
        set_menu_visible_on(GMnu_ROOM2);
      if ( spell_on )
        set_menu_visible_on(GMnu_SPELL);
      if ( spell_2_on )
        set_menu_visible_on(GMnu_SPELL2);
      if ( spell_lost_on )
        set_menu_visible_on(GMnu_SPELL_LOST);
      if ( trap_on )
        set_menu_visible_on(GMnu_TRAP);
      if ( trap_2_on )
        set_menu_visible_on(GMnu_TRAP2);
      if ( event_on )
        set_menu_visible_on(GMnu_EVENT);
      if ( query_on )
        set_menu_visible_on(GMnu_QUERY);
      if ( creat_on )
        set_menu_visible_on(GMnu_CREATURE);
      if ( creature_query1_on )
        set_menu_visible_on(GMnu_CREATURE_QUERY1);
      if ( creature_query2_on )
        set_menu_visible_on(GMnu_CREATURE_QUERY2);
      if ( creature_query3_on )
        set_menu_visible_on(GMnu_CREATURE_QUERY3);
      if ( creature_query4_on )
        set_menu_visible_on(GMnu_CREATURE_QUERY4);
      if ( battle_on )
        set_menu_visible_on(GMnu_BATTLE);
      if ( objective_on )
        set_menu_visible_on(GMnu_TEXT_INFO);
      if ( special_on )
        set_menu_visible_on(GMnu_DUNGEON_SPECIAL);
    } else
    {
      set_menu_visible_off(GMnu_MAIN);

      k = menu_id_to_number(GMnu_ROOM);
      if (k >= 0)
        room_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_ROOM);

      k = menu_id_to_number(GMnu_ROOM2);
      if (k >= 0)
        room_2_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_ROOM2);

      k = menu_id_to_number(GMnu_SPELL);
      if (k >= 0)
        spell_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_SPELL);

      k = menu_id_to_number(GMnu_SPELL2);
      if (k >= 0)
        spell_2_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_SPELL2);

      k = menu_id_to_number(GMnu_SPELL_LOST);
      if (k >= 0)
        spell_lost_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_SPELL_LOST);

      k = menu_id_to_number(GMnu_TRAP);
      if (k >= 0)
      trap_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_TRAP);

      k = menu_id_to_number(GMnu_TRAP2);
      if (k >= 0)
        trap_2_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_TRAP2);

      k = menu_id_to_number(GMnu_CREATURE);
      if (k >= 0)
        creat_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_CREATURE);

      k = menu_id_to_number(GMnu_EVENT);
      if (k >= 0)
        event_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_EVENT);

      k = menu_id_to_number(GMnu_QUERY);
      if (k >= 0)
        query_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_QUERY);

      k = menu_id_to_number(GMnu_CREATURE_QUERY1);
      if (k >= 0)
        creature_query1_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_CREATURE_QUERY1);

      k = menu_id_to_number(GMnu_CREATURE_QUERY2);
      if (k >= 0)
        creature_query2_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_CREATURE_QUERY2);

      k = menu_id_to_number(GMnu_CREATURE_QUERY3);
      if (k >= 0)
        creature_query3_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_CREATURE_QUERY3);

      k = menu_id_to_number(GMnu_CREATURE_QUERY4);
      if (k >= 0)
        creature_query4_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_CREATURE_QUERY4);

      k = menu_id_to_number(GMnu_TEXT_INFO);
      if (k >= 0)
        objective_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_TEXT_INFO);

      k = menu_id_to_number(GMnu_BATTLE);
      if (k >= 0)
        battle_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_BATTLE);

      k = menu_id_to_number(GMnu_DUNGEON_SPECIAL);
      if (k >= 0)
        special_on = get_active_menu(k)->is_turned_on;
      set_menu_visible_off(GMnu_DUNGEON_SPECIAL);
    }
  }
  return i;
}

TbBool toggle_first_person_menu(TbBool visible)
{
  static unsigned char creature_query_on = 0;
  if (visible)
  {
    if (creature_query_on == 1)
        set_menu_visible_on(GMnu_CREATURE_QUERY1);
    else
        if (creature_query_on == 2)
      set_menu_visible_on(GMnu_CREATURE_QUERY2);
    else
        if (creature_query_on == 3)
        set_menu_visible_on(GMnu_CREATURE_QUERY3);
    else
        if (creature_query_on == 4)
        set_menu_visible_on(GMnu_CREATURE_QUERY4);
    else
    {
      WARNMSG("No active query for first person menu; assuming query 1.");
      set_menu_visible_on(GMnu_CREATURE_QUERY1);
    }
    return true;
  } else
  {
    int64_t menu_num;
    // CREATURE_QUERY1
    menu_num = menu_id_to_number(GMnu_CREATURE_QUERY1);
    if (menu_num >= 0)
        creature_query_on = 1;
    set_menu_visible_off(GMnu_CREATURE_QUERY1);
    // CREATURE_QUERY2
    menu_num = menu_id_to_number(GMnu_CREATURE_QUERY2);
    if (menu_num >= 0)
        creature_query_on = 2;
    set_menu_visible_off(GMnu_CREATURE_QUERY2);
    // CREATURE_QUERY3
    menu_num = menu_id_to_number(GMnu_CREATURE_QUERY3);
    if (menu_num >= 0)
        creature_query_on = 3;
    set_menu_visible_off(GMnu_CREATURE_QUERY3);
    // CREATURE_QUERY4
    menu_num = menu_id_to_number(GMnu_CREATURE_QUERY4);
    if (menu_num >= 0)
        creature_query_on = 4;
    set_menu_visible_off(GMnu_CREATURE_QUERY4);
    return true;
  }
}

// The ImGui HUD composites over a full-screen engine window (also the only
// way a horizontal HUD layout works); the classic sprite GUI insets the
// engine window by the sidebar's width. Split out of set_gui_visible() so a
// live GUI_ICON_PACK change (frontgui_ingame.cpp's ingame_imgui_frame(),
// which polls ingame_gui_use_classic_hud() every frame) can re-apply the
// inset too -- without this, switching classic HUD on/off mid-session left
// the engine window at whatever inset the last GOF_ShowGui toggle had
// computed, showing as a black strip where the 3D view no longer reaches
// but nothing (sidebar or otherwise) draws over it either (live-tested).
void refresh_engine_window_for_gui_style(void)
{
  if (((kfx_sim_state.view_mode_flags & GNFldD_StatusPanelDisplay) != 0)
      && ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0)
      && ingame_gui_use_classic_hud())
  {
      setup_engine_window(status_panel_width, 0, MyScreenWidth, MyScreenHeight);
  }
  else
  {
      setup_engine_window(0, 0, MyScreenWidth, MyScreenHeight);
  }
}

void set_gui_visible(TbBool visible)
{
  SYNCDBG(6,"Starting");
  set_flag_value(kfx_sim_state.operation_flags, GOF_ShowGui, visible);
  struct PlayerInfo *player=get_my_player();
  unsigned char is_visbl = ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0);
  switch (player->view_type)
  {
  case PVT_CreatureContrl:
  case PVT_CreaturePasngr:
      toggle_first_person_menu(is_visbl);
      break;
  case PVT_MapScreen:
  case PVT_MapFadeIn:
  case PVT_MapFadeOut:
      toggle_status_menu(0);
      break;
  case PVT_DungeonTop:
  default:
      toggle_status_menu(is_visbl);
      break;
  }
  refresh_engine_window_for_gui_style();
}

void toggle_gui(void)
{
    TbBool visible = ((kfx_sim_state.operation_flags & GOF_ShowGui) == 0);
    set_gui_visible(visible);
}

void reinit_all_menus(void)
{
    TbBool visible = ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0);
    init_gui();
    reset_gui_based_on_player_mode();
    set_gui_visible(visible);
}

const char * mdlf_for_cd(const char * input)
{
    if (input[0] != '*') {
        snprintf(path_string, sizeof(path_string), "%s/%s", install_info.inst_path, input); // todo check out
        return path_string;
    }
    return input;
}

void frontend_load_data_from_cd(void)
{
    LbDataLoadSetModifyFilenameFunction(mdlf_for_cd);
}

void frontend_load_data_reset(void)
{
  LbDataLoadSetModifyFilenameFunction(defaultModifyDataLoadFilename);
}

void initialise_tab_tags(MenuID menu_id)
{
    info_tag =  (menu_id == GMnu_QUERY) || (menu_id == GMnu_CREATURE_QUERY1) ||
        (menu_id == GMnu_CREATURE_QUERY2) || (menu_id == GMnu_CREATURE_QUERY3) || (menu_id == GMnu_CREATURE_QUERY4);
    if (menu_id == GMnu_ROOM)
    {
        room_tag = 1;
    }
    else if (menu_id == GMnu_ROOM2)
    {
        room_tag = 2;
    }
    else
    {
        room_tag = 0;
    }
    if (menu_id == GMnu_SPELL)
    {
        spell_tag = 1;
    }
    else if (menu_id == GMnu_SPELL2)
    {
        spell_tag = 2;
    }
    else
    {
        spell_tag = 0;
    }
    if (menu_id == GMnu_TRAP)
    {
        trap_tag = 1;
    }
    else if (menu_id == GMnu_TRAP2)
    {
        trap_tag = 2;
    }
    else
    {
        trap_tag = 0;
    }
    creature_tag = (menu_id == GMnu_CREATURE);
}

void initialise_tab_tags_and_menu(MenuID menu_id)
{
    MenuNumber menu_num;
    initialise_tab_tags(menu_id);
    menu_num = menu_id_to_number(menu_id);
    if (menu_num >= 0) {
        setup_radio_buttons(get_active_menu(menu_num));
    }
}

void init_gui(void)
{
  memset(breed_activities, 0, CREATURE_TYPES_MAX *sizeof(int64_t));
  memset(menu_stack, 0, ACTIVE_MENUS_COUNT*sizeof(unsigned char));
  memset(active_menus, 0, ACTIVE_MENUS_COUNT*sizeof(struct GuiMenu));
  memset(active_buttons, 0, ACTIVE_BUTTONS_COUNT*sizeof(struct GuiButton));
  breed_activities[0] = get_players_special_digger_model(my_player_number);
  kfx_frontend_state.no_of_breeds_owned = 1;
  kfx_frontend_state.top_of_breed_list = 0;
  old_menu_mouse_x = -999;
  old_menu_mouse_y = -999;
  kfx_frontend_state.drag_menu_x = -999;
  kfx_frontend_state.drag_menu_y = -999;
  initialise_tab_tags(GMnu_ROOM);
  kfx_sim_state.new_objective = 0;
  input_button = 0;
  busy_doing_gui = 0;
  no_of_active_menus = 0;
}

void frontend_shutdown_state(FrontendMenuState pstate)
{
    char *fname;
    switch (pstate)
    {
    case FeSt_INITIAL:
        init_gui();
        fname = prepare_file_path(FGrp_LoData,"front.pal");
        if (LbFileLoadAt(fname, frontend_palette) != PALETTE_SIZE)
            ERRORLOG("Unable to load FRONTEND PALETTE");
        LbMoveGameCursorToHostCursor(); // set the initial cursor position for the main menu
        update_mouse();
        break;
    case FeSt_MAIN_MENU: // main menu state
        turn_off_menu(GMnu_FEMAIN);
        break;
    case FeSt_FELOAD_GAME:
        turn_off_menu(GMnu_FELOAD);
        break;
    case FeSt_LAND_VIEW:
        frontmap_unload();
        frontend_load_data();
        break;
    case FeSt_NET_SERVICE:
        turn_off_menu(GMnu_FENET_SERVICE);
        break;
    case FeSt_NET_SESSION: // Network play mode
        turn_off_menu(GMnu_FENET_SESSION);
        break;
    case FeSt_NET_START:
        LbStopTextInput();
        turn_off_menu(GMnu_FENET_START);
        break;
    case FeSt_STORY_POEM:
    case FeSt_STORY_BIRTHDAY:
        frontstory_unload();
        break;
    case FeSt_CREDITS:
        stop_music(true);
        break;
    case FeSt_LEVEL_STATS:
        stop_streamed_samples();
        turn_off_menu(GMnu_FESTATISTICS);
        break;
    case FeSt_HIGH_SCORES:
        turn_off_menu(GMnu_FEHIGH_SCORE_TABLE);
        break;
    case FeSt_TORTURE:
        set_pointer_graphic_none();
        fronttorture_clear_state();
        fronttorture_unload();
        frontend_load_data();
        break;
    case FeSt_NETLAND_VIEW:
        frontnetmap_unload();
        frontend_load_data();
        break;
    case FeSt_FEDEFINE_KEYS:
        turn_off_menu(GMnu_FEDEFINE_KEYS);
        save_settings();
        break;
    case FeSt_FEOPTIONS:
        turn_off_menu(GMnu_FEOPTION);
        stop_music(true);
        break;
    case FeSt_LEVEL_SELECT:
        turn_off_menu(GMnu_FELEVEL_SELECT);
        frontend_level_list_unload();
        break;
    case FeSt_MAPPACK_SELECT:
        turn_off_menu(GMnu_MAPPACK_SELECT);
        // Merged Free play screen's preview panel, same reasoning as
        // FeSt_CAMPAIGN_SELECT's below -- unload centrally here so every
        // way of leaving releases it.
        land_preview_unload(&land_preview);
        // Stops the mappack theme preview (frontend_play_campaign_preview_audio,
        // frontmenu_select.c) started on highlight -- without this, committing
        // ("Play"/"Enter this land" -> FeSt_START_KPRLEVEL) never passes through
        // FeSt_LAND_VIEW's own frontmap_unload() (which normally stops it), so
        // it would otherwise keep playing into actual gameplay.
        StopAllSamples();
        stop_music(false);
        break;
    case FeSt_CAMPAIGN_SELECT:
        turn_off_menu(GMnu_FECAMPAIGN_SELECT);
        // Land selection's preview panel owns loaded map art/ensign
        // sprites independent of this menu's own button teardown --
        // unload it centrally here, same as FeSt_LAND_VIEW's
        // frontmap_unload() elsewhere, so every way of leaving this
        // screen releases it.
        land_preview_unload(&land_preview);
        // See FeSt_MAPPACK_SELECT's own comment just above.
        StopAllSamples();
        stop_music(false);
        break;
    case FeSt_MP_MAPPACK_SELECT:
        turn_off_menu(GMnu_MP_MAPPACK_SELECT);
        break;
    case FeSt_START_KPRLEVEL:
    case FeSt_START_MPLEVEL:
    case FeSt_START_EDITOR:
    case FeSt_QUIT_GAME:
    case FeSt_LOAD_GAME:
    case FeSt_INTRO:
    case FeSt_DRAG:
    case FeSt_CAMPAIGN_INTRO:
    case FeSt_DEMO: //demo state (intro/credits)
    case FeSt_OUTRO:
    case FeSt_PACKET_DEMO:
        break;
#if (BFDEBUG_LEVEL > 0)
    case FeSt_FONT_TEST:
        free_testfont_fonts();
        break;
#endif
    default:
        ERRORLOG("Unhandled FRONTEND state %" PRId64 " shutdown",(int64_t)pstate);
        break;
    }
}

/**
 * Initializes given front-end state.
 * Loads required assets and initializes variables for each state.
 * Does not do network connection initialization for MP states.
 *
 * @param nstate The new state to be initialized.
 * @return The state which was really initialized.
 */
FrontendMenuState frontend_setup_state(FrontendMenuState nstate)
{
    SYNCDBG(9,"Starting for state %" PRId64,(int64_t)nstate);
    switch ( nstate )
    {
      case FeSt_INITIAL:
          set_pointer_graphic_none();
          break;
      case FeSt_MAIN_MENU:
          stop_music(true);
          kfx_frontend_state.continue_game_option_available = continue_game_available();
          if (!kfx_frontend_state.continue_game_option_available)
          {
              char* fname = prepare_file_path(FGrp_Save, continue_game_filename);
              LbFileDelete(fname);
          }
          if (!is_campaign_loaded()) {
              change_campaign(CampgnT_Default,"");
          }
          turn_on_menu(GMnu_FEMAIN);
          kfx_frontend_state.last_mouse_x = GetMouseX();
          kfx_frontend_state.last_mouse_y = GetMouseY();
          kfx_frontend_state.time_last_played_demo = LbTimerClock();
          fe_high_score_table_from_main_menu = true;
          clear_flag(kfx_sim_state.system_flags, GSF_NetworkActive);
          skip_high_score_screen = 0;
          set_pointer_graphic_menu();
          break;
      case FeSt_FELOAD_GAME:
          turn_on_menu(GMnu_FELOAD);
          set_pointer_graphic_menu();
          break;
      case FeSt_LAND_VIEW:
          set_pointer_graphic_none();
          if ( !frontmap_load() ) {
              // Fallback in case of error
                ERRORLOG("Failed to load campaign landview, going back to main menu");
                frontend_set_state(FeSt_MAIN_MENU);
                nstate = FeSt_MAIN_MENU;
          }
          break;
      case FeSt_NET_SERVICE:
          turn_on_menu(GMnu_FENET_SERVICE);
          frontnet_service_setup();
          set_pointer_graphic_menu();
          break;
      case FeSt_NET_SESSION:
          turn_on_menu(GMnu_FENET_SESSION);
          frontnet_session_setup();
          clear_flag(kfx_sim_state.system_flags, GSF_NetworkActive);
          set_pointer_graphic_menu();
          break;
      case FeSt_NET_START:
          turn_on_menu(GMnu_FENET_START);
          if (frontend_menu_state != FeSt_MP_MAPPACK_SELECT)
            frontnet_start_setup();
          LbStartTextInput();
          set_flag(kfx_sim_state.system_flags, GSF_NetworkActive);
          set_pointer_graphic_menu();
          break;
      // fade_palette_in cancellation removed here (and at every other write
      // site) per docs/refactor/renderer/05-imgui-owned-menu-backdrop.md
      // Phase 0 -- the mechanism it guarded is gone, so these states need
      // no special handling for it any more.
      case FeSt_START_KPRLEVEL:
      case FeSt_QUIT_GAME:
      case FeSt_LOAD_GAME:
      case FeSt_INTRO:
      case FeSt_DRAG:
      case FeSt_CAMPAIGN_INTRO:
      case FeSt_DEMO:
      case FeSt_OUTRO:
      case FeSt_PACKET_DEMO:
      case FeSt_START_MPLEVEL:
      case FeSt_START_EDITOR:
          break;
      case FeSt_STORY_POEM:
      case FeSt_STORY_BIRTHDAY:
          set_pointer_graphic_none();
          frontstory_load();
          break;
      case FeSt_CREDITS:
          // Item 5: the ImGui credits screen (frontgui_credits_frame()) now
          // has real controls (pack picker, Return button) -- unlike the
          // legacy crawl this used to be unconditionally, the cursor must
          // stay visible so they're usable. -classicmenu's crawl keeps
          // hiding it, same as the still-pure-text FeSt_STORY_POEM/
          // FeSt_STORY_BIRTHDAY cases just above.
          if (!frontend_imgui_screen_active(FeSt_CREDITS))
              set_pointer_graphic_none();
          credits_offset = lbDisplay.PhysicalScreenHeight;
          credits_end = 0;
          LbTextSetWindow(0, 0, lbDisplay.PhysicalScreenWidth, lbDisplay.PhysicalScreenHeight);
          RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
          play_music_track(7);
          break;
      case FeSt_LEVEL_STATS:
          turn_on_menu(GMnu_FESTATISTICS);
          frontstats_set_timer();
          set_pointer_graphic_menu();
          break;
      case FeSt_HIGH_SCORES:
          turn_on_menu(GMnu_FEHIGH_SCORE_TABLE);
          frontstats_save_high_score();
          set_pointer_graphic_menu();
          break;
      case FeSt_TORTURE:
          set_pointer_graphic_none();
          fronttorture_load();
          break;
      case FeSt_NETLAND_VIEW:
          set_pointer_graphic_none();
          if(!frontnetmap_load())
          {
                ERRORLOG("Failed to load netmap, going back to main menu");
                frontend_set_state(FeSt_MAIN_MENU);
                nstate = FeSt_MAIN_MENU;
          }
          break;
      case FeSt_FEDEFINE_KEYS:
          defining_a_key = 0;
          kfx_frontend_state.define_key_scroll_offset = 0;
          turn_on_menu(GMnu_FEDEFINE_KEYS);
          break;
      case FeSt_FEOPTIONS:
          play_music_track(3);
          turn_on_menu(GMnu_FEOPTION);
          set_pointer_graphic_menu();
          break;
    case FeSt_LEVEL_SELECT:
        turn_on_menu(GMnu_FELEVEL_SELECT);
        frontend_level_list_load();
        set_pointer_graphic_menu();
        break;
    case FeSt_MAPPACK_SELECT:
        turn_on_menu(GMnu_MAPPACK_SELECT);
        frontend_mappack_list_load();
        set_pointer_graphic_menu();
        break;
    case FeSt_CAMPAIGN_SELECT:
        turn_on_menu(GMnu_FECAMPAIGN_SELECT);
        frontend_campaign_list_load();
        set_pointer_graphic_menu();
        break;
    case FeSt_MP_MAPPACK_SELECT:
        turn_on_menu(GMnu_MP_MAPPACK_SELECT);
        frontend_mp_mappack_list_load();
        set_pointer_graphic_menu();
        break;
  #if (BFDEBUG_LEVEL > 0)
    case FeSt_FONT_TEST:
        load_testfont_fonts();
        set_pointer_graphic_menu();
        break;
  #endif
      default:
        ERRORLOG("Unhandled FRONTEND new state");
        break;
    }
    return nstate;
}

static const char * menu_state_str(FrontendMenuState state)
{
    switch (state) {
        case FeSt_INITIAL: return "FeSt_INITIAL";
        case FeSt_MAIN_MENU: return "FeSt_MAIN_MENU";
        case FeSt_FELOAD_GAME: return "FeSt_FELOAD_GAME";
        case FeSt_LAND_VIEW: return "FeSt_LAND_VIEW";
        case FeSt_NET_SERVICE: return "FeSt_NET_SERVICE";
        case FeSt_NET_SESSION: return "FeSt_NET_SESSION";
        case FeSt_NET_START: return "FeSt_NET_START";
        case FeSt_START_KPRLEVEL: return "FeSt_START_KPRLEVEL";
        case FeSt_START_MPLEVEL: return "FeSt_START_MPLEVEL";
        case FeSt_QUIT_GAME: return "FeSt_QUIT_GAME";
        case FeSt_LOAD_GAME: return "FeSt_LOAD_GAME";
        case FeSt_INTRO: return "FeSt_INTRO";
        case FeSt_STORY_POEM: return "FeSt_STORY_POEM";
        case FeSt_CREDITS: return "FeSt_CREDITS";
        case FeSt_DEMO: return "FeSt_DEMO";
        case FeSt_UNUSED1: return "FeSt_UNUSED1";
        case FeSt_UNUSED2: return "FeSt_UNUSED2";
        case FeSt_LEVEL_STATS: return "FeSt_LEVEL_STATS";
        case FeSt_HIGH_SCORES: return "FeSt_HIGH_SCORES";
        case FeSt_TORTURE: return "FeSt_TORTURE";
        case FeSt_UNUSED_STATE1: return "FeSt_UNUSED_STATE1";
        case FeSt_OUTRO: return "FeSt_OUTRO";
        case FeSt_UNUSED_STATE2: return "FeSt_UNUSED_STATE2";
        case FeSt_UNUSED_STATE3: return "FeSt_UNUSED_STATE3";
        case FeSt_NETLAND_VIEW: return "FeSt_NETLAND_VIEW";
        case FeSt_PACKET_DEMO: return "FeSt_PACKET_DEMO";
        case FeSt_FEDEFINE_KEYS: return "FeSt_FEDEFINE_KEYS";
        case FeSt_FEOPTIONS: return "FeSt_FEOPTIONS";
        case FeSt_UNUSED_STATE4: return "FeSt_UNUSED_STATE4";
        case FeSt_STORY_BIRTHDAY: return "FeSt_STORY_BIRTHDAY";
        case FeSt_LEVEL_SELECT: return "FeSt_LEVEL_SELECT";
        case FeSt_CAMPAIGN_SELECT: return "FeSt_CAMPAIGN_SELECT";
        case FeSt_DRAG: return "FeSt_DRAG";
        case FeSt_CAMPAIGN_INTRO: return "FeSt_CAMPAIGN_INTRO";
        case FeSt_MAPPACK_SELECT: return "FeSt_MAPPACK_SELECT";
        case FeSt_MP_MAPPACK_SELECT: return "FeSt_MP_MAPPACK_SELECT";
        case FeSt_START_EDITOR: return "FeSt_START_EDITOR";
        case FeSt_FONT_TEST: return "FeSt_FONT_TEST";
    }
    return "unknown";
}

FrontendMenuState frontend_set_state(FrontendMenuState nstate)
{
    SYNCDBG(8,"State %" PRId64 " will be switched to %" PRId64,(int64_t)frontend_menu_state,(int64_t)nstate);
    frontend_shutdown_state(frontend_menu_state);
    // fade_out()/fade_palette_in's fade_in() trigger (game_session_loop.cpp)
    // removed per docs/refactor/renderer/05-imgui-owned-menu-backdrop.md
    // Phase 0 -- the effect existed to mask loading time on decades-old
    // hardware and is no longer needed; dropped rather than replaced.
    SYNCMSG("Frontend state change from %" PRId64 " (%s) into %" PRId64 " (%s)",
        (int64_t)(frontend_menu_state), menu_state_str(frontend_menu_state),
        (int64_t)(nstate), menu_state_str(nstate));
    frontend_menu_state = frontend_setup_state(nstate);
    return frontend_menu_state;
}

TbBool frontmainmnu_input(void)
{
    int64_t mouse_x;
    int64_t mouse_y;
    // check if mouse position has changed
    mouse_x = GetMouseX();
    mouse_y = GetMouseY();
    if ((mouse_x != kfx_frontend_state.last_mouse_x) || (mouse_y != kfx_frontend_state.last_mouse_y))
    {
        kfx_frontend_state.last_mouse_x = mouse_x;
        kfx_frontend_state.last_mouse_y = mouse_y;
        kfx_frontend_state.time_last_played_demo = LbTimerClock();
    }
    // Handle key inputs
    if (lbKeyOn[KC_G] && lbKeyOn[KC_LSHIFT])
    {
        lbKeyOn[KC_G] = 0;
        frontend_set_state(FeSt_CREDITS);
        return true;
    }
    if (lbKeyOn[KC_T] && lbKeyOn[KC_LSHIFT])
    {
        if (kfx_sim_state.easter_eggs_enabled == true)
        {
            lbKeyOn[KC_T] = 0;
            set_player_as_won_level(get_my_player());
            frontend_set_state(FeSt_TORTURE);
            return true;
        }
    }
#if (BFDEBUG_LEVEL > 0)
    if (lbKeyOn[KC_F] && lbKeyOn[KC_LSHIFT])
    {
        if (kfx_sim_state.easter_eggs_enabled == true)
        {
            lbKeyOn[KC_F] = 0;
            frontend_set_state(FeSt_FONT_TEST);
            return true;
        }
    }
#endif
    return false;
}

TbBool front_continue_pressed(TbBool force)
{
    TbBool got_input;
    got_input = force;
    if (lbKeyOn[KC_SPACE])
    {
        lbKeyOn[KC_SPACE] = 0;
        got_input = true;
    }
    if (lbKeyOn[KC_RETURN])
    {
        lbKeyOn[KC_RETURN] = 0;
        got_input = true;
    }
    if (left_button_clicked)
    {
        left_button_clicked = 0;
        got_input = true;
    }
    return got_input;
}

TbBool frontscreen_end_input(TbBool force)
{
    TbBool do_change;
    do_change = force;
    if (lbKeyOn[KC_ESCAPE])
    {
        lbKeyOn[KC_ESCAPE] = 0;
        do_change = true;
    }
    if (right_button_clicked)
    {
        right_button_clicked = 0;
        do_change = true;
    }
    if (do_change)
    {
        FrontendMenuState nstate;
        nstate = get_menu_state_when_back_from_substate(frontend_menu_state);
        if (nstate != frontend_menu_state) {
            frontend_set_state(nstate);
        } else {
            do_change = false;
        }
    }
    return do_change;
}

int64_t get_frontend_global_inputs(void)
{
    if (is_game_key_pressed(Gkey_ExitGame, true ,false))
    {
        exit_keeper = true;
    } else {
        return false;
    }
    return true;
}

void frontend_input(void)
{
    SYNCDBG(7,"Starting");
    TbBool input_consumed;
    input_consumed = false;
    switch (frontend_menu_state)
    {
    case FeSt_MAIN_MENU:
        // frontmainmnu_input() is pure idle-timer/secret-key-combo state,
        // no GuiButton involved (see its own body) -- runs unconditionally,
        // same as define_key_input()'s own always-runs reasoning
        // (FeSt_FEDEFINE_KEYS below).
        if (!frontend_imgui_screen_active(FeSt_MAIN_MENU))
            get_gui_inputs(0);
        input_consumed = frontscreen_end_input(false);
        if (input_consumed) {
            break;
        }
        input_consumed = frontmainmnu_input();
        break;
    case FeSt_LAND_VIEW:
        frontmap_input();
        break;
    case FeSt_NET_SERVICE:
        if (!frontend_imgui_screen_active(FeSt_NET_SERVICE))
            get_gui_inputs(0);
        input_consumed = frontscreen_end_input(false);
        break;
    case FeSt_NET_SESSION:
        if (!frontend_imgui_screen_active(FeSt_NET_SESSION))
            get_gui_inputs(0);
        break;
    case FeSt_NET_START:
        // frontnet_start_input() edits player->mp_message_text directly
        // (backspace/UTF-8 splice, Enter-to-send) -- while ImGui owns this
        // screen, FeTextInput (frontgui_screens.cpp) owns that same buffer
        // instead, so both must not run together (§8: "a given
        // FrontendMenuState belongs entirely to one system"), same
        // reasoning as FeSt_HIGH_SCORES' own high_score_entry gating below.
        if (!frontend_imgui_screen_active(FeSt_NET_START))
        {
            get_gui_inputs(0);
            frontnet_start_input();
        }
        break;
    case FeSt_STORY_POEM:
    case FeSt_STORY_BIRTHDAY:
        input_consumed = frontscreen_end_input(front_continue_pressed(false));
        if (input_consumed) {
            break;
        }
        input_consumed = frontstory_input();
        break;
    case FeSt_CREDITS:
        // Unlike FeSt_STORY_POEM/_BIRTHDAY just above (still pure "click
        // anywhere to continue" text, no widgets), the ImGui credits screen
        // (frontgui_credits_frame()) now has a pack-picker combo and a
        // Return button -- front_continue_pressed()'s unconditional
        // left_button_clicked check would otherwise fire on every click
        // meant for those widgets and immediately exit the screen. Gated
        // the same way FeSt_HIGH_SCORES/FeSt_NET_START already are.
        if (!frontend_imgui_screen_active(FeSt_CREDITS))
        {
            input_consumed = frontscreen_end_input(front_continue_pressed(credits_end));
            if (input_consumed) {
                break;
            }
            frontcredits_input();
        }
        break;
    case FeSt_HIGH_SCORES:
        // §8: belongs entirely to one system while migrated -- ImGui's
        // InputText (frontgui_screens.cpp) owns the name-entry buffer
        // directly, so frontend_high_score_table_input()'s manual UTF-8
        // splice logic must not also run against the same high_score_entry
        // (it would fight ImGui's own cursor/edit state over the same
        // buffer).
        if (!frontend_imgui_screen_active(FeSt_HIGH_SCORES))
        {
            get_gui_inputs(0);
            if (high_score_entry_input_active < 0) {
                input_consumed = frontscreen_end_input(false);
            }
            if (input_consumed) {
                break;
            }
            input_consumed = frontend_high_score_table_input();
        }
        break;
    case FeSt_FEOPTIONS:
        // §3.3 point 1 / §8: "a given FrontendMenuState belongs entirely to
        // one system" -- when ImGui owns this screen, the legacy
        // frontend_option_buttons[] GuiButtons are invisible (draw_gui()
        // skipped above) but would otherwise still be live hit-test
        // targets; skip get_gui_inputs(0) entirely rather than only
        // WantCaptureMouse-gating it, since nothing here needs the legacy
        // widgets interactive at the same time as their ImGui replacement.
        if (!frontend_imgui_screen_active(FeSt_FEOPTIONS))
            get_gui_inputs(0);
        input_consumed = frontscreen_end_input(false);
        break;
    case FeSt_FELOAD_GAME:
        if (!frontend_imgui_screen_active(FeSt_FELOAD_GAME))
            get_gui_inputs(0);
        input_consumed = frontscreen_end_input(false);
        break;
    case FeSt_CAMPAIGN_SELECT:
        // FrontendImGuiLandPreviewInput must run before
        // frontscreen_end_input -- see its own comment (frontgui_screens.cpp)
        // for why the ordering matters (a right-click meant to clear the
        // preview's ensign highlight must not also trigger "go back").
        FrontendImGuiLandPreviewInput(FeSt_CAMPAIGN_SELECT);
        if (!frontend_imgui_screen_active(FeSt_CAMPAIGN_SELECT))
            get_gui_inputs(0);
        input_consumed = frontscreen_end_input(false);
        break;
    case FeSt_MAPPACK_SELECT:
        FrontendImGuiLandPreviewInput(FeSt_MAPPACK_SELECT);
        if (!frontend_imgui_screen_active(FeSt_MAPPACK_SELECT))
            get_gui_inputs(0);
        // Skirmish's Setup tab uses right-click on its tiles (off / -1): don't let the generic
        // "right-click = back" in frontscreen_end_input() also fire while the mouse is over it.
        if (frontend_freeplay_is_skirmish() && frontgui_skirmish_setup_captures_right_click())
            right_button_clicked = 0;
        input_consumed = frontscreen_end_input(false);
        break;
    case FeSt_MP_MAPPACK_SELECT:
        if (!frontend_imgui_screen_active(FeSt_MP_MAPPACK_SELECT))
            get_gui_inputs(0);
        input_consumed = frontscreen_end_input(false);
        break;
    case FeSt_LEVEL_STATS:
        if (!frontend_imgui_screen_active(FeSt_LEVEL_STATS))
            get_gui_inputs(0);
        input_consumed = frontscreen_end_input(false);
        break;
    case FeSt_TORTURE:
        fronttorture_input();
        break;
    case FeSt_NETLAND_VIEW:
        frontnetmap_input();
        break;
    case FeSt_FEDEFINE_KEYS:
        // define_key_input() always runs while capturing -- it's pure
        // lbInkey/defining_a_key* global state, no GuiButton involved, so
        // both draw paths share it unchanged. Only the row-list interaction
        // (get_gui_inputs(0), the legacy frontend_define_keys_buttons[]
        // hit-testing) needs the migration gate.
        if (!defining_a_key) {
            if (!frontend_imgui_screen_active(FeSt_FEDEFINE_KEYS))
                get_gui_inputs(0);
            input_consumed = frontscreen_end_input(false);
        } else {
            define_key_input();
            input_consumed = true;
        }
        break;
#if (BFDEBUG_LEVEL > 0)
    case FeSt_FONT_TEST:
        get_gui_inputs(0);
        input_consumed = frontscreen_end_input(false);
        if (input_consumed) {
            break;
        }
        fronttestfont_input();
        break;
#endif
    default:
        get_gui_inputs(0);
        input_consumed = frontscreen_end_input(false);
        break;
    } // end switch
    get_frontend_global_inputs();
    if (!input_consumed) {
        get_screen_capture_inputs();
    }
    SYNCDBG(19,"Finished");
}

void draw_defining_a_key_box(void)
{
    draw_text_box(get_string(GUIStr_PressAKey));
}

char update_menu_fade_level(struct GuiMenu *gmnu)
{
    switch (gmnu->visual_state)
    {
    case 1: // Fade in
        if (gmnu->fade_time-1.0 <= 0.0)
        {
            gmnu->fade_time = gmnu->menu_init->fade_time;
            gmnu->visual_state = 2;
            return 0;
        }
        if (kfx_net_state.frame_skip == 0)
        {
            gmnu->fade_time -= kfx_render_state.delta_time;
        } else {
            gmnu->fade_time -= 1.0;
        }
        return 0;
    case 3: // Fade out
        if (gmnu->fade_time-1.0 <= 0.0)
        {
            gmnu->fade_time = 0.0;
            return -1; // Kill menu
        }
        if (kfx_net_state.frame_skip == 0)
        {
            gmnu->fade_time -= kfx_render_state.delta_time;
        } else {
            gmnu->fade_time -= 1.0;
        }
        return 0;
    default:
        break;
    }
    return 0;
}

void toggle_gui_overlay_map(void)
{
    toggle_flag(kfx_sim_state.operation_flags, GOF_ShowGui);
}

void draw_menu_buttons(struct GuiMenu *gmnu)
{
    int64_t i;
    struct GuiButton *gbtn;
    Gf_Btn_Callback callback;
    SYNCDBG(18,"Starting phase one");
    for (i=0; i<ACTIVE_BUTTONS_COUNT; i++)
    {
        gbtn = &active_buttons[i];
        callback = gbtn->draw_call;
        if ((callback != NULL) && (gbtn->flags & LbBtnF_Visible) && (gbtn->flags & LbBtnF_Active) && (gbtn->gmenu_idx == gmnu->number))
        {
          if ( ((gbtn->button_state_left_pressed == 0) && (gbtn->button_state_right_pressed == 0)) || (gbtn->gbtype == LbBtnT_HorizSlider) || (callback == gui_area_null) )
            callback(gbtn);
        }
    }
    SYNCDBG(18,"Starting phase two");
    for (i=0; i<ACTIVE_BUTTONS_COUNT; i++)
    {
        gbtn = &active_buttons[i];
        callback = gbtn->draw_call;
        if ((callback != NULL) && (gbtn->flags & LbBtnF_Visible) && (gbtn->flags & LbBtnF_Active) && (gbtn->gmenu_idx == gmnu->number))
        {
          if (((gbtn->button_state_left_pressed) || (gbtn->button_state_right_pressed)) && (gbtn->gbtype != LbBtnT_HorizSlider) && (callback != gui_area_null))
            callback(gbtn);
        }
    }
    SYNCDBG(19,"Finished");
}

void update_fade_active_menus(void)
{
    SYNCDBG(8,"Starting");
    struct GuiMenu *gmnu;
    int64_t k;
    for (k=0; k < ACTIVE_MENUS_COUNT; k++)
    {
        gmnu = &active_menus[k];
        if (update_menu_fade_level(gmnu) == -1)
        {
            kill_menu(gmnu);
            remove_from_menu_stack(gmnu->ident);
        }
    }
    SYNCDBG(19,"Finished");
}

void draw_active_menus_buttons(void)
{
    struct GuiMenu *gmnu;
    int64_t k;
    int64_t menu_num;
    Gf_Mnu_Callback callback;
    SYNCDBG(8,"Starting with %" PRId64 " active menus",(int64_t)(no_of_active_menus));
    for (k=0; k < no_of_active_menus; k++)
    {
        menu_num = menu_id_to_number(menu_stack[k]);
        if (menu_num < 0) continue;
        // docs/refactor/ingame-gui/ Phase 0: ImGui submits this menu
        // (FrontendImGuiFrame -> ingame_imgui_frame); skip the sprite draw.
        if (ingame_imgui_menu_active(menu_stack[k])) continue;
        gmnu = &active_menus[menu_num];
        //SYNCMSG("DRAW menu %d, fields %d, %d",(int64_t)(menu_num),(int64_t)(gmnu->visual_state),(int64_t)(gmnu->is_turned_on));
        if ((gmnu->visual_state != 0) && (gmnu->is_turned_on))
        {
            if ((gmnu->visual_state != 2) && (gmnu->fade_time > 0))
            {
              if (gmnu->menu_init != NULL)
                if (gmnu->menu_init->fade_time)
                  RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
            }
            callback = gmnu->draw_cb;
            if (callback != NULL)
              callback(gmnu);
            if (gmnu->visual_state == 2)
              draw_menu_buttons(gmnu);
            RendererClearDrawFlags(Lb_SPRITE_TRANSPAR4);
        }
    }
    SYNCDBG(9,"Finished");
}

void spangle_button(struct GuiButton *gbtn)
{
    const struct TbSprite *spr;
    spr = get_button_sprite(GBS_guisymbols_new_function_1);
    int64_t bs_units_per_px;
    bs_units_per_px = 50 * units_per_pixel / spr->SHeight;
    int64_t x;
    int64_t y;
    uint64_t i;
    x = gbtn->pos_x + (gbtn->width >> 1)  - ((spr->SWidth*bs_units_per_px/16) / 2);
    y = gbtn->pos_y + (gbtn->height >> 1) - ((spr->SHeight*bs_units_per_px/16) / 2);
    i = GBS_guisymbols_new_function_1+((get_gameturn() >> 1) & 7);
    spr = get_button_sprite(i);
    LbSpriteDrawResized(x, y, bs_units_per_px, spr);
}

void draw_menu_spangle(struct GuiMenu *gmnu)
{
    if (gmnu->is_turned_on == 0)
      return;
    for (int64_t i = 0; i < ACTIVE_BUTTONS_COUNT; i++)
    {
        struct GuiButton *gbtn = &active_buttons[i];
        if ((gbtn->draw_call == NULL) || ((gbtn->flags & LbBtnF_Visible) == 0) || ((gbtn->flags & LbBtnF_Active) == 0) || (kfx_frontend_state.flash_button_index == 0))
          continue;
        if ((gbtn->id_num > BID_DEFAULT) && (gbtn->id_num == button_designation_to_tab_designation(kfx_frontend_state.flash_button_index)))
        {
            // Button is a tab header; spangle if the tab is not active
            MenuNumber idx = gbtn->btype_value & LbBFeF_IntValueMask;
            if (idx == GMnu_SPELL)
            {
                if ( (kfx_frontend_state.flash_button_index >= BID_POWER_TD17) && (kfx_frontend_state.flash_button_index <= BID_POWER_TD32) )
                {
                    idx = GMnu_SPELL2;
                }
            }
            else if (idx == GMnu_ROOM)
            {
                if ( (kfx_frontend_state.flash_button_index >= BID_ROOM_TD17) && (kfx_frontend_state.flash_button_index <= BID_ROOM_TD32) )
                {
                    idx = GMnu_ROOM2;
                }
            }
            else if (idx == GMnu_TRAP)
            {
                if ( (kfx_frontend_state.flash_button_index >= BID_MNFCT_TD17) && (kfx_frontend_state.flash_button_index <= BID_MNFCT_TD32) )
                {
                    idx = GMnu_TRAP2;
                }
            }
            if (!menu_is_active(idx))
            {
                spangle_button(gbtn);
            }
        } else
        if ((gbtn->id_num > BID_DEFAULT) && (gbtn->id_num == kfx_frontend_state.flash_button_index))
        {
            spangle_button(gbtn);
        }
    }
}

void draw_active_menus_highlights(void)
{
    struct GuiMenu *gmnu;
    int64_t k;
    SYNCDBG(8,"Starting");
    for (k=0; k<ACTIVE_MENUS_COUNT; k++)
    {
        gmnu = &active_menus[k];
        // Missed by the same skip draw_active_menus_buttons() above already
        // applies: spangle_button() positions itself from the legacy
        // gbtn->pos_x/pos_y, baked at create_menu() time against GMnu_MAIN's
        // always-flush-left legacy position -- so with GUI_POSITION set to
        // Right, this sparkle stayed stranded at the old left-edge spot
        // while frontgui_ingame_panel.cpp's own ImGui spangle (draw_tabs())
        // correctly followed the panel. That ImGui spangle is this one's
        // replacement once GMnu_MAIN is migrated -- skip the legacy draw
        // rather than fix its position, so the two don't both render.
        if ((gmnu->visual_state != 0) && (gmnu->ident == GMnu_MAIN) && !ingame_imgui_menu_active(gmnu->ident))
          draw_menu_spangle(gmnu);
    }
}

void draw_gui(void)
{
    SYNCDBG(6,"Starting");
    uint64_t flg_mem;
    LbTextSetFont(winfont);
    flg_mem = RendererGetDrawFlags();
    LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
    update_fade_active_menus();
    draw_active_menus_buttons();
    if (kfx_frontend_state.flash_button_index != 0)
    {
        draw_active_menus_highlights();
        if (kfx_frontend_state.flash_button_time > 0)
        {
            kfx_frontend_state.flash_button_time -= kfx_render_state.delta_time;
            if (kfx_frontend_state.flash_button_time <= 0) {
                kfx_frontend_state.flash_button_index = 0;
            }
        }
    }
    RendererSetDrawFlags(flg_mem);
    SYNCDBG(8,"Finished");
}

void draw_debug_messages() {
    LbTextSetFont(frontend_font[0]);
    LbTextSetWindow(0, 0, 640, 200);
    RendererSetDrawFlags(0);
    const int64_t x = 8 / pixel_size;
    int64_t y = 8 / pixel_size;
    for (auto message = debug_messages_head; message != nullptr; ) {
        LbTextDraw(x, y, message->text);
        y += 32 / pixel_size;
        const auto next = message->next;
        free(message);
        message = next;
    }
    debug_messages_head = nullptr;
    debug_messages_tail = &debug_messages_head;
}

/**
 * Frontend drawing function.
 * @return Gives 0 if a movie has started, 1 if normal draw occured, 2 on error.
 */
int64_t frontend_draw(void)
{
    poll_inputs();
    int64_t result;
    switch (frontend_menu_state)
    {
    case FeSt_INTRO:
        intro();
        return 0;
    case FeSt_DEMO:
        demo();
        return 0;
    case FeSt_CAMPAIGN_INTRO:
        campaign_intro();
        return 0;
    case FeSt_DRAG:
        drag_video();
        return 0;
    case FeSt_OUTRO:
        campaign_outro();
        return 0;
    }

    if (RendererLockFramebuffer() != Lb_SUCCESS)
        return 2;

    result = 1;
    switch ( frontend_menu_state )
    {
    case FeSt_UNUSED_STATE1:
    case FeSt_LEVEL_SELECT: // dead (frontmenu_select.h) -- unreachable, left in this group harmlessly
        frontend_copy_background();
        draw_gui();
        break;
    case FeSt_FEOPTIONS:
    case FeSt_FELOAD_GAME:
    case FeSt_HIGH_SCORES:
    case FeSt_MAPPACK_SELECT:
    case FeSt_CAMPAIGN_SELECT:
    case FeSt_MP_MAPPACK_SELECT:
    case FeSt_MAIN_MENU:
    case FeSt_LEVEL_STATS:
    case FeSt_NET_SERVICE:
    case FeSt_NET_SESSION:
    case FeSt_NET_START:
        // docs/refactor/renderer/05-imgui-owned-menu-backdrop.md Phase D:
        // the backdrop used to stay on the software path even when
        // migrated (§3.3 point 2/§3.4 of the other doc) -- only draw_gui()
        // (the legacy widgets) was replaced, by FrontendImGuiFrame's
        // per-screen submission. Once RendererSoftware::PresentFrame()
        // stops blitting the legacy framebuffer for these screens (Phase
        // C), drawing frontend_copy_background() into it here is pure
        // wasted work -- draw_menu_backdrop() (frontgui_screens.cpp) draws
        // the same image via ImGui instead. Phase E's master-detail
        // screens additionally render their land preview panel through the
        // software path too, but off-screen and from inside that later
        // ImGui submission (not from here) -- see draw_land_preview_panel's
        // own comment (frontgui_screens.cpp).
        if (!frontend_imgui_screen_active(frontend_menu_state))
        {
            frontend_copy_background();
            draw_gui();
        }
        break;
    case FeSt_LAND_VIEW:
        frontmap_draw();
        break;
    case FeSt_STORY_POEM:
        // Phase D: was frontend_copy_background() when migrated -- no
        // longer needed once the framebuffer blit itself is skipped
        // (Phase C) for this screen; draw_menu_backdrop() covers it.
        if (!frontend_imgui_screen_active(FeSt_STORY_POEM))
            frontstory_draw(); // calls frontend_copy_background() itself
        break;
    case FeSt_CREDITS:
        if (!frontend_imgui_screen_active(FeSt_CREDITS))
            frontcredits_draw(); // calls frontend_copy_background() itself
        break;
    case FeSt_TORTURE:
        fronttorture_draw();
        break;
    case FeSt_NETLAND_VIEW:
        frontnetmap_draw();
        break;
    case FeSt_FEDEFINE_KEYS:
        // Phase D: was an unconditional frontend_copy_background() -- see
        // the migrated-group comment above for why that's gone now.
        if (!frontend_imgui_screen_active(FeSt_FEDEFINE_KEYS))
        {
            frontend_copy_background();
            draw_gui();
            if ( defining_a_key )
                draw_defining_a_key_box();
        }
        // migrated: frontgui_definekeys_frame() draws both the row list and
        // its own "press a key" modal (§7 Phase D: "deletes the twelve-row
        // pattern, and brings draw_defining_a_key_box with it").
        break;
    case FeSt_STORY_BIRTHDAY:
        if (!frontend_imgui_screen_active(FeSt_STORY_BIRTHDAY))
            frontbirthday_draw(); // calls frontend_copy_background() itself
        break;
#if (BFDEBUG_LEVEL > 0)
    case FeSt_FONT_TEST:
        fronttestfont_draw();
        break;
#endif
    default:
        break;
    }
    draw_debug_messages();
    perform_any_screen_capturing();
    RendererUnlockFramebuffer();
    return result;
}

void load_game_update(void)
{
    if ((number_of_saved_games>0) && (load_game_scroll_offset>=0))
    {
        if ( load_game_scroll_offset > number_of_saved_games-1 )
          load_game_scroll_offset = number_of_saved_games-1;
    } else
    {
        load_game_scroll_offset = 0;
    }
}

void gui_set_autopilot(struct GuiButton *gbtn)
{
  struct PlayerInfo *player;
  player = get_my_player();
  int64_t ntype;
  if (kfx_net_state.comp_player_aggressive)
  {
    ntype = 1;
  } else
  if (kfx_net_state.comp_player_defensive)
  {
    ntype = 2;
  } else
  if (kfx_net_state.comp_player_construct)
  {
    ntype = 3;
  } else
  if (kfx_net_state.comp_player_creatrsonly)
  {
    ntype = 4;
  } else
  {
    ERRORLOG("Illegal Autopilot type, resetting to default");
    ntype = 1;
  }
  set_players_packet_action(player, PckA_SetComputerKind, ntype, 0, 0, 0);
}

void set_level_objective(PlayerNumber plyr_idx, const char *msg_text)
{
    if (msg_text == NULL)
    {
        ERRORLOG("Invalid message pointer");
        return;
    }
    snprintf(kfx_sim_state.evntbox_text_objective[plyr_idx], MESSAGE_TEXT_LEN, "%s", msg_text);
    if (is_my_player_number(plyr_idx))
    {
        kfx_sim_state.new_objective = 1;
    }
}

void update_player_objectives(PlayerNumber plyr_idx)
{
    struct PlayerInfo *player;
    SYNCDBG(6,"Starting for player %" PRId64,(int64_t)plyr_idx);
    player = get_player(plyr_idx);
    if (network_is_active())
    {
      if ((!player->display_objective_turn) && (player->victory_state != VicS_Undecided))
        player->display_objective_turn = get_gameturn()+1;
    }
    if (player->display_objective_turn == get_gameturn())
    {
      switch (player->victory_state)
      {
      case VicS_WonLevel:
          set_level_objective(player->id_number,get_string(CpgStr_SuccessLandIsYours));
          display_objectives(player->id_number, 0, 0);
          break;
      case VicS_LostLevel:
          TextStringId msg_idx = CpgStr_LevelLost;
          if (network_is_active() && (player->id_number == get_net_user_player_number(SERVER_ID)) && network_human_contenders_remain()) {
              msg_idx = GUIStr_NetHostLostWaitingForPlayers;
          }
          set_level_objective(player->id_number, get_string(msg_idx));
          display_objectives(player->id_number, 0, 0);
          break;
      }
    }
}

void display_objectives(PlayerNumber plyr_idx, MapSubtlCoord x, MapSubtlCoord y)
{
    display_objectives_with_icon(plyr_idx, x, y, -1);
}

void display_objectives_with_icon(PlayerNumber plyr_idx, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx)
{
    MapCoord cor_x;
    MapCoord cor_y;
    cor_y = 0;
    cor_x = 0;
    if ((x > 0) || (y > 0))
    {
        cor_x = subtile_coord_center(x);
        cor_y = subtile_coord_center(y);
    }
    int64_t evbtn_idx;
    for (evbtn_idx=0; evbtn_idx < EVENT_BUTTONS_COUNT+1; evbtn_idx++)
    {
        struct Dungeon *dungeon;
        dungeon = get_players_num_dungeon(plyr_idx);
        EventIndex evidx;
        evidx = dungeon->event_button_index[evbtn_idx];
        struct Event *event;
        event = &kfx_sim_state.event[evidx];
        if (event->kind == EvKind_Objective)
        {
            event_create_event_or_update_old_event(cor_x, cor_y, EvKind_Objective, plyr_idx, 0);
            kfx_sim_state.event[evidx].icon_idx = icon_idx;
            return;
        }
    }
    if ((x == 255) && (y == 255))
    {
        struct Thing *creatng = lord_of_the_land_find();
        if (thing_exists(creatng))
        {
            cor_x = creatng->mappos.x.val;
            cor_y = creatng->mappos.y.val;
        }
        event_create_event_or_update_old_event(cor_x, cor_y, EvKind_Objective, plyr_idx, creatng->index);
        struct Event *event = get_event_of_type_for_player(
            EvKind_Objective, plyr_idx);

        if (!event_is_invalid(event))
        {
            event->icon_idx = icon_idx;
        }
    } else
    {
        event_create_event_or_update_old_event(cor_x, cor_y, EvKind_Objective, plyr_idx, 0);
        struct Event *event = get_event_of_type_for_player(
            EvKind_Objective, plyr_idx);

        if (!event_is_invalid(event))
        {
            event->icon_idx = icon_idx;
        }
    }
}

void frontend_update(int64_t *finish_menu)
{
    SYNCDBG(18,"Starting for menu state %" PRId64, (int64_t)frontend_menu_state);
    switch ( frontend_menu_state )
    {
    case FeSt_MAIN_MENU:
        frontend_button_info[8].font_index = (kfx_frontend_state.continue_game_option_available?1:3);
        //this uses original timing function for compatibility with frontend_set_state()
        if ( abs(LbTimerClock()-(int64_t)kfx_frontend_state.time_last_played_demo) > MNU_DEMO_IDLE_TIME )
          frontend_set_state(FeSt_DEMO);
        break;
    case FeSt_FELOAD_GAME:
        load_game_update();
        break;
    case FeSt_LAND_VIEW:
        *finish_menu = frontmap_update();
        break;
    case FeSt_CAMPAIGN_INTRO:
        break;
    case FeSt_NET_SERVICE:
        frontnet_service_update();
        break;
    case FeSt_NET_SESSION:
        frontnet_session_update();
        break;
    case FeSt_NET_START:
        frontnet_start_update();
        break;
    case FeSt_START_KPRLEVEL:
    case FeSt_START_MPLEVEL:
    case FeSt_START_EDITOR:
    case FeSt_LOAD_GAME:
    case FeSt_PACKET_DEMO:
        *finish_menu = 1;
        break;
    case FeSt_QUIT_GAME:
        *finish_menu = 1;
        exit_keeper = 1;
        break;
    case FeSt_CREDITS:
        break;
    case FeSt_LEVEL_STATS:
        frontstats_update();
        break;
    case FeSt_TORTURE:
        fronttorture_update();
        break;
    case FeSt_NETLAND_VIEW:
        *finish_menu = frontnetmap_update();
        break;
    case FeSt_FEOPTIONS:
        break;
    case FeSt_LEVEL_SELECT:
        frontend_level_select_update();
        break;
    case FeSt_CAMPAIGN_SELECT:
        frontend_campaign_select_update();
        break;
    case FeSt_MAPPACK_SELECT:
        frontend_mappack_select_update();
        break;
    case FeSt_MP_MAPPACK_SELECT:
        // Used only for real multiplayer sessions now -- Skirmish no
        // longer reaches this state (see frontend_mp_mappack_select_resolve's
        // own comment, frontmenu_select.c), so frontnet_start_update()
        // (keeps the lobby's matchmaking/session polling alive while
        // picking a mappack) always applies here.
        frontend_mp_mappack_select_update();
        frontnet_start_update();
        break;
    case FeSt_HIGH_SCORES:
        frontend_high_scores_update();
        break;
    default:
        break;
    }
    SYNCDBG(17,"Finished");
}

/**
 * Chooses frontend menu state to which we should return when exiting specific level.
 */
FrontendMenuState get_menu_state_based_on_last_level(LevelNumber lvnum)
{
    if (is_singleplayer_level(lvnum) || is_bonus_level(lvnum) || is_extra_level(lvnum))
    {
        // FeSt_LAND_VIEW is the old full-screen flying-camera cutscene --
        // FeSt_CAMPAIGN_SELECT (frontgui_campaignselect_frame(),
        // frontgui_screens.cpp) is its ImGui replacement, embedding the
        // same LandPreviewPanel with a clickable "Enter this land" commit
        // (frontend_land_selection_enter_resolve() launches straight into
        // FeSt_START_KPRLEVEL, bypassing the cutscene entirely) -- this
        // helper's callers were still sending players back to the
        // deprecated screen (live-tested: "win goes to old fullscreen
        // landview, not the new land selector screen"; the same screen
        // was also observed rendering black on a loss before the player
        // escaped out, an old-screen rendering issue this reroute sidesteps
        // rather than chasing).
        return FeSt_CAMPAIGN_SELECT;
    } else
    if (is_multiplayer_level(lvnum))
    {
        // Skirmish maps are LvKind_IsMulti too (.lof KIND=MULTI, same as a
        // real network mappack -- frontend_freeplay_active_levels()'s own
        // comment, frontmenu_select.c), and frontend_start_skirmish_resolve()
        // enters via FeSt_MAPPACK_SELECT (the merged Free Play/Skirmish
        // screen), not FeSt_NET_SERVICE -- so a finished or quit skirmish
        // game must return there too, or it bounces to the Multiplayer
        // screen instead of back to Skirmish (found live).
        if (frontend_freeplay_is_skirmish())
            return FeSt_MAPPACK_SELECT;
        return FeSt_NET_SERVICE;
    } else
    if (is_freeplay_level(lvnum))
    {
        // FeSt_LEVEL_SELECT/GMnu_FELEVEL_SELECT are the pre-Phase-3
        // single-list screen, no longer reachable -- the merged Free play
        // screen (mappack + level lists together) is FeSt_MAPPACK_SELECT
        // now, same as every other entry point into free play. See
        // docs/refactor/gui/04-phase2-landview-panel-investigation.md.
        return FeSt_MAPPACK_SELECT;
    } else
    {
        return FeSt_MAIN_MENU;
    }
}

/**
 * Chooses frontend menu state to which we should return when exiting another frontend menu.
 * @return The frontend menu state to be used when a substate is cancelled.
 */
FrontendMenuState get_menu_state_when_back_from_substate(FrontendMenuState substate)
{
    LevelNumber lvnum;
    struct PlayerInfo *player;
    switch (substate)
    {
    case FeSt_START_KPRLEVEL:
    case FeSt_START_MPLEVEL:
        lvnum = get_loaded_level_number();
        return get_menu_state_based_on_last_level(lvnum);
    case FeSt_LOAD_GAME:
        return FeSt_START_KPRLEVEL;
    case FeSt_NET_START:
        return FeSt_NET_SESSION;
    case FeSt_MP_MAPPACK_SELECT:
        // Used only for real multiplayer sessions now -- see
        // frontend_mp_mappack_select_resolve's own comment (frontmenu_select.c).
        return FeSt_NET_START;
    case FeSt_LEVEL_SELECT:
         return FeSt_MAPPACK_SELECT;
    case FeSt_NET_SESSION:
    case FeSt_NETLAND_VIEW:
        return FeSt_NET_SERVICE;
    case FeSt_TORTURE:
    case FeSt_CAMPAIGN_INTRO:
        return FeSt_LAND_VIEW;
    case FeSt_LAND_VIEW:
        // Not just TORTURE/CAMPAIGN_INTRO's own back-target above -- also
        // where get_startup_menu_state() now sends a won or lost campaign
        // level (general issue #4's fix), so this substate has no single
        // "parent" screen it was entered from. Missing case here meant
        // frontmap_input()'s own Escape handler (front_landview.c, which
        // calls this with the *live* frontend_menu_state) fell through to
        // the `default` below and dropped straight to the true top-level
        // FeSt_MAIN_MENU -- live-tested: pressing Escape (or the "Return to
        // Main" button on the FeSt_HIGH_SCORES screen just before it, which
        // actually routes here, not to Main Menu, despite its label) right
        // after winning/losing bounced past campaign context entirely
        // ("black screen, then main menu"; "levelstats->landview rather
        // than campaign screen"). CAMPAIGN_SELECT keeps that one Escape
        // press within the campaign, symmetric with TORTURE/CAMPAIGN_INTRO
        // backing into LAND_VIEW itself above.
        return FeSt_CAMPAIGN_SELECT;
    case FeSt_OUTRO:
        return FeSt_LEVEL_STATS;
    case FeSt_DRAG:
        return FeSt_TORTURE;
    case FeSt_LEVEL_STATS:
        if (network_is_active() || skip_high_score_screen) {
            return FeSt_NET_SESSION;
        }
        lvnum = get_loaded_level_number();
        if (is_multiplayer_level(lvnum))
            return get_menu_state_based_on_last_level(lvnum);
        player = get_my_player();
        if (player->victory_state == VicS_WonLevel)
            return FeSt_HIGH_SCORES;
        return get_menu_state_based_on_last_level(lvnum);
    case FeSt_HIGH_SCORES:
        if (fe_high_score_table_from_main_menu)
            return FeSt_MAIN_MENU;
        lvnum = get_loaded_level_number();
        return get_menu_state_based_on_last_level(lvnum);
    case FeSt_FEDEFINE_KEYS:
        return FeSt_FEOPTIONS;
    case FeSt_CREDITS:
        return FeSt_MAIN_MENU;
    default:
        return FeSt_MAIN_MENU;
    }
}

/**
 * Chooses initial frontend menu state.
 * Used when game is first run, or player exits from gameplay.
 */
FrontendMenuState get_startup_menu_state(void)
{
  struct PlayerInfo *player;
  struct UserState *ustate = get_user_state(get_local_user());
  LevelNumber lvnum;
  // docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- an
  // in-session File > New/Open quit-and-relaunch (frontend.h's own comment
  // on editor_pending_relaunch) takes priority over every other branch in
  // this function, including the general "quit always goes to
  // FeSt_MAIN_MENU" one further down: the player *did* send
  // PckA_QuitToMainMenu (PlaF6_PlyrHasQuit gets set too), but this quit was
  // requested by the editor itself to swap levels, not a real "leave the
  // editor" -- FeSt_START_EDITOR re-runs the same bootstrap
  // (game_session_loop.cpp) that the initial Tools -> Editor entry uses.
  // Checked first, ahead of every other branch's own state/player reads,
  // so this stays cheaply callable/testable on its own.
  if (editor_pending_relaunch)
  {
      editor_pending_relaunch = false;
      SYNCLOG("Editor relaunch state selected");
      return FeSt_START_EDITOR;
  }
  // docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md --
  // same priority/placement reasoning as editor_pending_relaunch just
  // above, targeting a normal single-player game start (FeSt_START_KPRLEVEL)
  // on whatever level frontend_request_editor_playtest() already set via
  // set_selected_level_number(), instead of re-entering the editor.
  if (editor_pending_playtest)
  {
      editor_pending_playtest = false;
      editor_playtest_running = true;
      // docs/refactor/editor/fx-plans/03-content-editors-foundation.md §10 -- "Test as part of": the campaign
      // the playtest runs under, so its CONFIGS_LOCATION / CREATURES_LOCATION / strings apply. The editor puts
      // the previous campaign back once it has re-opened.
      if (s_playtest_campaign[0] != '\0')
      {
          if (!change_campaign(s_playtest_pack, s_playtest_campaign))
              WARNLOG("Playtest: could not switch to campaign \"%s\"", s_playtest_campaign);
          s_playtest_campaign[0] = '\0';
      }
      // Editor playtest is always a normal single-player start (this branch's own
      // comment above) -- must not inherit a stale fe_computer_players=1 left by
      // an earlier Skirmish match or Campaign-Editor test play this session.
      fe_computer_players = 0;
      SYNCLOG("Editor playtest state selected");
      return FeSt_START_KPRLEVEL;
  }
  // The playtest game has ended (win, lose or quit): back to the editor,
  // on the scratch level the playtest ran (editor_open() restores the
  // session's real identity from it).
  if (editor_playtest_running)
  {
      editor_playtest_running = false;
      editor_pending_lvnum = EDITOR_PLAYTEST_LEVEL_NUMBER;
      editor_pending_is_new = false;
      SYNCLOG("Editor return from playtest");
      return FeSt_START_EDITOR;
  }
  // A game a content tool (Campaign editor) started to try a level has ended: back to the main menu, which opens the tool again.
  if (content_tool_play_running)
  {
      content_tool_play_running = false;
      content_tool_reopen_tool = content_tool_return_tool;
      content_tool_return_tool = -1;
      // run_pending_content_tool_play() (frontgui_screens.cpp) may have set this for a multiplayer level's
      // default-AI start; nothing on the way back through here consumes or resets it (unlike a real
      // single-player campaign entry, which does via frontmap_load()) -- clear it explicitly so it can't
      // leak into whatever the player starts next from the main menu.
      fe_computer_players = 0;
      SYNCLOG("Return from a content tool's game to the main menu");
      return FeSt_MAIN_MENU;
  }
  if (game_flags2 & GF2_Server)
  {
      game_flags2 &= ~GF2_Server;
      SYNCLOG("Setup server");

      if (setup_network_service(FrontendNetSvc_Online))
      {
          frontnet_service_setup();
          frontnet_session_setup();
          frontnet_session_create(NULL);
          return FeSt_NET_START;
      }
  }
  else if (game_flags2 & GF2_Connect)
  {
      game_flags2 &= ~GF2_Connect;
      SYNCLOG("Setup client");
      if (setup_network_service(FrontendNetSvc_Online))
      {
          frontnet_service_setup();
          frontnet_session_setup();
          net_number_of_sessions = 0;
          memset(net_session, 0, sizeof(net_session));
          // TODO: should disable actual network enumerating if either
          if (LbNetwork_EnumerateSessions(enum_sessions_callback, 0))
          {
              ERRORLOG("LbNetwork_EnumerateSessions() failed");
          }
          else
          {
              net_session_index_active = 0;
              frontnet_session_join(NULL);
              return frontend_menu_state;
          }
      }
  }
  else if ((kfx_sim_state.mode_flags & MFlg_DemoMode) != 0)
  { // If starting up the game after intro
    if (is_full_moon)
    {
        SYNCLOG("Full moon state selected");
        return FeSt_STORY_POEM;
    } else
    if (get_team_birthday() != NULL)
    {
        SYNCLOG("Birthday state selected");
        return FeSt_STORY_BIRTHDAY;
    } else
    {
        SYNCLOG("Standard startup state selected");
        return FeSt_MAIN_MENU;
    }
  } else
  {
    player = get_my_player();
    lvnum = get_loaded_level_number();
    if (network_is_active())
    { // If played real network game, then resulting screen isn't changed based on victory
        SYNCLOG("Network game summary state selected");
        if ((ustate->additional_flags & UsrAF_UnlockedLordTorture) != 0)
        { // Player has won - go FeSt_TORTURE before any others
          ustate->additional_flags &= ~UsrAF_UnlockedLordTorture;
          return FeSt_TORTURE;
        } else
        if ((player->display_flags & PlaF6_PlyrHasQuit) == 0)
        {
          return FeSt_LEVEL_STATS;
        } else
        if (setup_old_network_service())
        {
          return FeSt_NET_SESSION;
        } else
        {
          return FeSt_MAIN_MENU;
        }
    } else
    if ((player->display_flags & PlaF6_PlyrHasQuit) != 0)
    {
        // Explicit Quit (the in-game GMnu_QUIT confirm, PckA_QuitToMainMenu)
        // goes straight to the true top-level main menu regardless of game
        // mode (live-tested request) -- unlike a loss, quitting isn't
        // "finished with this campaign/skirmish", it's "done with this
        // session", so get_menu_state_based_on_last_level()'s campaign-
        // landview / skirmish-select routing (still used by the loss branch
        // below, unchanged) doesn't apply here.
        SYNCLOG("Player quit state selected");
        return FeSt_MAIN_MENU;
    } else
    if (player->victory_state == VicS_Undecided)
    {
        SYNCLOG("Undecided victory state selected");
        return get_menu_state_based_on_last_level(lvnum);
    } else
    if (kfx_sim_state.mode_flags & MFlg_IsDemoMode)
    { // It wasn't a real game, just a demo - back to main menu
        SYNCLOG("Demo mode state selected");
        kfx_sim_state.mode_flags &= ~MFlg_IsDemoMode;
        return FeSt_MAIN_MENU;
    } else
    if (player->victory_state == VicS_WonLevel)
    {
        SYNCLOG("Victory achieved state selected");
        if (is_singleplayer_level(lvnum))
        {
            if (get_continue_level_number() == SINGLEPLAYER_FINISHED)
            {
                return FeSt_OUTRO;
            } else
            if ((ustate->additional_flags & UsrAF_UnlockedLordTorture) != 0)
            {
                ustate->additional_flags &= ~UsrAF_UnlockedLordTorture;
                return FeSt_DRAG;
            } else
            {
                return FeSt_LEVEL_STATS;
            }
        } else
        if (is_bonus_level(lvnum) || is_extra_level(lvnum))
        {
            // See get_menu_state_based_on_last_level()'s own comment --
            // FeSt_LAND_VIEW is the deprecated cutscene, FeSt_CAMPAIGN_SELECT
            // its replacement.
            return FeSt_CAMPAIGN_SELECT;
        } else
        {
            return FeSt_LEVEL_STATS;
        }
    } else
    if (player->victory_state == VicS_State3)
    {
        SYNCLOG("Victory st3 state selected");
        return FeSt_LEVEL_STATS;
    } else
    {
        SYNCLOG("Lost level state selected");
        return get_menu_state_based_on_last_level(lvnum);
    }
  }
  ERRORLOG("Unresolved menu state");
  return FeSt_MAIN_MENU;
}

void try_restore_frontend_error_box()
{
    if (gui_message_timeout < 0 || LbTimerClock() < gui_message_timeout) {
        turn_on_menu(GMnu_FEERROR_BOX);
    }
}

void create_frontend_error_box(int64_t showTime, const char * text)
{
    snprintf(gui_message_text, TEXT_BUFFER_LENGTH, "%s", text);
    gui_message_timeout = -1;
    if (showTime > 0) {
        gui_message_timeout = LbTimerClock() + showTime;
    }
    turn_on_menu(GMnu_FEERROR_BOX);
}

void frontend_draw_error_text_box(struct GuiButton *gbtn)
{
    draw_text_box(gbtn->content.str);
}

void frontend_maintain_error_text_box(struct GuiButton *gbtn)
{
    if (is_key_pressed(KC_ESCAPE, KMod_DONTCARE)) {
        clear_key_pressed(KC_ESCAPE);
        gui_message_timeout = 0;
        turn_off_menu(GMnu_FEERROR_BOX);
        return;
    }
    if (gui_message_timeout > 0 && LbTimerClock() > gui_message_timeout) {
        turn_off_menu(GMnu_FEERROR_BOX);
    }
}

void frontend_draw_product_version(struct GuiButton *gbtn)
{
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    LbTextSetFont(frontend_font[1]);
    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    int64_t h = LbTextLineHeight() * units_per_px / 16;
    LbTextSetWindow(0, gbtn->scr_pos_y, gbtn->width, h);
    char text[128];
    snprintf(text, sizeof(text), "%s %s", PRODUCT_NAME, PRODUCT_VERSION);
    LbTextDrawResized(0, 0, units_per_px, text);
}

TbBool should_use_delta_time_on_menu(void)
{
    switch (frontend_menu_state) {
        case FeSt_MAIN_MENU:
        case FeSt_FELOAD_GAME:
        case FeSt_NET_SERVICE: /**< Network service selection, where player can select Serial/Modem/IPX/TCP IP/1 player. */
        case FeSt_NET_SESSION: /**< Network session selection screen, where list of games is displayed, with possibility to join or create own game. */
        case FeSt_NET_START: /**< Network game start screen (the menu with chat), when created new session or joined existing session. */
        case FeSt_LEVEL_STATS:
        case FeSt_HIGH_SCORES:
        case FeSt_FEDEFINE_KEYS:
        case FeSt_FEOPTIONS:
        case FeSt_LEVEL_SELECT:
        case FeSt_CAMPAIGN_SELECT:
        case FeSt_MAPPACK_SELECT:
        case FeSt_MP_MAPPACK_SELECT:
        case FeSt_LAND_VIEW:
        case FeSt_NETLAND_VIEW:
        case FeSt_TORTURE:
            return true;
        default:
            return false;
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
