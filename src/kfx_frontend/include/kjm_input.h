/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kjm_input.h
 *     Header file for kjm_input.c.
 * @par Purpose:
 *     Keyboard-Joypad-Mouse input routines.
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
#ifndef DK_KJMINPUT_H
#define DK_KJMINPUT_H

#include "bflib_basics.h"
#include "globals.h"

#include "bflib_keybrd.h" // is_key_pressed/clear_key_pressed/key_modifiers (moved there, S03)
#include "bflib_mouse.h"  // GetMouseX/GetMouseY/is_mouse_pressed_lrbutton (moved there, S03)

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct KeyToStringInit { // sizeof = 5
  unsigned char chr;
  int64_t str_idx;
};

struct TbSpriteSheet;

/******************************************************************************/
extern int64_t defining_a_key;
extern int64_t defining_a_key_id;
// docs/refactor/editor/10-definable-keybindings.md -- which table
// defining_a_key_id indexes into (false = game_key_settings[]/
// settings.kbkeys[], true = editor_key_settings[]/settings.editor_kbkeys[]).
extern TbBool defining_editor_key;

extern int64_t left_button_held_x;
extern int64_t left_button_held_y;
extern int64_t left_button_double_clicked_y;
extern int64_t left_button_double_clicked_x;
extern int64_t right_button_double_clicked_y;
extern int64_t right_button_double_clicked_x;
extern char right_button_clicked;
extern char left_button_clicked;
extern int64_t right_button_released_x;
extern int64_t right_button_released_y;
extern char right_button_double_clicked;
extern int64_t left_button_released_y;
extern int64_t left_button_released_x;
extern char left_button_double_clicked;
extern char right_button_released;
extern char right_button_held;
extern int64_t right_button_click_space_count;
extern int64_t right_button_held_y;
extern int64_t left_button_clicked_y;
extern int64_t left_button_clicked_x;
extern int64_t left_button_click_space_count;
extern int64_t right_button_held_x;
extern char left_button_released;
extern int64_t right_button_clicked_y;
extern int64_t right_button_clicked_x;
extern char left_button_held;

extern int64_t key_to_string[256];

#pragma pack()
/******************************************************************************/
extern TbBool defined_keys_that_have_been_swapped[];
// docs/refactor/editor/10-definable-keybindings.md -- editor keys' own
// parallel array (settings.editor_kbkeys[]/EditorGameKeys).
extern TbBool defined_editor_keys_that_have_been_swapped[];
extern TbBool wheel_scrolled_up;
extern TbBool wheel_scrolled_down;

TbBool poll_inputs(void);

void clear_mouse_pressed_lrbutton(void);
void update_mouse(void);
void update_wheel_scrolled(void);

void define_key_input(void);
void init_key_to_strings(void);
TbBool add_input_text_to_message(char *message, int64_t max_message_length, struct TbSpriteSheet *font, int64_t max_width);

TbBool mouse_is_over_panel_map(ScreenCoord x, ScreenCoord y);
TbBool mouse_is_over_side_panel_bottom();

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
