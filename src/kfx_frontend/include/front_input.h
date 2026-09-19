/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_input.h
 *     Header file for front_input.c.
 * @par Purpose:
 *     Front-end user keyboard and mouse input.
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

#ifndef DK_FRONTINPUT_H
#define DK_FRONTINPUT_H

#include "bflib_basics.h"
#include "globals.h"
#include "sim_feedback.h"
#include "config_settings.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

// enum GameKeys/GAME_KEYS_COUNT moved to kfx_platform's globals.h
// (stage 13.3) -- pure name/ID vocabulary, no functional coupling here.

// enum BindingMenuVisibility moved to kfx_config's config_settings.h
// (2026-08-29, docs/refactor/todo/check-layering-symbol-level-blind-spot.md),
// alongside game_key_settings[] -- pure name/ID vocabulary, no functional
// coupling here.

// enum TbButtonFrontendFlags moved to bflib_guibtns.h (stage 10,
// docs/refactor/stage-10-kfx-frontend.md).

// Rudimentary GUI Layer support
// Add layers to this enum to distinguish between input layers
// This allows conflicting use of the same input to be resolved sensibly
// e.g. `GuiLayer_OneClick` is supposed to signify that the user is in "one-click mode"
enum GuiLayers {
    GuiLayer_Default  = 0,
    GuiLayer_OneClick,
    GuiLayer_OneClickBridgeBuild,
};

struct GuiLayer {
    long current_gui_layer;
};

// struct GamekeySettings/game_key_settings[] moved to kfx_config's
// config_settings.h (stage 13.3) -- config_settings.c reads its fields
// by value and is the lowest-ranked real consumer.

enum ZoomToMouseOptions
{
    ZoomToMouse_Never = 1,
    ZoomToMouse_Wheel = 2,
    ZoomToMouse_Always = 3
};
extern enum ZoomToMouseOptions zoom_to_mouse_option;

enum RotateAroundMouseOptions
{
    RotateAroundMouse_Never = 1,
    RotateAroundMouse_RotationKeys = 2,
    RotateAroundMouse_MovementKeys = 3,
    RotateAroundMouse_Always = 4
};
extern enum RotateAroundMouseOptions rotate_around_mouse_option;

#pragma pack()
/******************************************************************************/
extern long old_mx;
extern long old_my;
/******************************************************************************/
void input(void);
short get_screen_capture_inputs(void);
int is_game_key_pressed(long key_id, TbBool clear_pressed, TbBool ignore_mods);
// docs/refactor/editor/10-definable-keybindings.md -- editor keys' own
// checker (settings.editor_kbkeys[]/EditorGameKeys, globals.h), not
// GameKeys/settings.kbkeys[].
int is_editor_key_pressed(long key_id, TbBool clear_pressed, TbBool ignore_mods);
short game_is_busy_doing_gui_string_input(void);
short get_gui_inputs(short gameplay_on);
#define ZOOM_KEY_ROOMS_COUNT   15
extern unsigned short const zoom_key_room_order[];
unsigned short get_zoom_key_room_order(long idx);
TbBool check_current_gui_layer(long layer_id);
TbBool process_cheat_heart_health_inputs(HitPoints *value, HitPoints max_health);
TbControllerButtons get_game_key_controller_buttons(long key_id);
float get_game_key_axis_value(long key_id, TbBool ignore_mods);

void toggle_hero_health_flowers(void);
void update_time(void);
// struct GameTime moved to sim_feedback.h (kfx_config) -- see include below.
void get_game_time(struct GameTime *GT, unsigned long turns, unsigned long fps);
void update_game_time(struct GameTime *GT, unsigned long *gameseconds);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
