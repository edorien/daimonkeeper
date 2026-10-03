/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_net_data.cpp
 *     GUI menus for network support.
 * @par Purpose:
 *     Structures to show and maintain network screens.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 15 Jun 2014
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "frontmenu_net.h"
#include "globals.h"
#include "bflib_basics.h"

#include "bflib_netsession.h"
#include "bflib_guibtns.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"

#include "config_strings.h"
#include "front_network.h"
#include "gui_frontbtns.h"
#include "gui_draw.h"
#include "frontend.h"
#include "front_landview.h"
#include "net_game.h"
#include "net_lobby.h"
#include "sprites.h"
#include "custom_sprites.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// GCC's -Wmissing-field-initializers fires on a partially-designated
// GuiButtonInit aggregate in this C++ translation unit even though the
// omitted fields are the struct's own zero defaults; not a real risk here
// since every field is still named where it matters.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
struct GuiButtonInit frontend_net_service_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MENU_TITLE, .scr_pos_x = 999, .scr_pos_y = 30, .pos_x = 999, .pos_y = 30, .width = 371, .height = 46, .draw_call = frontend_draw_large_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetServiceMenu } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 124, .pos_x = 82, .pos_y = 124, .width = 220, .height = 26, .draw_call = frontnet_draw_scroll_box_tab, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuOnlineLobbies } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 150, .pos_x = 82, .pos_y = 150, .width = 450, .height = 180, .draw_call = frontnet_draw_scroll_box, .tooltip_stridx = GUIStr_Empty, .content = { 26 } },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = frontnet_service_up, .ptover_event = frontend_over_button, .scr_pos_x = 532, .scr_pos_y = 149, .pos_x = 532, .pos_y = 149, .width = 26, .height = 14, .draw_call = frontnet_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 17 }, .maintain_call = frontnet_service_up_maintain },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = frontnet_service_down, .ptover_event = frontend_over_button, .scr_pos_x = 532, .scr_pos_y = 317, .pos_x = 532, .pos_y = 317, .width = 26, .height = 14, .draw_call = frontnet_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 18 }, .maintain_call = frontnet_service_down_maintain },
  { .gbtype = LbBtnT_HoldableBtn, .scr_pos_x = 536, .scr_pos_y = 163, .pos_x = 536, .pos_y = 163, .width = 20, .height = 154, .draw_call = frontnet_draw_services_scroll_tab, .tooltip_stridx = GUIStr_Empty, .content = { 40 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 102, .scr_pos_y = 125, .pos_x = 102, .pos_y = 125, .width = 220, .height = 26, .draw_call = frontend_draw_text, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetServices } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_service_select, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = FE_ROW_Y(158, 26, 0), .pos_x = 95, .pos_y = FE_ROW_Y(158, 26, 0), .width = 424, .height = 26, .draw_call = frontnet_draw_service_button, .tooltip_stridx = GUIStr_Empty, .content = { 45 }, .maintain_call = frontnet_service_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_service_select, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = FE_ROW_Y(158, 26, 1), .pos_x = 95, .pos_y = FE_ROW_Y(158, 26, 1), .width = 424, .height = 26, .draw_call = frontnet_draw_service_button, .tooltip_stridx = GUIStr_Empty, .content = { 46 }, .maintain_call = frontnet_service_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_service_select, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = FE_ROW_Y(158, 26, 2), .pos_x = 95, .pos_y = FE_ROW_Y(158, 26, 2), .width = 424, .height = 26, .draw_call = frontnet_draw_service_button, .tooltip_stridx = GUIStr_Empty, .content = { 47 }, .maintain_call = frontnet_service_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_service_select, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = FE_ROW_Y(158, 26, 3), .pos_x = 95, .pos_y = FE_ROW_Y(158, 26, 3), .width = 424, .height = 26, .draw_call = frontnet_draw_service_button, .tooltip_stridx = GUIStr_Empty, .content = { 48 }, .maintain_call = frontnet_service_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_service_select, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = FE_ROW_Y(158, 26, 4), .pos_x = 95, .pos_y = FE_ROW_Y(158, 26, 4), .width = 424, .height = 26, .draw_call = frontnet_draw_service_button, .tooltip_stridx = GUIStr_Empty, .content = { 49 }, .maintain_call = frontnet_service_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_service_select, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = FE_ROW_Y(158, 26, 5), .pos_x = 95, .pos_y = FE_ROW_Y(158, 26, 5), .width = 424, .height = 26, .draw_call = frontnet_draw_service_button, .tooltip_stridx = GUIStr_Empty, .content = { 50 }, .maintain_call = frontnet_service_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_change_state, .ptover_event = frontend_over_button, .btype_value = 1, .scr_pos_x = 999, .scr_pos_y = 404, .pos_x = 999, .pos_y = 404, .width = 371, .height = 46, .draw_call = frontend_draw_large_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuReturnToMain } },
  { .gbtype = -1 },
};

struct GuiButtonInit frontend_net_session_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MENU_TITLE, .scr_pos_x = 999, .scr_pos_y = 12, .pos_x = 999, .pos_y = 12, .width = 371, .height = 46, .draw_call = frontend_draw_large_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuOnlineLobbies } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 61, .pos_x = 82, .pos_y = 61, .width = 165, .height = 29, .draw_call = frontnet_draw_text_bar, .tooltip_stridx = GUIStr_Empty, .content = { 27 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 95, .scr_pos_y = 63, .pos_x = 91, .pos_y = 63, .width = 165, .height = 25, .draw_call = frontend_draw_text, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetName } },
  { .gbtype = 5, .id_num = -1, .click_event = frontnet_session_set_player_name, .ptover_event = frontend_over_button, .btype_value = 19, .scr_pos_x = 200, .scr_pos_y = 63, .pos_x = 95, .pos_y = 63, .width = 432, .height = 26, .draw_call = frontend_draw_enter_text, .tooltip_stridx = GUIStr_Empty, .content = { .str = tmp_net_player_name }, .maxval = 20 },
  //{ .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_session_add, .ptover_event = frontend_over_button, .scr_pos_x = 321, .scr_pos_y = 93, .pos_x = 321, .pos_y = 93, .width = 247, .height = 46, .draw_call = frontend_draw_small_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuAddComputer } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 112, .pos_x = 82, .pos_y = 112, .width = 220, .height = 26, .draw_call = frontnet_draw_scroll_box_tab, .tooltip_stridx = GUIStr_Empty, .content = { 28 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 138, .pos_x = 82, .pos_y = 138, .width = 450, .height = 180, .draw_call = frontnet_draw_scroll_box, .tooltip_stridx = GUIStr_Empty, .content = { 25 } },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = frontnet_session_up, .ptover_event = frontend_over_button, .scr_pos_x = 532, .scr_pos_y = 137, .pos_x = 532, .pos_y = 137, .width = 26, .height = 14, .draw_call = frontnet_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 17 }, .maintain_call = frontnet_session_up_maintain },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = frontnet_session_down, .ptover_event = frontend_over_button, .scr_pos_x = 532, .scr_pos_y = 217, .pos_x = 532, .pos_y = 217, .width = 26, .height = 14, .draw_call = frontnet_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 18 }, .maintain_call = frontnet_session_down_maintain },
  { .gbtype = LbBtnT_HoldableBtn, .scr_pos_x = 536, .scr_pos_y = 151, .pos_x = 536, .pos_y = 151, .width = 20, .height = 66, .draw_call = frontnet_draw_sessions_scroll_tab, .tooltip_stridx = GUIStr_Empty, .content = { 40 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 102, .scr_pos_y = 113, .pos_x = 102, .pos_y = 113, .width = 220, .height = 26, .draw_call = frontend_draw_text, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetSessions } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 230, .pos_x = 82, .pos_y = 230, .width = 450, .height = 28, .draw_call = frontnet_draw_session_selected, .tooltip_stridx = GUIStr_Empty, .content = { 35 } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_session_select, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = FE_ROW_Y(141, 26, 0), .pos_x = 95, .pos_y = FE_ROW_Y(141, 26, 0), .width = 424, .height = 26, .draw_call = frontnet_draw_session_button, .tooltip_stridx = GUIStr_Empty, .content = { 45 }, .maintain_call = frontnet_session_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_session_select, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = FE_ROW_Y(141, 26, 1), .pos_x = 95, .pos_y = FE_ROW_Y(141, 26, 1), .width = 424, .height = 26, .draw_call = frontnet_draw_session_button, .tooltip_stridx = GUIStr_Empty, .content = { 46 }, .maintain_call = frontnet_session_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_session_select, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = FE_ROW_Y(141, 26, 2), .pos_x = 95, .pos_y = FE_ROW_Y(141, 26, 2), .width = 424, .height = 26, .draw_call = frontnet_draw_session_button, .tooltip_stridx = GUIStr_Empty, .content = { 47 }, .maintain_call = frontnet_session_maintain },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 261, .pos_x = 82, .pos_y = 261, .width = 220, .height = 26, .draw_call = frontnet_draw_scroll_box_tab, .tooltip_stridx = GUIStr_Empty, .content = { 28 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 287, .pos_x = 82, .pos_y = 287, .width = 450, .height = 74, .draw_call = frontnet_draw_scroll_box, .tooltip_stridx = GUIStr_Empty, .content = { 24 } },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = frontnet_players_up, .ptover_event = frontend_over_button, .scr_pos_x = 532, .scr_pos_y = 286, .pos_x = 532, .pos_y = 286, .width = 26, .height = 14, .draw_call = frontnet_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 36 }, .maintain_call = frontnet_players_up_maintain },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = frontnet_players_down, .ptover_event = frontend_over_button, .scr_pos_x = 532, .scr_pos_y = 344, .pos_x = 532, .pos_y = 344, .width = 26, .height = 14, .draw_call = frontnet_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 37 }, .maintain_call = frontnet_players_down_maintain },
  { .gbtype = LbBtnT_HoldableBtn, .scr_pos_x = 536, .scr_pos_y = 300, .pos_x = 536, .pos_y = 300, .width = 20, .height = 44, .draw_call = frontnet_draw_players_scroll_tab, .tooltip_stridx = GUIStr_Empty, .content = { 40 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 95, .scr_pos_y = 262, .pos_x = 95, .pos_y = 262, .width = 220, .height = 22, .draw_call = frontend_draw_text, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuPlayers } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 95, .scr_pos_y = 291, .pos_x = 82, .pos_y = 291, .width = 450, .height = 52, .draw_call = frontnet_draw_net_session_players, .tooltip_stridx = GUIStr_Empty, .content = { 21 } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_session_join, .ptover_event = frontend_over_button, .scr_pos_x = 72, .scr_pos_y = 360, .pos_x = 72, .pos_y = 360, .width = 247, .height = 46, .draw_call = frontend_draw_small_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetJoinGame }, .maintain_call = frontnet_join_game_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_session_create, .ptover_event = frontend_over_button, .scr_pos_x = 321, .scr_pos_y = 360, .pos_x = 321, .pos_y = 360, .width = 247, .height = 46, .draw_call = frontend_draw_small_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetCreateGame } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_return_to_main_menu, .ptover_event = frontend_over_button, .scr_pos_x = 999, .scr_pos_y = 404, .pos_x = 999, .pos_y = 404, .width = 371, .height = 46, .draw_call = frontend_draw_large_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuReturnToMain } },
  { .gbtype = -1 },
};

struct GuiButtonInit frontend_net_start_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MENU_TITLE, .scr_pos_x = 999, .scr_pos_y = 30, .pos_x = 999, .pos_y = 30, .width = 371, .height = 46, .draw_call = frontend_draw_large_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetSessionMenu } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 78, .pos_x = 82, .pos_y = 78, .width = 220, .height = 26, .draw_call = frontnet_draw_scroll_box_tab, .tooltip_stridx = GUIStr_Empty, .content = { 28 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 421, .scr_pos_y = 81, .pos_x = 421, .pos_y = 81, .width = 100, .height = 27, .draw_call = frontnet_draw_alliance_box_tab, .tooltip_stridx = GUIStr_Empty, .content = { 28 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 104, .pos_x = 82, .pos_y = 104, .width = 450, .height = 70, .draw_call = frontnet_draw_scroll_box, .tooltip_stridx = GUIStr_Empty, .content = { 90 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 102, .scr_pos_y = 79, .pos_x = 102, .pos_y = 79, .width = 220, .height = 26, .draw_call = frontend_draw_text, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuPlayers } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 95, .scr_pos_y = 105, .pos_x = 82, .pos_y = 105, .width = 432, .height = 104, .draw_call = frontnet_draw_net_start_players, .tooltip_stridx = GUIStr_Empty, .content = { 21 } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .scr_pos_x = 431, .scr_pos_y = 107, .pos_x = 431, .pos_y = 116, .width = 432, .height = 88, .draw_call = frontnet_draw_alliance_grid, .tooltip_stridx = GUIStr_Empty, .content = { 74 } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .scr_pos_x = 431, .scr_pos_y = 108, .pos_x = 431, .pos_y = 108, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 74 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 1, .scr_pos_x = 453, .scr_pos_y = 108, .pos_x = 453, .pos_y = 108, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 74 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 2, .scr_pos_x = 475, .scr_pos_y = 108, .pos_x = 475, .pos_y = 108, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 74 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 3, .scr_pos_x = 497, .scr_pos_y = 108, .pos_x = 497, .pos_y = 108, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 74 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .scr_pos_x = 431, .scr_pos_y = 134, .pos_x = 431, .pos_y = 134, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 75 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 1, .scr_pos_x = 453, .scr_pos_y = 134, .pos_x = 453, .pos_y = 134, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 75 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 2, .scr_pos_x = 475, .scr_pos_y = 134, .pos_x = 475, .pos_y = 134, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 75 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 3, .scr_pos_x = 497, .scr_pos_y = 134, .pos_x = 497, .pos_y = 134, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 75 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .scr_pos_x = 431, .scr_pos_y = 160, .pos_x = 431, .pos_y = 160, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 76 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 1, .scr_pos_x = 453, .scr_pos_y = 160, .pos_x = 453, .pos_y = 160, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 76 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 2, .scr_pos_x = 475, .scr_pos_y = 160, .pos_x = 475, .pos_y = 160, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 76 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 3, .scr_pos_x = 497, .scr_pos_y = 160, .pos_x = 497, .pos_y = 160, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 76 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .scr_pos_x = 431, .scr_pos_y = 186, .pos_x = 431, .pos_y = 183, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 77 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 1, .scr_pos_x = 453, .scr_pos_y = 186, .pos_x = 453, .pos_y = 186, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 77 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 2, .scr_pos_x = 475, .scr_pos_y = 186, .pos_x = 475, .pos_y = 186, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 77 }, .maintain_call = frontnet_maintain_alliance },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_select_alliance, .ptover_event = frontend_over_button, .btype_value = 3, .scr_pos_x = 497, .scr_pos_y = 186, .pos_x = 497, .pos_y = 186, .width = 22, .height = 26, .draw_call = frontnet_draw_alliance_button, .tooltip_stridx = GUIStr_Empty, .content = { 77 }, .maintain_call = frontnet_maintain_alliance },
  
  { .gbtype = LbBtnT_HoldableBtn, .scr_pos_x = 305, .scr_pos_y = 217, .pos_x = 305, .pos_y = 217, .width = 220, .height = 26, .draw_call = frontnet_draw_bottom_scroll_box_tab, .tooltip_stridx = GUIStr_Empty, .content = { 28 } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_toggle_computer_players, .ptover_event = frontend_over_button, .scr_pos_x = 315, .scr_pos_y = 214, .pos_x = 315, .pos_y = 214, .width = 200, .height = 26, .draw_call = frontend_draw_computer_players, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuComputer } },
  
  
  { .gbtype = LbBtnT_HoldableBtn, .scr_pos_x = 85, .scr_pos_y = 217, .pos_x = 85, .pos_y = 217, .width = 220, .height = 26, .draw_call = frontnet_draw_bottom_scroll_box_tab, .tooltip_stridx = GUIStr_Empty, .content = { 28 } }, //pack select background
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontend_load_mp_mappacks, .ptover_event = frontend_over_button, .scr_pos_x = 95, .scr_pos_y = 214, .pos_x = 95, .pos_y = 214, .width = 200, .height = 26, .draw_call = frontend_draw_mp_mappack, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MouseOptions } }, //pack select
 
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 246, .pos_x = 82, .pos_y = 246, .width = 220, .height = 26, .draw_call = frontnet_draw_scroll_box_tab, .tooltip_stridx = GUIStr_Empty, .content = { 28 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 272, .pos_x = 82, .pos_y = 272, .width = 450, .height = 111, .draw_call = frontnet_draw_scroll_box, .tooltip_stridx = GUIStr_Empty, .content = { 91 } },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = frontnet_messages_up, .ptover_event = frontend_over_button, .scr_pos_x = 532, .scr_pos_y = 271, .pos_x = 532, .pos_y = 271, .width = 26, .height = 14, .draw_call = frontnet_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 38 }, .maintain_call = frontnet_messages_up_maintain },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = frontnet_messages_down, .ptover_event = frontend_over_button, .scr_pos_x = 532, .scr_pos_y = 373, .pos_x = 532, .pos_y = 373, .width = 26, .height = 14, .draw_call = frontnet_draw_slider_button, .tooltip_stridx = GUIStr_Empty, .content = { 39 }, .maintain_call = frontnet_messages_down_maintain },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 102, .scr_pos_y = 247, .pos_x = 102, .pos_y = 247, .width = 220, .height = 26, .draw_call = frontend_draw_text, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetMessages } },
  { .gbtype = LbBtnT_HoldableBtn, .scr_pos_x = 536, .scr_pos_y = 285, .pos_x = 536, .pos_y = 285, .width = 20, .height = 88, .draw_call = frontnet_draw_messages_scroll_tab, .tooltip_stridx = GUIStr_Empty, .content = { 40 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 82, .scr_pos_y = 386, .pos_x = 82, .pos_y = 386, .width = 459, .height = 28, .draw_call = frontnet_draw_current_message, .tooltip_stridx = GUIStr_Empty, .content = { 43 } },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 89, .scr_pos_y = 273, .pos_x = 89, .pos_y = 273, .width = 438, .height = 104, .draw_call = frontnet_draw_messages, .tooltip_stridx = GUIStr_Empty, .content = { 44 } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = set_packet_start, .ptover_event = frontend_over_button, .scr_pos_x = 49, .scr_pos_y = 412, .pos_x = 49, .pos_y = 412, .width = 247, .height = 46, .draw_call = frontend_draw_small_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetStartGame }, .maintain_call = frontnet_start_game_maintain },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_return_to_session_menu, .ptover_event = frontend_over_button, .btype_value = 1, .scr_pos_x = 345, .scr_pos_y = 412, .pos_x = 345, .pos_y = 412, .width = 247, .height = 46, .draw_call = frontend_draw_small_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuCancel } },
  { .gbtype = -1 },
};

struct GuiButtonInit frontend_add_session_buttons[] = {//TODO GUI prepare add session screen
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 999, .pos_x = 999, .width = 450, .height = 180, .draw_call = frontnet_draw_scroll_box, .tooltip_stridx = GUIStr_Empty, .content = { 26 } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_add_session_done, .ptover_event = frontend_over_button, .scr_pos_x = 72, .scr_pos_y = 48, .pos_x = 72, .pos_y = 48, .width = 247, .height = 46, .draw_call = frontend_draw_small_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_NetJoinGame } },
  { .gbtype = LbBtnT_NormalBtn, .click_event = frontnet_add_session_back, .ptover_event = frontend_over_button, .scr_pos_x = 321, .scr_pos_y = 48, .pos_x = 321, .pos_y = 48, .width = 247, .height = 46, .draw_call = frontend_draw_small_menu_button, .tooltip_stridx = GUIStr_Empty, .content = { FEBtn_MnuCancel } },
  { .gbtype = -1, .tooltip_stridx = GUIStr_Empty },
};
#pragma GCC diagnostic pop

struct GuiMenu frontend_net_service_menu =
 { GMnu_FENET_SERVICE, 0, 1, frontend_net_service_buttons, POS_SCRCTR, POS_SCRCTR,  640, 480, NULL, 0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu frontend_net_session_menu =
 { GMnu_FENET_SESSION, 0, 1, frontend_net_session_buttons, POS_SCRCTR, POS_SCRCTR,  640, 480, NULL, 0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu frontend_net_start_menu =
 { GMnu_FENET_START,   0, 1, frontend_net_start_buttons,   POS_SCRCTR, POS_SCRCTR,  640, 480, NULL, 0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu frontend_add_session_box =
 { GMnu_FEADD_SESSION, 0, 1, frontend_add_session_buttons, POS_SCRCTR, POS_SCRCTR,  450,  92, NULL, 0, NULL,    NULL,                    0, 1, 0,};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
void frontnet_draw_session_selected(struct GuiButton *gbtn)
{
    const struct TbSprite *spr;
    int64_t pos_x;
    int64_t pos_y;
    int64_t i;
    pos_x = gbtn->scr_pos_x;
    pos_y = gbtn->scr_pos_y;
    int64_t fs_units_per_px;
    fs_units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_largearea_xts_tx1_c, 100);
    spr = get_frontend_sprite(GFS_largearea_xts_cor_l);
    for (i=0; i < 6; i++)
    {
        LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
        pos_x += spr->SWidth * fs_units_per_px / 16;
        spr++;
    }
    if (net_session_index_active >= 0)
    {
        const char *text;
        text = net_session[net_session_index_active]->text;
        i = frontend_button_caption_font(gbtn, 0);
        if (text != NULL)
        {
            RendererSetDrawFlags(0);
            LbTextSetFont(frontend_font[i]);
            // Set drawing window and draw the text
            int64_t tx_units_per_px;
            tx_units_per_px = (gbtn->height*13/14) * 16 / LbTextLineHeight();
            int64_t h;
            h = LbTextLineHeight()*tx_units_per_px/16;
            LbTextSetWindow(gbtn->scr_pos_x + 13*fs_units_per_px/16, gbtn->scr_pos_y, gbtn->width - 26*fs_units_per_px/16, h);
            LbTextDrawResized(0, 0, tx_units_per_px, text);
        }
    }
}

/** Highlights the session at list index i -- no frontend_set_state() call
 * anywhere in this one, so unlike most of this phase's other extractions
 * it's already directly safe to call from an active ImGui window. Shared
 * by the legacy click_event (frontnet_session_select, below) and the
 * ImGui screen (frontgui_screens.cpp), which iterates net_session[]
 * directly and already has a real index.
 */
void frontnet_session_select_by_index(int64_t i)
{
    if (net_number_of_sessions > i)
    {
        net_session_index_active = i;
        net_session_index_active_id = net_session[i]->id;
    }
}

void frontnet_session_select(struct GuiButton *gbtn)
{
    int64_t i;
    i = frontend_selectlist_row_to_item_index(&net_session_list, gbtn);
    frontnet_session_select_by_index(i);
}

void frontnet_draw_session_button(struct GuiButton *gbtn)
{
    int64_t sessionIndex;
    int64_t height;

    sessionIndex = frontend_selectlist_row_to_item_index(&net_session_list, gbtn);
    if ((sessionIndex < 0) || (sessionIndex >= net_number_of_sessions))
        return;
    int64_t font_idx;
    font_idx = frontend_button_caption_font(gbtn,frontend_mouse_over_button);
    LbTextSetFont(frontend_font[font_idx]);
    RendererSetDrawFlags(0);
    int64_t tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / LbTextLineHeight();
    height = LbTextLineHeight() * tx_units_per_px / 16;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, height);
    LbTextDrawResized(0, 0, tx_units_per_px, net_session[sessionIndex]->text);
}

/** frontnet_session_create's actual work, minus the state-transition call
 * itself -- see frontnet_session_join_resolve's comment (frontmenu_net.c)
 * for why. process_network_error() only pops the (legacy GuiMenu-based,
 * not ImGui) error box, so it's safe to call here regardless.
 */
int64_t frontnet_session_create_resolve(void)
{
    // Create a new session using the player name as the session name.
    // Append a number to the session name if it already exists.
    int64_t idx = 0;
    for (int64_t i = 0; i < net_number_of_sessions; i++)
    {
        const auto nsname = net_session[i];
        if (nsname == nullptr) continue;
        const char * backslash = strchr(nsname->text, '\'');
        if (backslash) {
            if (strlen(net_player_name) == backslash - nsname->text) {
                if (strncmp(nsname->text, net_player_name, backslash - nsname->text) == 0) {
                    idx++;
                }
            }
        } else if (strcmp(nsname->text, net_player_name) == 0) {
            idx++;
        }
    }
    char text[sizeof(net_session[0]->text) + 16];
    if (idx > 0) {
        snprintf(text, sizeof(text), "%s (%" PRId64 ")", net_player_name, (int64_t)(idx + 1));
    } else {
        snprintf(text, sizeof(text), "%s", net_player_name);
    }
    uint64_t plyr_num;
    if (LbNetwork_Create(text, net_player_name, &plyr_num, nullptr))
    {
        process_network_error(-801);
        return -1;
    }
    frontend_set_player_number(plyr_num);
    fe_computer_players = 0;
    return FeSt_NET_START;
}

void frontnet_session_create(struct GuiButton *gbtn)
{
    int64_t next_state = frontnet_session_create_resolve();
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}
/******************************************************************************/
