/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_frontmenu.h
 *     Header file for gui_frontmenu.c.
 * @par Purpose:
 *     GUI Menus support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     28 May 2010 - 12 Jun 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_GUI_FRONTMENU_H
#define DK_GUI_FRONTMENU_H

#include "globals.h"
#include "bflib_guibtns.h"

#define ACTIVE_MENUS_COUNT           8

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// enum GUI_Menus, MENU_INVALID_ID, MenuID, MenuNumber moved to globals.h
// (stage 9, docs/refactor/stage-09-kfx-game.md) -- kfx_game needs the
// menu-ID vocabulary without this file's menu-management functions.

#pragma pack(1)

struct GuiMenu;
struct GuiButton;

/******************************************************************************/

extern char no_of_active_menus;
extern unsigned char menu_stack[ACTIVE_MENUS_COUNT];
extern struct GuiMenu active_menus[ACTIVE_MENUS_COUNT];
// Moved here from frontmenu_ingame_evnt.h (stage 10,
// docs/refactor/stage-10-kfx-frontend.md) -- gui_frontmenu.c
// (kfx_frontend's lower internal sub-layer) needed it without depending
// on frontmenu_ingame_evnt.h.
extern EventIndex my_visible_event_idx;

#pragma pack()
/******************************************************************************/
struct GuiMenu *get_active_menu(MenuNumber num);
void refresh_active_button_sprites_for_player(PlayerNumber plyr_idx);
MenuNumber menu_id_to_number(MenuID menu_id);
int64_t first_monopoly_menu(void);
int64_t point_is_over_gui_menu(int64_t x, int64_t y);
void update_busy_doing_gui_on_menu(void);

void turn_on_menu(MenuID idx);
void turn_off_menu(MenuID mnu_idx);
void update_query_menu();
void turn_off_query_menus(void);
void turn_off_all_menus(void);
int64_t turn_off_all_window_menus(void);
int64_t turn_off_all_bottom_menus(void);
void turn_on_main_panel_menu(void);
void turn_off_all_panel_menus(void);
void set_menu_mode(int64_t mnu_idx);
void set_menu_visible_on(MenuID menu_id);
void set_menu_visible_off(MenuID menu_id);
void turn_off_event_box_if_necessary(PlayerNumber plyr_idx, unsigned char event_idx);

void kill_menu(struct GuiMenu *gmnu);
void remove_from_menu_stack(int64_t mnu_id);
void add_to_menu_stack(unsigned char mnu_idx);
int64_t first_available_menu(void);
void reset_gui_based_on_player_mode(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
