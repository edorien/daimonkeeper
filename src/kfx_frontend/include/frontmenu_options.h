/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_options.h
 *     Header file for frontmenu_options.c.
 * @par Purpose:
 *     GUI menus for game options.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 09 Oct 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_FRONTMENU_OPTS_H
#define DK_FRONTMENU_OPTS_H

#include "globals.h"
#include "bflib_guibtns.h"
#include "frontmenu_settingctrl.h"
#include <stddef.h> // size_t
#include <stdint.h> // uint8_t

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

#define GAMMA_LEVELS_COUNT      5

enum OptionsButtonDesignationIDs {
    BID_SOUND_VOL  = BID_DEFAULT + 75,
    BID_MUSIC_VOL  = BID_DEFAULT + 76,
    BID_MENTOR_VOL = BID_DEFAULT + 77,
    BID_MOUSE_MUL  = BID_DEFAULT + 78,
};

struct GuiMenu;
struct GuiButton;

/******************************************************************************/
extern char video_cluedo_mode;
extern char video_shadows;
extern char video_textures;
extern char video_view_distance_level;

#pragma pack()
/******************************************************************************/
extern struct GuiMenu frontend_define_keys_menu;
#define frontend_define_keys_menu_items_visible  10
extern struct GuiMenu frontend_option_menu;
/******************************************************************************/
// The FrontendSliderCtrl/FrontendCheckboxCtrl bindings behind the sliders/
// checkbox above, exposed (no longer `static`) so kfx_frontend's ImGui
// FeSt_FEOPTIONS screen (docs/refactor/renderer/04-imgui-gui-foundation.md
// Phase C) can bind to the exact same settings.<field> get/set pair rather
// than duplicating it -- single source of truth for both draw paths.
extern const struct FrontendSliderCtrl sound_volume_ctrl;
extern const struct FrontendSliderCtrl music_volume_ctrl;
extern const struct FrontendSliderCtrl mentor_volume_ctrl;
extern const struct FrontendSliderCtrl mouse_sensitivity_ctrl;
extern const struct FrontendCheckboxCtrl mouse_invert_ctrl;
/******************************************************************************/
void frontend_define_key_up(struct GuiButton *gbtn);
void frontend_define_key_down(struct GuiButton *gbtn);
void frontend_define_key_scroll(struct GuiButton *gbtn);
void frontend_define_key(struct GuiButton *gbtn);
void frontend_define_key_up_maintain(struct GuiButton *gbtn);
void frontend_define_key_down_maintain(struct GuiButton *gbtn);
void frontend_define_key_maintain(struct GuiButton *gbtn);
void frontend_draw_define_key_scroll_tab(struct GuiButton *gbtn);
void frontend_draw_define_key(struct GuiButton *gbtn);
void frontend_format_key_binding(long key_id, char *text, size_t text_size);
// docs/refactor/editor/10-definable-keybindings.md -- editor keys' own
// counterpart (settings.editor_kbkeys[]/EditorGameKeys, globals.h).
void frontend_format_editor_key_binding(long key_id, char *text, size_t text_size);
uint8_t num_definable_keys(void);
void frontend_set_mouse_sensitivity(struct GuiButton *gbtn);
void frontend_invert_mouse(struct GuiButton *gbtn);
void frontend_draw_invert_mouse(struct GuiButton *gbtn);
void gui_video_shadows(struct GuiButton *gbtn);
void gui_video_view_distance_level(struct GuiButton *gbtn);
void gui_video_rotate_mode(struct GuiButton *gbtn);
void gui_video_cluedo_mode(struct GuiButton *gbtn);
void gui_video_gamma_correction(struct GuiButton *gbtn);
void gui_video_cluedo_maintain(struct GuiButton *gbtn);
void gui_set_sound_volume(struct GuiButton *gbtn);
void gui_set_music_volume(struct GuiButton *gbtn);
void gui_set_mentor_volume(struct GuiButton *gbtn);
void init_video_menu(struct GuiMenu *gmnu);
void init_audio_menu(struct GuiMenu *gmnu);
void frontend_options_menu_init_sliders(struct GuiMenu *gmnu);
/******************************************************************************/
int make_audio_slider_linear(int a);
int make_audio_slider_nonlinear(int a);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
