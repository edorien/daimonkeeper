/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_draw.h
 *     Header file for gui_draw.c.
 * @par Purpose:
 *     GUI elements drawing functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     20 Jan 2009 - 30 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_GUIDRAW_H
#define DK_GUIDRAW_H

#include "bflib_basics.h"
#include "bflib_video.h"
#include "bflib_sprite.h"
#include "globals.h"
#include "vidmode.h"

// Sprites
// Maybe "Count + 1"? there is no sprite#517
#define GUI_SLAB_DIMENSION 64
// Positioning constants for menus
#define POS_AUTO -9999
#define POS_MOUSMID -999
#define POS_MOUSPRV -998
#define POS_SCRCTR  -997
#define POS_SCRBTM  -996
#define POS_GAMECTR  999
#define ROUNDSLAB64K_LIGHT 0
#define ROUNDSLAB64K_DARK 1

// Moved here from frontmenu_ingame_tabs.h (stage 10,
// docs/refactor/stage-10-kfx-frontend.md) -- gui_parchment.c
// (kfx_frontend's lower internal sub-layer) needed the pixel-scaling
// helpers without depending on frontmenu_ingame_tabs.h.
#define AROUND_2x2_PIXEL      4
#define AROUND_3x3_PIXEL      9
#define AROUND_4x4_PIXEL      16
#define AROUND_5x5_PIXEL      25
#define AROUND_6x6_PIXEL      36

#define ONE_PIXEL       2048
#define TWO_PIXELS      1024
#define THREE_PIXELS     512
#define FOUR_PIXELS      256
#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct GuiButton;
/******************************************************************************/
// gui_panel_sprites/frontend_sprite/gui_slab moved to kfx_render's
// vidmode.h (stage 13.3, docs/refactor/stage-13-enforce-and-document.md).
extern unsigned char *frontend_background;
extern int64_t gui_blink_rate;
extern int64_t neutral_flash_rate;
// Moved here from frontmenu_ingame_tabs.h (stage 10,
// docs/refactor/stage-10-kfx-frontend.md).
extern char gui_room_type_highlighted;
extern char gui_door_type_highlighted;

#pragma pack()
/******************************************************************************/
extern char gui_textbuf[TEXT_BUFFER_LENGTH];
extern const int64_t pixels_needed[];
// draw_square moved to kfx_sim's power_hand.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md).
/******************************************************************************/
int64_t get_pixels_scaled_and_zoomed(int64_t basic_zoom);
int64_t scale_pixel(int64_t basic_zoom);
int64_t simple_button_sprite_height_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction);
int64_t simple_button_sprite_width_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction);
int64_t simple_frontend_sprite_height_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction);
int64_t simple_frontend_sprite_width_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction);
int64_t simple_gui_panel_sprite_height_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction);
int64_t simple_gui_panel_sprite_width_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction);

// Moved here from front_simple.h (stage 10,
// docs/refactor/stage-10-kfx-frontend.md).
TbBool copy_raw8_image_buffer(TbPixel *dst_buf,const int64_t scanline,const int64_t nlines,const int64_t dst_width,const int64_t dst_height,
    const int64_t spw,const int64_t sph,const unsigned char *src_buf,const int64_t src_width,const int64_t src_height);

// Rect-clipped sibling of copy_raw8_image_buffer -- see its doc comment in
// gui_draw.c. Needed to embed a panned raw image (e.g. the landview
// backdrop) inside a panel alongside other UI without blanking pixels
// outside its own rect.
TbBool copy_raw8_image_buffer_rect(TbPixel *dst_buf,const int64_t scanline,const int64_t nlines,
    const int64_t rect_x,const int64_t rect_y,const int64_t rect_w,const int64_t rect_h,
    const int64_t dst_width,const int64_t dst_height,const int64_t spw,const int64_t sph,
    const unsigned char *src_buf,const int64_t src_width,const int64_t src_height);

// 32-bit counterparts, for images loaded as true colour (the start-up
// splash/legal PNGs): a tent-filter resize and an unscaled whole-screen copy.
TbBool resample_rgba_image(const TbPixel *src, int64_t src_width, int64_t src_height,
    TbPixel *dst, int64_t dst_width, int64_t dst_height);
TbBool copy_rgba_image_buffer(TbPixel *dst_buf, const int64_t scanline, const int64_t nlines,
    const int64_t pos_x, const int64_t pos_y, const TbPixel *src_buf, const int64_t src_width, const int64_t src_height);

void draw_bar64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width);
void draw_lit_bar64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width);
void draw_slab64k_background(int64_t pos_x, int64_t pos_y, int64_t width, int64_t height);
/** The tiling itself; draw_slab64k_background routes through the renderer first. */
void draw_slab64k_background_immediate(int64_t pos_x, int64_t pos_y, int64_t width, int64_t height);
void draw_slab64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height);
void draw_ornate_slab64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height);
void draw_ornate_slab_outline64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height);
void draw_round_slab64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height, int64_t style_type);
void draw_string64k(int64_t x, int64_t y, int64_t units_per_px, const char * text);

void draw_button_string(struct GuiButton *gbtn, int64_t base_width, const char *text);
TbBool draw_text_box(const char *text);
TbBool draw_text_box_top(const char* text, uint64_t drawflags);
void draw_scroll_box(struct GuiButton *gbtn, int64_t units_per_px, int64_t num_rows);
int64_t scroll_box_get_units_per_px(struct GuiButton *gbtn);

#define draw_gui_panel_sprite_left(x, y, units_per_px, spridx) draw_gui_panel_sprite_left_player(x, y, units_per_px, spridx, my_player_number)
void draw_gui_panel_sprite_left_player(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx, PlayerNumber plyr_idx);
#define draw_gui_panel_sprite_rmleft(x, y, units_per_px, spridx, remap) draw_gui_panel_sprite_rmleft_player(x, y, units_per_px, spridx, remap, my_player_number)
void draw_gui_panel_sprite_rmleft_player(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx, uint64_t remap, PlayerNumber plyr_idx);
void draw_gui_panel_sprite_centered(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx);
void draw_gui_panel_sprite_occentered(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx, TbPixel color);
void draw_button_sprite_left(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx);
void draw_button_sprite_rmleft(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx, uint64_t remap);

void draw_frontend_sprite_left(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx);

void draw_frontmenu_background(int64_t rect_x,int64_t rect_y,int64_t rect_w,int64_t rect_h);
// Had real external linkage but no header declaration at all (same
// situation as config_settings.c's own setup_default_settings() before it
// got one) -- added so FeStyleGetMenuBackdropTexture() (frontgui_style.cpp,
// docs/refactor/renderer/05-imgui-owned-menu-backdrop.md Phase A) can reuse
// this exact aspect-fit math instead of re-deriving it.
struct TbRect;
int64_t get_frontmenu_background_area_rect(int64_t rect_x, int64_t rect_y, int64_t rect_w, int64_t rect_h, struct TbRect *bkgnd_area);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
