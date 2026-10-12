/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_ingame_tabs_data.cpp
 *     Main in-game GUI, visible during gameplay.
 * @par Purpose:
 *     Structures to show and maintain tabbed menu appearing ingame.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 11 Feb 2013
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "frontmenu_ingame_tabs.h"
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
#include "config_strings.h"
#include "frontmenu_options.h"
#include "game_legacy.h"
#include "frontmenu_ingame_evnt.h"
#include "frontmenu_ingame_opts.h"
#include "sprites.h"
#include "player_instances.h"
#include "kfx_frontend_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
void gui_go_to_map(struct GuiButton *gbtn);
void gui_turn_on_autopilot(struct GuiButton *gbtn);
void menu_tab_maintain(struct GuiButton *gbtn);
void gui_area_autopilot_button(struct GuiButton *gbtn);
void maintain_turn_on_autopilot(struct GuiButton *gbtn);
void gui_choose_room(struct GuiButton *gbtn);
void gui_area_event_button(struct GuiButton *gbtn);
void gui_remove_area_for_rooms(struct GuiButton *gbtn);
void gui_area_big_room_button(struct GuiButton *gbtn);
void gui_choose_spell(struct GuiButton *gbtn);
void gui_go_to_next_spell(struct GuiButton *gbtn);
void gui_area_spell_button(struct GuiButton *gbtn);
void gui_choose_special_spell(struct GuiButton *gbtn);
void gui_area_big_spell_button(struct GuiButton *gbtn);
void gui_choose_workshop_item(struct GuiButton *gbtn);
void gui_go_to_next_trap(struct GuiButton *gbtn);
void gui_over_trap_button(struct GuiButton *gbtn);
void maintain_trap(struct GuiButton *gbtn);
void gui_area_trap_button(struct GuiButton *gbtn);
void gui_go_to_next_door(struct GuiButton *gbtn);
void maintain_door(struct GuiButton *gbtn);
void gui_over_door_button(struct GuiButton *gbtn);
void gui_remove_area_for_traps(struct GuiButton *gbtn);
void gui_area_big_trap_button(struct GuiButton *gbtn);
void gui_area_trap_build_info_button(struct GuiButton* gbtn);
void maintain_big_trap(struct GuiButton *gbtn);
void gui_creature_query_background1(struct GuiMenu *gmnu);
void gui_creature_query_background2(struct GuiMenu *gmnu);
void maintain_room(struct GuiButton *gbtn);
void maintain_big_room(struct GuiButton *gbtn);
void maintain_spell(struct GuiButton *gbtn);
void maintain_big_spell(struct GuiButton *gbtn);
void maintain_trap(struct GuiButton *gbtn);
void maintain_door(struct GuiButton *gbtn);
void maintain_buildable_info(struct GuiButton *gbtn);
void pick_up_creature_doing_activity(struct GuiButton *gbtn);
void gui_go_to_next_creature_activity(struct GuiButton *gbtn);
void gui_go_to_next_room(struct GuiButton *gbtn);
void gui_over_room_button(struct GuiButton *gbtn);
void gui_area_room_button(struct GuiButton *gbtn);
void pick_up_next_creature(struct GuiButton *gbtn);
void gui_go_to_next_creature(struct GuiButton *gbtn);
void gui_area_anger_button(struct GuiButton *gbtn);
void gui_area_smiley_anger_button(struct GuiButton *gbtn);
void gui_area_experience_button(struct GuiButton *gbtn);
void gui_area_instance_button(struct GuiButton *gbtn);
void maintain_instance(struct GuiButton *gbtn);
void maintain_activity_up(struct GuiButton *gbtn);
void maintain_activity_down(struct GuiButton *gbtn);
void maintain_activity_pic(struct GuiButton *gbtn);
void maintain_activity_row(struct GuiButton *gbtn);
void gui_scroll_activity_up(struct GuiButton *gbtn);
void gui_scroll_activity_up(struct GuiButton *gbtn);
void gui_scroll_activity_down(struct GuiButton *gbtn);
void gui_scroll_activity_down(struct GuiButton *gbtn);
void maintain_activity_up(struct GuiButton *gbtn);
void maintain_activity_down(struct GuiButton *gbtn);
void maintain_activity_pic(struct GuiButton *gbtn);
void maintain_activity_row(struct GuiButton *gbtn);
void gui_activity_background(struct GuiMenu *gmnu);
void gui_area_ally(struct GuiButton *gbtn);
void gui_area_stat_button(struct GuiButton *gbtn);
void maintain_event_button(struct GuiButton *gbtn);
void gui_toggle_ally(struct GuiButton *gbtn);
void maintain_ally(struct GuiButton *gbtn);
void maintain_prison_bar(struct GuiButton *gbtn);
void maintain_flee_button(struct GuiButton *gbtn);
extern TbBool tendency_button_imprison;
extern TbBool tendency_button_flee;
void maintain_room_button(struct GuiButton *gbtn);
void maintain_creature_button(struct GuiButton* gbtn);
void pick_up_next_wanderer(struct GuiButton *gbtn);
void gui_go_to_next_wanderer(struct GuiButton *gbtn);
void pick_up_next_worker(struct GuiButton *gbtn);
void gui_go_to_next_worker(struct GuiButton *gbtn);
void pick_up_next_fighter(struct GuiButton *gbtn);
void gui_go_to_next_fighter(struct GuiButton *gbtn);
void gui_area_payday_button(struct GuiButton *gbtn);
void gui_area_research_bar(struct GuiButton *gbtn);
void gui_area_workshop_bar(struct GuiButton *gbtn);
void gui_area_player_creature_info(struct GuiButton *gbtn);
void gui_area_player_room_info(struct GuiButton *gbtn);
void spell_lost_first_person(struct GuiButton *gbtn);
void gui_set_tend_to(struct GuiButton *gbtn);
void gui_set_query(struct GuiButton *gbtn);
void maintain_query_button(struct GuiButton *gbtn);
void maintain_player_page2(struct GuiButton *gbtn);
void gui_set_page(struct GuiButton* gbtn);
/******************************************************************************/
// GCC's -Wmissing-field-initializers fires on a partially-designated
// GuiButtonInit aggregate in this C++ translation unit even though the
// omitted fields are the struct's own zero defaults; not a real risk here
// since every field is still named where it matters.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
struct GuiButtonInit main_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_OPTIONS, .scr_pos_x = 68, .pos_x = 68, .width = 68, .height = 16, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_quit_act, .tooltip_stridx = GUIStr_MnuOptionsDesc, .parent_menu = &options_menu },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MAP_ZOOM_IN, .click_event = gui_zoom_in, .scr_pos_x = 112, .scr_pos_y = 4, .pos_x = 114, .pos_y = 4, .width = 26, .height = 66, .draw_call = gui_area_new_vertical_button, .sprite_idx = GPS_rpanel_rpanel_mapbt2a, .tooltip_stridx = GUIStr_PaneZoomInDesc },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MAP_ZOOM_OU, .click_event = gui_zoom_out, .scr_pos_x = 110, .scr_pos_y = 70, .pos_x = 114, .pos_y = 70, .width = 26, .height = 66, .draw_call = gui_area_new_vertical_button, .sprite_idx = GPS_rpanel_rpanel_mapbt3a, .tooltip_stridx = GUIStr_PaneZoomOutDesc },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MAP_ZOOM_FS, .click_event = gui_go_to_map, .width = 30, .height = 31, .draw_call = gui_area_new_vertical_button, .sprite_idx = GPS_rpanel_rpanel_btn_bigmap_act, .tooltip_stridx = GUIStr_PaneLargeMapDesc },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ASSIST, .click_event = gui_turn_on_autopilot, .scr_pos_y = 70, .pos_y = 70, .width = 16, .height = 67, .draw_call = gui_area_autopilot_button, .sprite_idx = GPS_rpanel_rpanel_btn_cassisti_act, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_turn_on_autopilot },
  { .gbtype = LbBtnT_RadioBtn, .id_num = BID_INFO_TAB, .click_event = gui_set_menu_mode, .btype_value = GMnu_QUERY, .scr_pos_y = 154, .pos_y = 154, .width = 28, .height = 34, .draw_call = gui_draw_tab, .sprite_idx = GPS_rpanel_rpanel_tab_infoa, .tooltip_stridx = GUIStr_InformationPanelDesc, .content = { .ptr = &info_tag }, .maintain_call = menu_tab_maintain },
  { .gbtype = LbBtnT_RadioBtn, .id_num = BID_ROOM_TAB, .click_event = gui_set_menu_mode, .btype_value = GMnu_ROOM, .scr_pos_x = 28, .scr_pos_y = 154, .pos_x = 28, .pos_y = 154, .width = 28, .height = 34, .draw_call = gui_draw_tab, .sprite_idx = GPS_rpanel_rpanel_tab_rooma, .tooltip_stridx = GUIStr_RoomPanelDesc, .content = { .ptr = &room_tag }, .maintain_call = menu_tab_maintain },
  { .gbtype = LbBtnT_RadioBtn, .id_num = BID_SPELL_TAB, .click_event = gui_set_menu_mode, .btype_value = GMnu_SPELL, .scr_pos_x = 56, .scr_pos_y = 154, .pos_x = 56, .pos_y = 154, .width = 28, .height = 34, .draw_call = gui_draw_tab, .sprite_idx = GPS_rpanel_rpanel_tab_spela, .tooltip_stridx = GUIStr_ResearchPanelDesc, .content = { .ptr = &spell_tag }, .maintain_call = menu_tab_maintain },
  { .gbtype = LbBtnT_RadioBtn, .id_num = BID_MNFCT_TAB, .click_event = gui_set_menu_mode, .btype_value = GMnu_TRAP, .scr_pos_x = 84, .scr_pos_y = 154, .pos_x = 84, .pos_y = 154, .width = 28, .height = 34, .draw_call = gui_draw_tab, .sprite_idx = GPS_rpanel_rpanel_tab_wrksha, .tooltip_stridx = GUIStr_WorkshopPanelDesc, .content = { .ptr = &trap_tag }, .maintain_call = menu_tab_maintain },
  { .gbtype = LbBtnT_RadioBtn, .id_num = BID_CREATR_TAB, .click_event = gui_set_menu_mode, .btype_value = GMnu_CREATURE, .scr_pos_x = 112, .scr_pos_y = 154, .pos_x = 112, .pos_y = 154, .width = 28, .height = 34, .draw_call = gui_draw_tab, .sprite_idx = GPS_rpanel_rpanel_tab_crtra, .tooltip_stridx = GUIStr_CreaturePanelDesc, .content = { .ptr = &creature_tag }, .maintain_call = menu_tab_maintain },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV01, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 0), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 0), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV02, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 1), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 1), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 1 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV03, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 2), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 2), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 2 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV04, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 3), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 3), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 3 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV05, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 4), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 4), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 4 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV06, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 5), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 5), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 5 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV07, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 6), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 6), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 6 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV08, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 7), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 7), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 7 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV09, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 8), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 8), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 8 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV10, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 9), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 9), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 9 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV11, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 10), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 10), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 10 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV12, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 11), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 11), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 11 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MSG_EV13, .click_event = gui_open_event, .rclick_event = gui_kill_event, .scr_pos_x = 138, .scr_pos_y = FE_ROW_Y(360, -30, 12), .pos_x = 138, .pos_y = FE_ROW_Y(360, -30, 12), .width = 24, .height = 30, .draw_call = gui_area_event_button, .tooltip_stridx = GUIStr_Empty, .content = { 12 }, .maintain_call = maintain_event_button },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 22, .scr_pos_y = 122, .pos_x = 22, .pos_y = 122, .width = 94, .height = 40, .tooltip_stridx = GUIStr_TeamMoneyAvailable },
  { .gbtype = -1 },
};

struct GuiButtonInit room_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD01, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 2, .scr_pos_y = 238, .pos_x = 6, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_treasury_std_s, .tooltip_stridx = CpgStr_RoomDesc1+0, .content = { 2 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD03, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 34, .scr_pos_y = 238, .pos_x = 38, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_lair_std_s, .tooltip_stridx = CpgStr_RoomDesc1+10, .content = { 14 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD02, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 66, .scr_pos_y = 238, .pos_x = 70, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_hatchery_std_s, .tooltip_stridx = CpgStr_RoomDesc1+9, .content = { 13 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD05, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 98, .scr_pos_y = 238, .pos_x = 102, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_training_std_s, .tooltip_stridx = CpgStr_RoomDesc1+3, .content = { 6 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD04, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 2, .scr_pos_y = 276, .pos_x = 6, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_research_std_s, .tooltip_stridx = CpgStr_RoomDesc1+1, .content = { 3 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD13, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 34, .scr_pos_y = 276, .pos_x = 38, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_bridge_std_s, .tooltip_stridx = CpgStr_RoomDesc1+11, .content = { 15 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD14, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 66, .scr_pos_y = 276, .pos_x = 70, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_grdpost_std_s, .tooltip_stridx = CpgStr_RoomDesc1+12, .content = { 16 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD08, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 98, .scr_pos_y = 276, .pos_x = 102, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_workshop_std_s, .tooltip_stridx = CpgStr_RoomDesc1+6, .content = { 8 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD06, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 2, .scr_pos_y = 314, .pos_x = 6, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_prison_std_s, .tooltip_stridx = CpgStr_RoomDesc1+2, .content = { 4 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD12, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 34, .scr_pos_y = 314, .pos_x = 38, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_torture_std_s, .tooltip_stridx = CpgStr_RoomDesc1+4, .content = { 5 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD11, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 66, .scr_pos_y = 314, .pos_x = 70, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_armory_std_s, .tooltip_stridx = CpgStr_RoomDesc1+8, .content = { 12 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD07, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 98, .scr_pos_y = 314, .pos_x = 102, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_temple_std_s, .tooltip_stridx = CpgStr_RoomDesc1+13, .content = { 10 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD10, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 2, .scr_pos_y = 352, .pos_x = 6, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_graveyard_std_s, .tooltip_stridx = CpgStr_RoomDesc1+7, .content = { 11 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD09, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 34, .scr_pos_y = 352, .pos_x = 38, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_room_scavenge_std_s, .tooltip_stridx = CpgStr_RoomDesc1+14, .content = { 9 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 66, .scr_pos_y = 352, .pos_x = 70, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD16, .click_event = gui_remove_area_for_rooms, .scr_pos_x = 98, .scr_pos_y = 352, .pos_x = 102, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_no_anim_button, .sprite_idx = GPS_rpanel_frame_portrt_sell, .tooltip_stridx = GUIStr_SellRoomDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 210, .pos_x = 8, .pos_y = 194, .width = 126, .height = 44, .draw_call = gui_area_big_room_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_big_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_NXPG, .button_flags = 1, .click_event = gui_set_page, .scr_pos_x = 78, .scr_pos_y = 188, .pos_x = 78, .pos_y = 188, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_Empty, .parent_menu = &room_menu2, .content = { GMnu_ROOM2 }, .maintain_call = maintain_room_next_page_button },
  { .gbtype = -1 },
};

struct GuiButtonInit room_menu2_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD17, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 2, .scr_pos_y = 238, .pos_x = 6, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 2 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD18, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 34, .scr_pos_y = 238, .pos_x = 38, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 14 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD19, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 66, .scr_pos_y = 238, .pos_x = 70, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 13 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD20, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 98, .scr_pos_y = 238, .pos_x = 102, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 6 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD21, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 2, .scr_pos_y = 276, .pos_x = 6, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 3 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD22, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 34, .scr_pos_y = 276, .pos_x = 38, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 15 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD23, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 66, .scr_pos_y = 276, .pos_x = 70, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 16 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD24, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 98, .scr_pos_y = 276, .pos_x = 102, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 8 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD25, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 2, .scr_pos_y = 314, .pos_x = 6, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 4 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD26, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 34, .scr_pos_y = 314, .pos_x = 38, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 5 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD27, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 66, .scr_pos_y = 314, .pos_x = 70, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 12 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD28, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 98, .scr_pos_y = 314, .pos_x = 102, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 10 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD29, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 2, .scr_pos_y = 352, .pos_x = 6, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 11 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD30, .click_event = gui_choose_room, .rclick_event = gui_go_to_next_room, .ptover_event = gui_over_room_button, .scr_pos_x = 34, .scr_pos_y = 352, .pos_x = 38, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_room_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 9 }, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 66, .scr_pos_y = 352, .pos_x = 70, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_TD31, .click_event = gui_remove_area_for_rooms, .scr_pos_x = 98, .scr_pos_y = 352, .pos_x = 102, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_no_anim_button, .sprite_idx = GPS_rpanel_frame_portrt_sell, .tooltip_stridx = GUIStr_SellRoomDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 210, .pos_x = 8, .pos_y = 194, .width = 126, .height = 44, .draw_call = gui_area_big_room_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_big_room },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_ROOM_NXPG, .button_flags = 1, .click_event = gui_set_page, .scr_pos_x = 78, .scr_pos_y = 188, .pos_x = 78, .pos_y = 188, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_Empty, .parent_menu = &room_menu, .content = { GMnu_ROOM }, .maintain_call = maintain_room_next_page_button },
  { .gbtype = -1 },
};

struct GuiButtonInit spell_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD16, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 2, .scr_pos_y = 238, .pos_x = 6, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_possess_std_s, .tooltip_stridx = CpgStr_PowerDesc1+0, .content = { 18 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD01, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 34, .scr_pos_y = 238, .pos_x = 38, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_imp_std_s, .tooltip_stridx = CpgStr_PowerDesc1+1, .content = { 2 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD02, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 66, .scr_pos_y = 238, .pos_x = 70, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_sight_std_s, .tooltip_stridx = CpgStr_PowerDesc1+2, .content = { 5 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD07, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 98, .scr_pos_y = 238, .pos_x = 102, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_speed_std_s, .tooltip_stridx = CpgStr_PowerDesc1+7, .content = { 11 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD15, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 2, .scr_pos_y = 276, .pos_x = 6, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_crspell_whip_std_s, .tooltip_stridx = CpgStr_PowerDesc1+6, .content = { 3 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD03, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 34, .scr_pos_y = 276, .pos_x = 38, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_cta_std_s, .tooltip_stridx = CpgStr_PowerDesc1+3, .content = { 6 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD09, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 66, .scr_pos_y = 276, .pos_x = 70, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_conceal_std_s, .tooltip_stridx = CpgStr_PowerDesc1+9, .content = { 13 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD14, .click_event = gui_choose_special_spell, .scr_pos_x = 98, .scr_pos_y = 276, .pos_x = 102, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_holdaud_std_s, .tooltip_stridx = CpgStr_PowerDesc1+4, .content = { 9 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD04, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 2, .scr_pos_y = 314, .pos_x = 6, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_cavein_std_s, .tooltip_stridx = CpgStr_PowerDesc1+5, .content = { 7 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD06, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 34, .scr_pos_y = 314, .pos_x = 38, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_heal_std_s, .tooltip_stridx = CpgStr_PowerDesc1+14, .content = { 8 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD05, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 66, .scr_pos_y = 314, .pos_x = 70, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_lightng_std_s, .tooltip_stridx = CpgStr_PowerDesc1+10, .content = { 10 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD08, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 98, .scr_pos_y = 314, .pos_x = 102, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_armor_std_s, .tooltip_stridx = CpgStr_PowerDesc1+8, .content = { 12 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD10, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 2, .scr_pos_y = 352, .pos_x = 6, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_chicken_std_s, .tooltip_stridx = CpgStr_PowerDesc1+11, .content = { 15 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD11, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 34, .scr_pos_y = 352, .pos_x = 38, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_disease_std_s, .tooltip_stridx = CpgStr_PowerDesc1+12, .content = { 14 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD13, .click_event = gui_choose_special_spell, .scr_pos_x = 66, .scr_pos_y = 352, .pos_x = 70, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_armagedn_std_s, .tooltip_stridx = CpgStr_PowerDesc1+16, .content = { 19 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD12, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 98, .scr_pos_y = 352, .pos_x = 102, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_dstwall_std_s, .tooltip_stridx = CpgStr_PowerDesc1+13, .content = { 16 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 210, .pos_x = 8, .pos_y = 194, .width = 126, .height = 44, .draw_call = gui_area_big_spell_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_big_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_NXPG, .button_flags = 1, .click_event = gui_set_page, .scr_pos_x = 78, .scr_pos_y = 188, .pos_x = 78, .pos_y = 188, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_Empty, .parent_menu = &spell_menu2, .content = { GMnu_SPELL2 }, .maintain_call = maintain_spell_next_page_button },
  { .gbtype = -1 },
};

struct GuiButtonInit spell_menu2_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD17, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 2, .scr_pos_y = 238, .pos_x = 6, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_keepower_timebomb_std_s, .tooltip_stridx = CpgStr_PowerDesc1+15, .content = { 18 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD18, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 34, .scr_pos_y = 238, .pos_x = 38, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 2 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD19, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 66, .scr_pos_y = 238, .pos_x = 70, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 5 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD20, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 98, .scr_pos_y = 238, .pos_x = 102, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 11 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD21, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 2, .scr_pos_y = 276, .pos_x = 6, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 3 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD22, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 34, .scr_pos_y = 276, .pos_x = 38, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 6 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD23, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 66, .scr_pos_y = 276, .pos_x = 70, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 13 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD24, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 98, .scr_pos_y = 276, .pos_x = 102, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 9 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD25, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 2, .scr_pos_y = 314, .pos_x = 6, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 7 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD26, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 34, .scr_pos_y = 314, .pos_x = 38, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 8 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD27, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 66, .scr_pos_y = 314, .pos_x = 70, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 10 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD28, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 98, .scr_pos_y = 314, .pos_x = 102, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 12 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD29, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 2, .scr_pos_y = 352, .pos_x = 6, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 15 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD30, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 34, .scr_pos_y = 352, .pos_x = 38, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 14 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD31, .click_event = gui_choose_special_spell, .scr_pos_x = 66, .scr_pos_y = 352, .pos_x = 70, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 19 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD32, .click_event = gui_choose_spell, .rclick_event = gui_go_to_next_spell, .scr_pos_x = 98, .scr_pos_y = 352, .pos_x = 102, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_spell_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = CpgStr_Empty, .content = { 16 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 210, .pos_x = 8, .pos_y = 194, .width = 126, .height = 44, .draw_call = gui_area_big_spell_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_big_spell },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_NXPG, .button_flags = 1, .click_event = gui_set_page, .scr_pos_x = 78, .scr_pos_y = 188, .pos_x = 78, .pos_y = 188, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_Empty, .parent_menu = &spell_menu, .content = { GMnu_SPELL }, .maintain_call = maintain_spell_next_page_button },
  { .gbtype = -1 },
};

struct GuiButtonInit spell_lost_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_POWER_TD16, .click_event = spell_lost_first_person, .scr_pos_x = 2, .scr_pos_y = 238, .pos_x = 8, .pos_y = 250, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_keepower_possess_std_s, .tooltip_stridx = CpgStr_PowerDesc1+0, .content = { 18 }, .maintain_call = maintain_spell },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 34, .scr_pos_y = 238, .pos_x = 40, .pos_y = 250, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 66, .scr_pos_y = 238, .pos_x = 72, .pos_y = 250, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 98, .scr_pos_y = 238, .pos_x = 104, .pos_y = 250, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 2, .scr_pos_y = 276, .pos_x = 8, .pos_y = 288, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 34, .scr_pos_y = 276, .pos_x = 40, .pos_y = 288, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 66, .scr_pos_y = 276, .pos_x = 72, .pos_y = 288, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 98, .scr_pos_y = 276, .pos_x = 104, .pos_y = 288, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 2, .scr_pos_y = 314, .pos_x = 8, .pos_y = 326, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 34, .scr_pos_y = 314, .pos_x = 40, .pos_y = 326, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 66, .scr_pos_y = 314, .pos_x = 72, .pos_y = 326, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 98, .scr_pos_y = 314, .pos_x = 104, .pos_y = 326, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 2, .scr_pos_y = 352, .pos_x = 8, .pos_y = 364, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 34, .scr_pos_y = 352, .pos_x = 40, .pos_y = 364, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 66, .scr_pos_y = 352, .pos_x = 72, .pos_y = 364, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 98, .scr_pos_y = 352, .pos_x = 104, .pos_y = 364, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 210, .pos_x = 8, .pos_y = 194, .width = 126, .height = 44, .draw_call = gui_area_big_spell_button, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = -1 },
};

struct GuiButtonInit trap_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD02, .scr_pos_x = 2, .scr_pos_y = 238, .pos_x = 6, .pos_y = 242, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_trap_alarm_std_s, .tooltip_stridx = CpgStr_AlarmTrapDesc, .content = { 2 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD03, .scr_pos_x = 34, .scr_pos_y = 238, .pos_x = 38, .pos_y = 242, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_trap_gas_std_s, .tooltip_stridx = CpgStr_PoisonGasTrapDesc, .content = { 3 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD04, .scr_pos_x = 66, .scr_pos_y = 238, .pos_x = 70, .pos_y = 242, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_trap_lightning_std_s, .tooltip_stridx = CpgStr_LightningTrapDesc, .content = { 4 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD15, .scr_pos_x = 98, .scr_pos_y = 238, .pos_x = 102, .pos_y = 242, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_trap_lava_std_s, .tooltip_stridx = CpgStr_LavaTrapDesc, .content = { 6 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD01, .scr_pos_x = 2, .scr_pos_y = 276, .pos_x = 6, .pos_y = 280, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_trap_boulder_std_s, .tooltip_stridx = CpgStr_TrapBoulderDesc, .content = { 1 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD05, .scr_pos_x = 34, .scr_pos_y = 276, .pos_x = 38, .pos_y = 280, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_trap_wop_std_s, .tooltip_stridx = CpgStr_WordOfPowerTrapDesc, .content = { 5 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD11, .scr_pos_x = 66, .scr_pos_y = 276, .pos_x = 70, .pos_y = 280, .width = 32, .height = 36, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD12, .scr_pos_x = 98, .scr_pos_y = 276, .pos_x = 102, .pos_y = 280, .width = 32, .height = 36, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD06, .scr_pos_x = 2, .scr_pos_y = 314, .pos_x = 6, .pos_y = 318, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_door_wood_std_s, .tooltip_stridx = CpgStr_WoodenDoorDesc, .content = { 7 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD07, .scr_pos_x = 34, .scr_pos_y = 314, .pos_x = 38, .pos_y = 318, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_door_braced_std_s, .tooltip_stridx = CpgStr_BracedDoorDesc, .content = { 8 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD08, .scr_pos_x = 66, .scr_pos_y = 314, .pos_x = 70, .pos_y = 318, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_door_iron_std_s, .tooltip_stridx = CpgStr_IronDoorDesc, .content = { 9 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD09, .scr_pos_x = 98, .scr_pos_y = 314, .pos_x = 102, .pos_y = 318, .width = 32, .height = 36, .sprite_idx = GPS_trapdoor_door_magic_std_s, .tooltip_stridx = CpgStr_MagicDoorDesc, .content = { 10 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD13, .scr_pos_x = 2, .scr_pos_y = 352, .pos_x = 6, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD14, .scr_pos_x = 34, .scr_pos_y = 352, .pos_x = 38, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD16, .scr_pos_x = 66, .scr_pos_y = 352, .pos_x = 70, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD10, .click_event = gui_remove_area_for_traps, .scr_pos_x = 98, .scr_pos_y = 352, .pos_x = 102, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_no_anim_button, .sprite_idx = GPS_rpanel_frame_portrt_sell, .tooltip_stridx = GUIStr_SellItemDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 210, .pos_x = 8, .pos_y = 194, .width = 126, .height = 44, .draw_call = gui_area_big_trap_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_big_trap },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 110, .scr_pos_y = 215, .pos_x = 110, .pos_y = 216, .width = 16, .height = 20, .draw_call = gui_area_trap_build_info_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_buildable_info },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_NXPG, .button_flags = 1, .click_event = gui_set_page, .scr_pos_x = 78, .scr_pos_y = 188, .pos_x = 78, .pos_y = 188, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_Empty, .parent_menu = &trap_menu2, .content = { GMnu_TRAP2 }, .maintain_call = maintain_trap_next_page_button },
  { .gbtype = -1 },
};

struct GuiButtonInit trap_menu2_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD17, .scr_pos_x = 2, .scr_pos_y = 238, .pos_x = 6, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 2 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD18, .scr_pos_x = 34, .scr_pos_y = 238, .pos_x = 38, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 3 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD19, .scr_pos_x = 66, .scr_pos_y = 238, .pos_x = 70, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 4 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD20, .scr_pos_x = 98, .scr_pos_y = 238, .pos_x = 102, .pos_y = 242, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 6 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD21, .scr_pos_x = 2, .scr_pos_y = 276, .pos_x = 6, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 1 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD22, .scr_pos_x = 34, .scr_pos_y = 276, .pos_x = 38, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 5 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD23, .scr_pos_x = 66, .scr_pos_y = 276, .pos_x = 70, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD24, .scr_pos_x = 98, .scr_pos_y = 276, .pos_x = 102, .pos_y = 280, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD25, .scr_pos_x = 2, .scr_pos_y = 314, .pos_x = 6, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 7 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD26, .scr_pos_x = 34, .scr_pos_y = 314, .pos_x = 38, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 8 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD27, .scr_pos_x = 66, .scr_pos_y = 314, .pos_x = 70, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 9 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD28, .scr_pos_x = 98, .scr_pos_y = 314, .pos_x = 102, .pos_y = 318, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty, .content = { 10 } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD29, .scr_pos_x = 2, .scr_pos_y = 352, .pos_x = 6, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD30, .scr_pos_x = 34, .scr_pos_y = 352, .pos_x = 38, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD31, .scr_pos_x = 66, .scr_pos_y = 352, .pos_x = 70, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_null_button, .sprite_idx = GPS_rpanel_frame_portrt_empty, .tooltip_stridx = GUIStr_Empty },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_TD32, .click_event = gui_remove_area_for_traps, .scr_pos_x = 98, .scr_pos_y = 352, .pos_x = 102, .pos_y = 356, .width = 32, .height = 36, .draw_call = gui_area_new_no_anim_button, .sprite_idx = GPS_rpanel_frame_portrt_sell, .tooltip_stridx = GUIStr_SellItemDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 8, .scr_pos_y = 210, .pos_x = 8, .pos_y = 194, .width = 126, .height = 44, .draw_call = gui_area_big_trap_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_big_trap },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 110, .scr_pos_y = 215, .pos_x = 110, .pos_y = 216, .width = 16, .height = 20, .draw_call = gui_area_trap_build_info_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_buildable_info },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_MNFCT_NXPG, .button_flags = 1, .click_event = gui_set_page, .scr_pos_x = 78, .scr_pos_y = 188, .pos_x = 78, .pos_y = 188, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_Empty, .parent_menu = &trap_menu, .content = { GMnu_TRAP }, .maintain_call = maintain_trap_next_page_button },
  { .gbtype = -1 },
};

struct GuiButtonInit creature_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_CRTR_NXWNDR, .click_event = pick_up_next_wanderer, .rclick_event = gui_go_to_next_wanderer, .scr_pos_x = 26, .scr_pos_y = 192, .pos_x = 26, .pos_y = 192, .width = 38, .height = 24, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_tab_crtr_wandr_act, .tooltip_stridx = GUIStr_CreatureIdleDesc },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_CRTR_NXWRKR, .click_event = pick_up_next_worker, .rclick_event = gui_go_to_next_worker, .scr_pos_x = 62, .scr_pos_y = 192, .pos_x = 62, .pos_y = 192, .width = 38, .height = 24, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_tab_crtr_work_act, .tooltip_stridx = GUIStr_CreatureWorkingDesc },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_CRTR_NXFIGT, .click_event = pick_up_next_fighter, .rclick_event = gui_go_to_next_fighter, .scr_pos_x = 98, .scr_pos_y = 192, .pos_x = 98, .pos_y = 192, .width = 38, .height = 24, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_tab_crtr_fight_act, .tooltip_stridx = GUIStr_CreatureFightingDesc },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = gui_scroll_activity_up, .rclick_event = gui_scroll_activity_up, .scr_pos_x = 4, .scr_pos_y = 192, .pos_x = 4, .pos_y = 192, .width = 22, .height = 24, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_up_act, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_activity_up },
  { .gbtype = LbBtnT_HoldableBtn, .click_event = gui_scroll_activity_down, .rclick_event = gui_scroll_activity_down, .scr_pos_x = 4, .scr_pos_y = 364, .pos_x = 4, .pos_y = 364, .width = 22, .height = 24, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_down_act, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_activity_down },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_next_creature, .rclick_event = gui_go_to_next_creature, .ptover_event = gui_over_creature_button, .scr_pos_y = 196, .pos_y = 218, .width = 22, .height = 22, .draw_call = gui_area_creatrmodel_button, .tooltip_stridx = GUIStr_PickCreatrMostExpDesc, .maintain_call = maintain_activity_pic },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .scr_pos_x = 26, .scr_pos_y = 220, .pos_x = 26, .pos_y = 220, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrIdleDesc, .content = { .lptr = &activity_list[0+0] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .scr_pos_x = 62, .scr_pos_y = 220, .pos_x = 62, .pos_y = 220, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrWorkingDesc, .content = { .lptr = &activity_list[0+1] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .scr_pos_x = 98, .scr_pos_y = 220, .pos_x = 98, .pos_y = 220, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrFightingDesc, .content = { .lptr = &activity_list[0+2] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_next_creature, .rclick_event = gui_go_to_next_creature, .ptover_event = gui_over_creature_button, .btype_value = 1, .scr_pos_y = 220, .pos_y = 242, .width = 22, .height = 22, .draw_call = gui_area_creatrmodel_button, .tooltip_stridx = GUIStr_PickCreatrMostExpDesc, .maintain_call = maintain_activity_pic },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 1, .scr_pos_x = 26, .scr_pos_y = 244, .pos_x = 26, .pos_y = 244, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrIdleDesc, .content = { .lptr = &activity_list[4+0] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 1, .scr_pos_x = 62, .scr_pos_y = 244, .pos_x = 62, .pos_y = 244, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrWorkingDesc, .content = { .lptr = &activity_list[4+1] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 1, .scr_pos_x = 98, .scr_pos_y = 244, .pos_x = 98, .pos_y = 244, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrFightingDesc, .content = { .lptr = &activity_list[4+2] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_next_creature, .rclick_event = gui_go_to_next_creature, .ptover_event = gui_over_creature_button, .btype_value = 2, .scr_pos_y = 244, .pos_y = 266, .width = 22, .height = 22, .draw_call = gui_area_creatrmodel_button, .tooltip_stridx = GUIStr_PickCreatrMostExpDesc, .maintain_call = maintain_activity_pic },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 2, .scr_pos_x = 26, .scr_pos_y = 268, .pos_x = 26, .pos_y = 268, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrIdleDesc, .content = { .lptr = &activity_list[8] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 2, .scr_pos_x = 62, .scr_pos_y = 268, .pos_x = 62, .pos_y = 268, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrWorkingDesc, .content = { .lptr = &activity_list[9] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 2, .scr_pos_x = 98, .scr_pos_y = 268, .pos_x = 98, .pos_y = 268, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrFightingDesc, .content = { .lptr = &activity_list[10] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_next_creature, .rclick_event = gui_go_to_next_creature, .ptover_event = gui_over_creature_button, .btype_value = 3, .scr_pos_y = 268, .pos_y = 290, .width = 22, .height = 22, .draw_call = gui_area_creatrmodel_button, .tooltip_stridx = GUIStr_PickCreatrMostExpDesc, .maintain_call = maintain_activity_pic },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 3, .scr_pos_x = 26, .scr_pos_y = 292, .pos_x = 26, .pos_y = 292, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrIdleDesc, .content = { .lptr = &activity_list[12] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 3, .scr_pos_x = 62, .scr_pos_y = 292, .pos_x = 62, .pos_y = 292, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrWorkingDesc, .content = { .lptr = &activity_list[13] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 3, .scr_pos_x = 98, .scr_pos_y = 292, .pos_x = 98, .pos_y = 292, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrFightingDesc, .content = { .lptr = &activity_list[14] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_next_creature, .rclick_event = gui_go_to_next_creature, .ptover_event = gui_over_creature_button, .btype_value = 4, .scr_pos_y = 292, .pos_y = 314, .width = 22, .height = 22, .draw_call = gui_area_creatrmodel_button, .tooltip_stridx = GUIStr_PickCreatrMostExpDesc, .maintain_call = maintain_activity_pic },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 4, .scr_pos_x = 26, .scr_pos_y = 316, .pos_x = 26, .pos_y = 316, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrIdleDesc, .content = { .lptr = &activity_list[16] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 4, .scr_pos_x = 62, .scr_pos_y = 316, .pos_x = 62, .pos_y = 316, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrWorkingDesc, .content = { .lptr = &activity_list[17] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 4, .scr_pos_x = 98, .scr_pos_y = 316, .pos_x = 98, .pos_y = 316, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrFightingDesc, .content = { .lptr = &activity_list[18] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_next_creature, .rclick_event = gui_go_to_next_creature, .ptover_event = gui_over_creature_button, .btype_value = 5, .scr_pos_y = 314, .pos_y = 338, .width = 22, .height = 22, .draw_call = gui_area_creatrmodel_button, .tooltip_stridx = GUIStr_PickCreatrMostExpDesc, .maintain_call = maintain_activity_pic },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 5, .scr_pos_x = 26, .scr_pos_y = 340, .pos_x = 26, .pos_y = 340, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrIdleDesc, .content = { .lptr = &activity_list[20] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 5, .scr_pos_x = 62, .scr_pos_y = 340, .pos_x = 62, .pos_y = 340, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrWorkingDesc, .content = { .lptr = &activity_list[21] }, .maintain_call = maintain_activity_row },
  { .gbtype = LbBtnT_NormalBtn, .click_event = pick_up_creature_doing_activity, .rclick_event = gui_go_to_next_creature_activity, .btype_value = 5, .scr_pos_x = 98, .scr_pos_y = 340, .pos_x = 98, .pos_y = 340, .width = 32, .height = 20, .draw_call = gui_area_anger_button, .sprite_idx = GPS_rpanel_tab_crtr_annoy_lv00, .tooltip_stridx = GUIStr_PickCreatrFightingDesc, .content = { .lptr = &activity_list[22] }, .maintain_call = maintain_activity_row },
  { .gbtype = -1 },
};

struct GuiButtonInit query_menu_buttons[] = {
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_set_query, .scr_pos_x = 44, .scr_pos_y = 374, .pos_x = 44, .pos_y = 374, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_crinfo_act, .tooltip_stridx = GUIStr_GoToQueryMode, .maintain_call = maintain_query_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_2, .click_event = gui_switch_players_visible, .scr_pos_x = 14, .scr_pos_y = 374, .pos_x = 14, .pos_y = 374, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_MoreInformation, .maintain_call = maintain_player_page2 },
  { .gbtype = LbBtnT_ToggleBtn, .id_num = BID_QRY_IMPRSN, .click_event = gui_set_tend_to, .btype_value = 1, .scr_pos_x = 36, .scr_pos_y = 190, .pos_x = 36, .pos_y = 190, .width = 32, .height = 26, .draw_call = gui_area_flash_cycle_button, .sprite_idx = GPS_rpanel_tendency_prisne_act, .tooltip_stridx = GUIStr_CreatureImprisonDesc, .content = { .ptr = &tendency_button_imprison }, .maxval = 1, .maintain_call = maintain_prison_bar },
  { .gbtype = LbBtnT_ToggleBtn, .id_num = BID_QRY_FLEE, .click_event = gui_set_tend_to, .btype_value = 2, .scr_pos_x = 74, .scr_pos_y = 190, .pos_x = 74, .pos_y = 190, .width = 32, .height = 26, .draw_call = gui_area_flash_cycle_button, .sprite_idx = GPS_rpanel_tendency_fleee_act, .tooltip_stridx = GUIStr_CreatureFleeDesc, .content = { .ptr = &tendency_button_flee }, .maxval = 1, .maintain_call = maintain_flee_button },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 4, .scr_pos_y = 216, .pos_x = 4, .pos_y = 222, .width = 132, .height = 24, .draw_call = gui_area_payday_button, .sprite_idx = GPS_rpanel_rpanel_payday_counter, .tooltip_stridx = GUIStr_PayTimeDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 2, .scr_pos_y = 246, .pos_x = 2, .pos_y = 246, .width = 60, .height = 24, .draw_call = gui_area_research_bar, .sprite_idx = GPS_room_research_std_s, .tooltip_stridx = GUIStr_ResearchTimeDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 74, .scr_pos_y = 246, .pos_x = 74, .pos_y = 246, .width = 60, .height = 24, .draw_call = gui_area_workshop_bar, .sprite_idx = GPS_room_workshop_std_s, .tooltip_stridx = GUIStr_WorkshopTimeDesc },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_DUNGEON_INFO, .scr_pos_x = 74, .scr_pos_y = 274, .pos_x = 74, .pos_y = 274, .width = 60, .height = 24, .draw_call = gui_area_player_creature_info, .sprite_idx = GPS_plyrsym_symbol_player_red_std_a, .tooltip_stridx = GUIStr_NumberOfCreaturesDesc, .maintain_call = maintain_creature_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_DUNGEON_INFO, .scr_pos_x = 74, .scr_pos_y = 298, .pos_x = 74, .pos_y = 298, .width = 60, .height = 24, .draw_call = gui_area_player_creature_info, .sprite_idx = GPS_plyrsym_symbol_player_blue_std_a, .tooltip_stridx = GUIStr_NumberOfCreaturesDesc, .content = { 1 }, .maintain_call = maintain_creature_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_DUNGEON_INFO, .scr_pos_x = 74, .scr_pos_y = 322, .pos_x = 74, .pos_y = 322, .width = 60, .height = 24, .draw_call = gui_area_player_creature_info, .sprite_idx = GPS_plyrsym_symbol_player_green_std_a, .tooltip_stridx = GUIStr_NumberOfCreaturesDesc, .content = { 2 }, .maintain_call = maintain_creature_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_DUNGEON_INFO, .scr_pos_x = 74, .scr_pos_y = 346, .pos_x = 74, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_player_creature_info, .sprite_idx = GPS_plyrsym_symbol_player_yellow_std_a, .tooltip_stridx = GUIStr_NumberOfCreaturesDesc, .content = { 3 }, .maintain_call = maintain_creature_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_DUNGEON_INFO, .scr_pos_x = 4, .scr_pos_y = 274, .pos_x = 4, .pos_y = 274, .width = 60, .height = 24, .draw_call = gui_area_player_room_info, .sprite_idx = GPS_plyrsym_symbol_room_red_std_a, .tooltip_stridx = GUIStr_NumberOfRoomsDesc, .maintain_call = maintain_room_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_DUNGEON_INFO, .scr_pos_x = 4, .scr_pos_y = 298, .pos_x = 4, .pos_y = 298, .width = 60, .height = 24, .draw_call = gui_area_player_room_info, .sprite_idx = GPS_plyrsym_symbol_room_blue_std_a, .tooltip_stridx = GUIStr_NumberOfRoomsDesc, .content = { 1 }, .maintain_call = maintain_room_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_DUNGEON_INFO, .scr_pos_x = 4, .scr_pos_y = 322, .pos_x = 4, .pos_y = 322, .width = 60, .height = 24, .draw_call = gui_area_player_room_info, .sprite_idx = GPS_plyrsym_symbol_room_green_std_a, .tooltip_stridx = GUIStr_NumberOfRoomsDesc, .content = { 2 }, .maintain_call = maintain_room_button },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_DUNGEON_INFO, .scr_pos_x = 4, .scr_pos_y = 346, .pos_x = 4, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_player_room_info, .sprite_idx = GPS_plyrsym_symbol_room_yellow_std_a, .tooltip_stridx = GUIStr_NumberOfRoomsDesc, .content = { 3 }, .maintain_call = maintain_room_button },
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_toggle_ally, .scr_pos_x = 62, .scr_pos_y = 274, .pos_x = 62, .pos_y = 274, .width = 14, .height = 22, .draw_call = gui_area_ally, .tooltip_stridx = GUIStr_AllyWithPlayer, .maintain_call = maintain_ally },
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_toggle_ally, .scr_pos_x = 62, .scr_pos_y = 298, .pos_x = 62, .pos_y = 298, .width = 14, .height = 22, .draw_call = gui_area_ally, .tooltip_stridx = GUIStr_AllyWithPlayer, .content = { 1 }, .maintain_call = maintain_ally },
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_toggle_ally, .scr_pos_x = 62, .scr_pos_y = 322, .pos_x = 62, .pos_y = 322, .width = 14, .height = 22, .draw_call = gui_area_ally, .tooltip_stridx = GUIStr_AllyWithPlayer, .content = { 2 }, .maintain_call = maintain_ally },
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_toggle_ally, .scr_pos_x = 62, .scr_pos_y = 346, .pos_x = 62, .pos_y = 346, .width = 14, .height = 22, .draw_call = gui_area_ally, .tooltip_stridx = GUIStr_AllyWithPlayer, .content = { 3 }, .maintain_call = maintain_ally },
  { .gbtype = -1 },
};    

struct GuiButtonInit event_menu_buttons[] = {
  { .gbtype = -1 },
};

struct GuiButtonInit creature_query_buttons1[] = {
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .scr_pos_x = 44, .scr_pos_y = 374, .pos_x = 44, .pos_y = 374, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_MoreInformation, .parent_menu = &creature_query_menu2 },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 80, .scr_pos_y = 200, .pos_x = 80, .pos_y = 200, .width = 56, .height = 24, .draw_call = gui_area_smiley_anger_button, .sprite_idx = GPS_rpanel_bar_rounded_empty, .tooltip_stridx = GUIStr_CreatureAngerDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 80, .scr_pos_y = 230, .pos_x = 80, .pos_y = 230, .width = 56, .height = 24, .draw_call = gui_area_experience_button, .sprite_idx = GPS_rpanel_bar_rounded_full, .tooltip_stridx = GUIStr_ExperienceDesc },
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_query_next_creature_of_owner, .rclick_event = gui_query_next_creature_of_owner_and_model, .scr_pos_x = 4, .scr_pos_y = 266, .pos_x = 4, .pos_y = 266, .width = 126, .height = 14, .tooltip_stridx = GUIStr_NameAndHealthDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 4, .scr_pos_y = 290, .pos_x = 4, .pos_y = 290, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 72, .scr_pos_y = 290, .pos_x = 72, .pos_y = 290, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 1 }, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 4, .scr_pos_y = 318, .pos_x = 4, .pos_y = 318, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 2 }, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 72, .scr_pos_y = 318, .pos_x = 72, .pos_y = 318, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 3 }, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 4, .scr_pos_y = 346, .pos_x = 4, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 4 }, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .scr_pos_x = 72, .scr_pos_y = 346, .pos_x = 72, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 5 }, .maintain_call = maintain_instance },
  { .gbtype = -1 },
};

struct GuiButtonInit creature_query_buttons2[] = {
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .scr_pos_x = 44, .scr_pos_y = 374, .pos_x = 44, .pos_y = 374, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_MoreInformation, .parent_menu = &creature_query_menu3 },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 80, .scr_pos_y = 200, .pos_x = 80, .pos_y = 200, .width = 56, .height = 24, .draw_call = gui_area_smiley_anger_button, .sprite_idx = GPS_rpanel_bar_rounded_empty, .tooltip_stridx = GUIStr_CreatureAngerDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 80, .scr_pos_y = 230, .pos_x = 80, .pos_y = 230, .width = 56, .height = 24, .draw_call = gui_area_experience_button, .sprite_idx = GPS_rpanel_bar_rounded_full, .tooltip_stridx = GUIStr_ExperienceDesc },
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_query_next_creature_of_owner, .rclick_event = gui_query_next_creature_of_owner_and_model, .scr_pos_x = 4, .scr_pos_y = 266, .pos_x = 4, .pos_y = 266, .width = 126, .height = 14, .tooltip_stridx = GUIStr_NameAndHealthDesc },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 4, .scr_pos_y = 290, .pos_x = 4, .pos_y = 290, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 4 }, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 72, .scr_pos_y = 290, .pos_x = 72, .pos_y = 290, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 5 }, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 4, .scr_pos_y = 318, .pos_x = 4, .pos_y = 318, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 6 }, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 72, .scr_pos_y = 318, .pos_x = 72, .pos_y = 318, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 7 }, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .scr_pos_x = 4, .scr_pos_y = 346, .pos_x = 4, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 8 }, .maintain_call = maintain_instance },
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .scr_pos_x = 72, .scr_pos_y = 346, .pos_x = 72, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_instance_button, .tooltip_stridx = GUIStr_Empty, .content = { 9 }, .maintain_call = maintain_instance },
  { .gbtype = -1 },
};

struct GuiButtonInit creature_query_buttons3[] = {
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .scr_pos_x = 44, .scr_pos_y = 374, .pos_x = 44, .pos_y = 374, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_MoreInformation, .parent_menu = &creature_query_menu4 },
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_query_next_creature_of_owner, .rclick_event = gui_query_next_creature_of_owner_and_model, .scr_pos_x = 4, .scr_pos_y = 204, .pos_x = 4, .pos_y = 204, .width = 126, .height = 14, .tooltip_stridx = GUIStr_NameAndHealthDesc },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 226, .pos_x = 4, .pos_y = 226, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_crspell_heal_std_s, .tooltip_stridx = GUIStr_CreatureHealthDesc, .content = { CrLStat_Health } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 226, .pos_x = 72, .pos_y = 226, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_strength_std, .tooltip_stridx = GUIStr_CreatureStrengthDesc, .content = { CrLStat_Strength } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 256, .pos_x = 4, .pos_y = 256, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_armor_std, .tooltip_stridx = GUIStr_CreatureArmourDesc, .content = { CrLStat_Armour } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 256, .pos_x = 72, .pos_y = 256, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_defense_std, .tooltip_stridx = GUIStr_CreatureDefenceDesc, .content = { CrLStat_Defence } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 286, .pos_x = 4, .pos_y = 286, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_luck_std, .tooltip_stridx = GUIStr_CreatureLuckDesc, .content = { CrLStat_Luck } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 286, .pos_x = 72, .pos_y = 286, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_dexterity_std, .tooltip_stridx = GUIStr_CreatureDexterityDesc, .content = { CrLStat_Dexterity } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 316, .pos_x = 4, .pos_y = 316, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_wage_std, .tooltip_stridx = GUIStr_CreatureWageDesc, .content = { CrLStat_GoldWage } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 316, .pos_x = 72, .pos_y = 316, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_gold_std, .tooltip_stridx = GUIStr_CreatureGoldHeldDesc, .content = { CrLStat_GoldHeld } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 346, .pos_x = 4, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_age_std, .tooltip_stridx = GUIStr_CreatureTimeInDungeonDesc, .content = { CrLStat_AgeTime } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 346, .pos_x = 72, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_kills_std, .tooltip_stridx = GUIStr_CreatureKillsDesc, .content = { CrLStat_Kills } },
  { .gbtype = -1 },
};

struct GuiButtonInit creature_query_buttons4[] = {
  { .gbtype = LbBtnT_NormalBtn, .button_flags = 1, .scr_pos_x = 44, .scr_pos_y = 374, .pos_x = 44, .pos_y = 374, .width = 52, .height = 20, .draw_call = gui_area_new_normal_button, .sprite_idx = GPS_rpanel_rpanel_btn_nxpage_act, .tooltip_stridx = GUIStr_MoreInformation, .parent_menu = &creature_query_menu1 },
  { .gbtype = LbBtnT_NormalBtn, .click_event = gui_query_next_creature_of_owner, .rclick_event = gui_query_next_creature_of_owner_and_model, .scr_pos_x = 4, .scr_pos_y = 204, .pos_x = 4, .pos_y = 204, .width = 126, .height = 14, .tooltip_stridx = GUIStr_NameAndHealthDesc },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 226, .pos_x = 4, .pos_y = 226, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_crspell_speedup_dis_s, .tooltip_stridx = GUIStr_CreatureSpeedDesc, .content = { CrLStat_Speed } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 226, .pos_x = 72, .pos_y = 226, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_crspell_whip_std_s, .tooltip_stridx = GUIStr_CreatureLoyaltyDesc, .content = { CrLStat_Loyalty } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 256, .pos_x = 4, .pos_y = 256, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_room_research_std_s, .tooltip_stridx = GUIStr_CreatureResrchSkillDesc, .content = { CrLStat_ResearchSkill } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 256, .pos_x = 72, .pos_y = 256, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_room_workshop_std_s, .tooltip_stridx = GUIStr_CreatureManfctrSkillDesc, .content = { CrLStat_ManufactureSkill } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 286, .pos_x = 4, .pos_y = 286, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_room_training_std_s, .tooltip_stridx = GUIStr_CreatureTraingSkillDesc, .content = { CrLStat_TrainingSkill } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 286, .pos_x = 72, .pos_y = 286, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_room_scavenge_std_s, .tooltip_stridx = GUIStr_CreatureScavngSkillDesc, .content = { CrLStat_ScavengeSkill } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 316, .pos_x = 4, .pos_y = 316, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_traingcst_std, .tooltip_stridx = GUIStr_CreatureTraingCostDesc, .content = { CrLStat_TrainingCost } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 316, .pos_x = 72, .pos_y = 316, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_scavngcst_std, .tooltip_stridx = GUIStr_CreatureScavngCostDesc, .content = { CrLStat_ScavengeCost } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 4, .scr_pos_y = 346, .pos_x = 4, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_trapdoor_trap_boulder_std_s, .tooltip_stridx = GUIStr_CreatureWeightDesc, .content = { CrLStat_Weight } },
  { .gbtype = LbBtnT_NormalBtn, .id_num = BID_QUERY_INFO, .scr_pos_x = 72, .scr_pos_y = 346, .pos_x = 72, .pos_y = 346, .width = 60, .height = 24, .draw_call = gui_area_stat_button, .sprite_idx = GPS_symbols_creatr_stat_blood_std, .tooltip_stridx = GUIStr_CreatureBloodTypeDesc, .content = { CrLStat_BloodType } },
  { .gbtype = -1 },
};
#pragma GCC diagnostic pop

struct GuiMenu main_menu =
 {           GMnu_MAIN, 0, 1, main_menu_buttons,                    0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu room_menu =
 {           GMnu_ROOM, 0, 1, room_menu_buttons,                    0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 1,};
struct GuiMenu spell_menu =
 {          GMnu_SPELL, 0, 1, spell_menu_buttons,                   0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 1,};
struct GuiMenu spell_lost_menu =
 {     GMnu_SPELL_LOST, 0, 1, spell_lost_menu_buttons,              0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 1,};
struct GuiMenu trap_menu =
 {           GMnu_TRAP, 0, 1, trap_menu_buttons,                    0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 1,};
struct GuiMenu creature_menu =
 {       GMnu_CREATURE, 0, 1, creature_menu_buttons,                0,   0, 140, 400, gui_activity_background,     0, NULL,    NULL,                    0, 0, 1,};
struct GuiMenu query_menu =
 {          GMnu_QUERY, 0, 1, query_menu_buttons,                   0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 1,};
struct GuiMenu event_menu =
 {          GMnu_EVENT, 0, 1, event_menu_buttons,                   0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu creature_query_menu1 =
 {GMnu_CREATURE_QUERY1, 0, 1, creature_query_buttons1,              0,   0, 140, 400, gui_creature_query_background1,0,NULL,   NULL,                    0, 0, 1,};
struct GuiMenu creature_query_menu2 =
 {GMnu_CREATURE_QUERY2, 0, 1, creature_query_buttons2,              0,   0, 140, 400, gui_creature_query_background1,0,NULL,   NULL,                    0, 0, 1,};
struct GuiMenu creature_query_menu3 =
 {GMnu_CREATURE_QUERY3, 0, 1, creature_query_buttons3,              0,   0, 140, 400, gui_creature_query_background2,0,NULL,   NULL,                    0, 0, 1,};
struct GuiMenu creature_query_menu4 =
 {GMnu_CREATURE_QUERY4, 0, 1, creature_query_buttons4,              0,   0, 140, 400, gui_creature_query_background2,0,NULL,   NULL,                    0, 0, 1,};
struct GuiMenu spell_menu2 =
 {          GMnu_SPELL2, 0, 1, spell_menu2_buttons,                 0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 1,};
 struct GuiMenu room_menu2 =
 {          GMnu_ROOM2, 0, 1, room_menu2_buttons,                   0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 1,};
 struct GuiMenu trap_menu2 =
 {          GMnu_TRAP2, 0, 1, trap_menu2_buttons,                   0,   0, 140, 400, NULL,                        0, NULL,    NULL,                    0, 0, 1,};

struct TiledSprite status_panel = {
    2, 4, {
        { 1, 2,},
        { 3, 4,},
        { 5, 6,},
        {21,22,},
    },
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
