/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_frontbtns.h
 *     Header file for gui_frontbtns.c.
 * @par Purpose:
 *     gui_frontbtns functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_GUI_FRONTBTNS_H
#define DK_GUI_FRONTBTNS_H

#include "globals.h"
#include "bflib_guibtns.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)


#pragma pack()
/******************************************************************************/
int64_t guibutton_get_unused_slot(void);

void gui_clear_buttons_not_over_mouse(int64_t gmbtn_idx);
TbBool gui_button_release_inputs(int64_t gmbtn_idx);
TbBool gui_slider_button_inputs(int64_t gbtn_idx);
TbBool gui_slider_button_mouse_over_slider_tracker(int64_t gbtn_idx);
TbBool gui_button_click_inputs(int64_t gmbtn_idx);
void fake_button_click(int64_t gmbtn_idx);
void gui_set_menu_mode(struct GuiButton *gbtn);

void gui_pretty_background(struct GuiMenu *gmnu);
void gui_round_glass_background(struct GuiMenu *gmnu);
void frontend_copy_background(void);

void gui_area_new_normal_button(struct GuiButton *gbtn);
void gui_area_new_vertical_button(struct GuiButton *gbtn);
void gui_area_new_null_button(struct GuiButton *gbtn);
void gui_area_new_no_anim_button(struct GuiButton *gbtn);
void gui_area_creatrmodel_button(struct GuiButton *gbtn);
void gui_area_compsetting_button(struct GuiButton *gbtn);
void gui_area_no_anim_button(struct GuiButton *gbtn);
void gui_area_normal_button(struct GuiButton *gbtn);
void gui_area_null(struct GuiButton *gbtn);
void gui_area_flash_cycle_button(struct GuiButton *gbtn);

void gui_draw_tab(struct GuiButton *gbtn);
void gui_draw_scroll_box(struct GuiButton *gbtn, int64_t height_lines, TbBool draw_scrollbar);
void gui_draw_scroll_box_cropped(struct GuiButton *gbtn, TbBool draw_scrollbar);
void frontend_over_button(struct GuiButton *gbtn);
void frontend_draw_button(struct GuiButton *gbtn, int64_t btntype, const char *text, uint64_t drw_flags);
void frontend_draw_large_menu_button(struct GuiButton *gbtn);
int64_t frontend_button_chrome_repeat_count(int64_t width, int64_t left_w, int64_t right_w, int64_t mid_w);
int64_t frontend_draw_button_chrome_flexible(struct GuiButton *gbtn, uint64_t spridx, int64_t units_per_px);
void frontend_draw_button_icon(struct GuiButton *gbtn);
int64_t frontend_button_chrome_fit_width(uint64_t spridx, int64_t units_per_px, int64_t min_width);
void frontend_draw_scroll_box_tab(struct GuiButton *gbtn);
void frontend_draw_scroll_box(struct GuiButton *gbtn);

void reset_scroll_window(struct GuiMenu *gmnu);
void clear_radio_buttons(struct GuiMenu *gmnu);
void update_radio_button_data(struct GuiMenu *gmnu);
void frontend_draw_slider(struct GuiButton *gbtn);
void frontend_draw_small_slider(struct GuiButton *gbtn);
void frontend_draw_slider_button(struct GuiButton *gbtn);

void init_slider_bars(struct GuiMenu *gmnu);
void init_menu_buttons(struct GuiMenu *gmnu);
void kill_button_area_input(void);
void kill_button(struct GuiButton *gbtn);
void setup_radio_buttons(struct GuiMenu *gmnu);

struct GuiButton* get_gui_button(int64_t id);
struct GuiButtonInit* get_gui_button_init(struct GuiMenu * menu, int64_t id);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
