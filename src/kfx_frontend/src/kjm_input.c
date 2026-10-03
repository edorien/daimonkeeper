/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kjm_input.c
 *     Keyboard-Joypad-Mouse input routines.
 * @par Purpose:
 *     Allows reading state of input devices.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     20 Jan 2009 - 30 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kjm_input.h"
#include <math.h>

#include "globals.h"
#include "bflib_basics.h"

#include "bflib_video.h"
#include "bflib_keybrd.h"
#include "bflib_mouse.h"
#include "bflib_joyst.h"
#include "bflib_planar.h"
#include "bflib_math.h"
#include "bflib_sprfnt.h"
#include "bflib_text.h"
#include "bflib_inputctrl.h"
#include "bflib_datetm.h"

#include "button_snapping.h"
#include "config_settings.h"
#include "config_strings.h"
#include "frontend.h"
#include "front_input.h"
#include "frontmenu_ingame_map.h"
#include "game_legacy.h"
#include "config_keeperfx.h" // keeperfx_ui_config.hud_position -- GUI_POSITION
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
TbBool wheel_scrolled_up;
TbBool wheel_scrolled_down;

int64_t defining_a_key;
int64_t defining_a_key_id;
// docs/refactor/editor/10-definable-keybindings.md -- which table
// defining_a_key_id indexes into: false = settings.kbkeys[]/GameKeys
// (game_key_settings[]), true = settings.editor_kbkeys[]/EditorGameKeys
// (editor_key_settings[]). Both tables start at index 0, so this
// discriminator is required, not just a convenience.
TbBool defining_editor_key;

int64_t left_button_held_x;
int64_t left_button_held_y;
int64_t left_button_double_clicked_y;
int64_t left_button_double_clicked_x;
int64_t right_button_double_clicked_y;
int64_t right_button_double_clicked_x;
char right_button_clicked;
char left_button_clicked;
int64_t right_button_released_x;
int64_t right_button_released_y;
char right_button_double_clicked;
int64_t left_button_released_y;
int64_t left_button_released_x;
char left_button_double_clicked;
char right_button_released;
char right_button_held;
int64_t right_button_click_space_count;
int64_t right_button_held_y;
int64_t left_button_clicked_y;
int64_t left_button_clicked_x;
int64_t left_button_click_space_count;
int64_t right_button_held_x;
char left_button_released;
int64_t right_button_clicked_y;
int64_t right_button_clicked_x;
char left_button_held;

int64_t key_to_string[256];

/** Initialization array, used to create array which stores index of text name of keyboard keys. */
struct KeyToStringInit key_to_string_init[] = {

  {KC_A,  -'A'},
  {KC_B,  -'B'},
  {KC_C,  -'C'},
  {KC_D,  -'D'},
  {KC_E,  -'E'},
  {KC_F,  -'F'},
  {KC_G,  -'G'},
  {KC_H,  -'H'},
  {KC_I,  -'I'},
  {KC_J,  -'J'},
  {KC_K,  -'K'},
  {KC_L,  -'L'},
  {KC_M,  -'M'},
  {KC_N,  -'N'},
  {KC_O,  -'O'},
  {KC_P,  -'P'},
  {KC_Q,  -'Q'},
  {KC_R,  -'R'},
  {KC_S,  -'S'},
  {KC_T,  -'T'},
  {KC_U,  -'U'},
  {KC_V,  -'V'},
  {KC_W,  -'W'},
  {KC_X,  -'X'},
  {KC_Y,  -'Y'},
  {KC_Z,  -'Z'},
  {KC_F1,  GUIStr_KeyF1},
  {KC_F2,  GUIStr_KeyF2},
  {KC_F3,  GUIStr_KeyF3},
  {KC_F4,  GUIStr_KeyF4},
  {KC_F5,  GUIStr_KeyF5},
  {KC_F6,  GUIStr_KeyF6},
  {KC_F7,  GUIStr_KeyF7},
  {KC_F8,  GUIStr_KeyF8},
  {KC_F9,  GUIStr_KeyF9},
  {KC_F10, GUIStr_KeyF10},
  {KC_F11, GUIStr_KeyF11},
  {KC_F12, GUIStr_KeyF12},
  {KC_CAPITAL, GUIStr_KeyCapsLock},
  {KC_LSHIFT,  GUIStr_KeyLeftShift},
  {KC_RSHIFT,  GUIStr_KeyRightShift},
  {KC_LCONTROL, GUIStr_KeyLeftControl},
  {KC_RCONTROL, GUIStr_KeyRightControl},
  {KC_RETURN,  GUIStr_KeyReturn},
  {KC_BACK,    GUIStr_KeyBackspace},
  {KC_INSERT,  GUIStr_KeyInsert},
  {KC_DELETE,  GUIStr_KeyDelete},
  {KC_HOME,    GUIStr_KeyHome},
  {KC_END,     GUIStr_KeyEnd},
  {KC_PGUP,    GUIStr_KeyPageUp},
  {KC_PGDOWN,  GUIStr_KeyPageDown},
  {KC_NUMLOCK, GUIStr_KeyNumLock},
  {KC_DIVIDE,  GUIStr_KeyNumSlash},
  {KC_MULTIPLY, GUIStr_KeyNumMul},
  {KC_NUMPADENTER, GUIStr_KeyNumEnter},
  {KC_DECIMAL, GUIStr_KeyNumDelete},
  {KC_NUMPAD0, GUIStr_KeyNum0},
  {KC_NUMPAD1, GUIStr_KeyNum1},
  {KC_NUMPAD2, GUIStr_KeyNum2},
  {KC_NUMPAD3, GUIStr_KeyNum3},
  {KC_NUMPAD4, GUIStr_KeyNum4},
  {KC_NUMPAD5, GUIStr_KeyNum5},
  {KC_NUMPAD6, GUIStr_KeyNum6},
  {KC_NUMPAD7, GUIStr_KeyNum7},
  {KC_NUMPAD8, GUIStr_KeyNum8},
  {KC_NUMPAD9, GUIStr_KeyNum9},
  {KC_UP,     GUIStr_KeyUp},
  {KC_DOWN,   GUIStr_KeyDown},
  {KC_LEFT,   GUIStr_KeyLeft},
  {KC_RIGHT,  GUIStr_KeyRight},
  {KC_LALT,   GUIStr_KeyLeftAlt},
  {KC_RALT,   GUIStr_KeyRightAlt},
  {KC_MOUSE3,          GUIStr_MouseButton},
  {KC_MOUSEWHEEL_UP,   GUIStr_MouseScrollWheelUp},
  {KC_MOUSEWHEEL_DOWN, GUIStr_MouseScrollWheelDown},
  {KC_MOUSE4,          GUIStr_MouseButton},
  {KC_MOUSE5,          GUIStr_MouseButton},
  {KC_MOUSE6,          GUIStr_MouseButton},
  {KC_MOUSE7,          GUIStr_MouseButton},
  {KC_MOUSE8,          GUIStr_MouseButton},
  {KC_MOUSE9,          GUIStr_MouseButton},
  {KC_ADD,             -'+'},
  {KC_SUBTRACT,        -'-'},
  {KC_GRAVE,           GUIStr_KeyGrave},
  {KC_SEMICOLON,       -';'},
  {KC_SLASH,           -'/'},
  {KC_COMMA,           -','},
  {KC_TAB,             GUIStr_KeyTab},
  {KC_SPACE,           GUIStr_KeySpace},
  {KC_COLON,           -':'},
  {KC_EQUALS,          -'='},
  {KC_MINUS,           -'-'},
  {  0,     0},
};

// An array of the defined keys, when an indexed key is true in this array,
// it should be highlighted in font color #3 in the list, to show that it was swapped
TbBool defined_keys_that_have_been_swapped[GAME_KEYS_COUNT] = { false };
// docs/refactor/editor/10-definable-keybindings.md -- editor keys' own
// parallel swapped-flag array, same purpose as the one above but for
// settings.editor_kbkeys[]/EditorGameKeys.
TbBool defined_editor_keys_that_have_been_swapped[EDITOR_GAME_KEYS_COUNT] = { false };
/******************************************************************************/

static void get_button_snapping_inputs(void)
{
    struct PlayerInfo* player = get_my_player();
    if (player->view_type == PVT_CreatureContrl)
        return;

    TbControllerButtons snapbtns = get_game_key_controller_buttons(Gkey_ButtonSnapRight)|get_game_key_controller_buttons(Gkey_ButtonSnapLeft)|get_game_key_controller_buttons(Gkey_ButtonSnapUp)|get_game_key_controller_buttons(Gkey_ButtonSnapDown);
    TbControllerButtons relevant_buttons = controller_button_state & snapbtns;
    if ((relevant_buttons == 0) || (relevant_buttons != controller_button_state)) {
        return;
    }

    double snap_x = get_game_key_axis_value(Gkey_ButtonSnapRight, false) - get_game_key_axis_value(Gkey_ButtonSnapLeft, false);
    double snap_y = get_game_key_axis_value(Gkey_ButtonSnapDown, false)  - get_game_key_axis_value(Gkey_ButtonSnapUp, false);
    
    snap_to_direction(GetMouseX(), GetMouseY(), snap_x, snap_y);

    controller_button_state = 0;
}

static double get_input_delta_time()
{
    static TbClockMSec delta_time_previous_msec = 0;

    TbClockMSec current_msec = LbTimerClock();
    if (delta_time_previous_msec == 0 || current_msec < delta_time_previous_msec) {
        delta_time_previous_msec = current_msec;
        return 0.0;
    }

    TbClockMSec elapsed_msec = current_msec - delta_time_previous_msec;
    delta_time_previous_msec = current_msec;
    double calculated_delta_time = ((double)elapsed_msec / 1000.0) * kfx_sim_state.turns_per_second;
    return min(calculated_delta_time, 1.0);
}

void poll_controller_mouse_clicks()
{
    static TbControllerButtons previous_controller_button_state;
    TbControllerButtons left_buttons = get_game_key_controller_buttons(Gkey_LeftClick);
    TbControllerButtons right_buttons = get_game_key_controller_buttons(Gkey_RightClick);
    
    struct TbPoint delta = {0, 0};
    
    if ((controller_button_state & left_buttons) != (previous_controller_button_state & left_buttons)) {
        if (controller_button_state & left_buttons) {
            mouseControl(MActn_LBUTTONDOWN, &delta);
        } else {
            mouseControl(MActn_LBUTTONUP, &delta);
        }
    }
    if ((controller_button_state & right_buttons) != (previous_controller_button_state & right_buttons)) {
        if (controller_button_state & right_buttons) {
            mouseControl(MActn_RBUTTONDOWN, &delta);
        } else {
            mouseControl(MActn_RBUTTONUP, &delta);
        }
    }
    previous_controller_button_state = controller_button_state;
}

#define SECONDS_TO_CROSS   20.0
static void poll_controller_mouse_movement(double nx, double ny)
{
    static double mouse_accum_x;
    static double mouse_accum_y;
    static double input_delta_time = 0.0;
    input_delta_time = get_input_delta_time();
    double mag = sqrt(nx * nx + ny * ny);

    if (mag <= 0.0)
        return;

    nx /= mag;
    ny /= mag;

    double norm_mag = min(mag, 1.0);
    double curved = norm_mag * norm_mag;
    double pixels_per_second = lbDisplay.GraphicsWindowWidth / SECONDS_TO_CROSS;
    double pixels_this_frame = pixels_per_second * input_delta_time;

    mouse_accum_x += nx * curved * pixels_this_frame;
    mouse_accum_y += ny * curved * pixels_this_frame;

    int64_t dx = (int64_t)mouse_accum_x;
    int64_t dy = (int64_t)mouse_accum_y;

    mouse_accum_x -= dx;
    mouse_accum_y -= dy;

    if (dx != 0 || dy != 0) {
        struct TbPoint mouseDelta = { dx, dy };
        mouseControl(MActn_MOUSEMOVE, &mouseDelta);
    }
}

void update_controller_inputs()
{
    if (!controller_connected())
    {
        return;
    }

    poll_controller_mouse_clicks();
    double mouse_x = get_game_key_axis_value(Gkey_MouseRight, false) - get_game_key_axis_value(Gkey_MouseLeft, false);
    double mouse_y = get_game_key_axis_value(Gkey_MouseDown, false) - get_game_key_axis_value(Gkey_MouseUp, false);
    poll_controller_mouse_movement(mouse_x, mouse_y);
    get_button_snapping_inputs();

    static TbBool last_pause_menu_state = false;
    TbBool pause_menu_pressed = is_game_key_pressed(Gkey_PauseMenu, false, true);

    if (pause_menu_pressed && !last_pause_menu_state) {
        lbKeyOn[KC_ESCAPE] = pause_menu_pressed;
    }
    last_pause_menu_state = pause_menu_pressed;
        
}

TbBool poll_inputs(void)
{
    TbBool user_not_quit = LbPollInputs();
    update_controller_inputs();

    return user_not_quit;
}

void clear_mouse_pressed_lrbutton(void)
{
  lbDisplay.LeftButton = 0;
  lbDisplay.RightButton = 0;
}

void update_left_button_released(void)
{
  left_button_released = 0;
  left_button_double_clicked = 0;
  if ( lbDisplay.LeftButton )
  {
    left_button_held = 1;
    left_button_held_x = GetMouseX();
    left_button_held_y = GetMouseY();
  }
  if (left_button_held)
  {
    if (!lbDisplay.MLeftButton)
    {
      left_button_released = 1;
      left_button_held = 0;
      left_button_released_x = GetMouseX();
      left_button_released_y = GetMouseY();
      if ( left_button_click_space_count < 5 )
      {
        left_button_double_clicked = 1;
        left_button_double_clicked_x = left_button_released_x;
        left_button_double_clicked_y = left_button_released_y;
      }
      left_button_click_space_count = 0;
    }
  } else
  {
    if (left_button_click_space_count < INT32_MAX)
      left_button_click_space_count++;
  }
}

void update_right_button_released(void)
{
  right_button_released = 0;
  right_button_double_clicked = 0;
  if (lbDisplay.RightButton)
  {
    right_button_held = 1;
    right_button_held_x = GetMouseX();
    right_button_held_y = GetMouseY();
  }
  if ( right_button_held )
  {
    if ( !lbDisplay.MRightButton )
    {
      right_button_released = 1;
      right_button_held = 0;
      right_button_released_x = GetMouseX();
      right_button_released_y = GetMouseY();
      if (right_button_click_space_count < 5)
      {
        right_button_double_clicked = 1;
        right_button_double_clicked_x = right_button_released_x;
        right_button_double_clicked_y = right_button_released_y;
      }
      right_button_click_space_count = 0;
    }
  } else
  {
    if (right_button_click_space_count < INT32_MAX)
      right_button_click_space_count++;
  }
}

void update_left_button_clicked(void)
{
  left_button_clicked = lbDisplay.LeftButton;
  left_button_clicked_x = lbDisplay.MouseX * (int64_t)pixel_size;
  left_button_clicked_y = lbDisplay.MouseY * (int64_t)pixel_size;
}

void update_right_button_clicked(void)
{
  right_button_clicked = lbDisplay.RightButton;
  right_button_clicked_x = lbDisplay.MouseX * (int64_t)pixel_size;
  right_button_clicked_y = lbDisplay.MouseY * (int64_t)pixel_size;
}

void update_wheel_scrolled(void)
{
    wheel_scrolled_up = (lbDisplayEx.WhellMoveUp > 0);
    wheel_scrolled_down = (lbDisplayEx.WhellMoveDown > 0);
}

/**
 * Translates mouse input from lbDisplay struct into simple variables.
 */
void update_mouse(void)
{
  update_left_button_released();
  update_right_button_released();
  update_left_button_clicked();
  update_right_button_clicked();
  update_wheel_scrolled();
  lbDisplay.LeftButton = 0;
  lbDisplay.RightButton = 0;
  lbDisplayEx.WhellMoveUp = 0;
  lbDisplayEx.WhellMoveDown = 0;
  // [mouse buttons as keybinds - quick fix]
  lbKeyOn[KC_MOUSE3] = lbDisplay.MiddleButton;
  lbKeyOn[KC_MOUSEWHEEL_UP] = wheel_scrolled_up;
  lbKeyOn[KC_MOUSEWHEEL_DOWN] = wheel_scrolled_down;
  lbInkey = lbDisplay.MiddleButton ? KC_MOUSE3 : wheel_scrolled_down ? KC_MOUSEWHEEL_DOWN : wheel_scrolled_up ? KC_MOUSEWHEEL_UP : lbInkey;

}

// docs/refactor/editor/10-definable-keybindings.md -- swap-on-conflict/
// modifier-group logic (below) is now parameterized on which binding table
// it operates over, rather than hardcoded to settings.kbkeys[]/
// GAME_KEYS_COUNT, so set_game_key() and set_editor_game_key() share one
// implementation instead of the editor keeping a simplified copy with no
// collision handling. `swapped` mirrors `keys` 1:1 (defined_keys_that_have_
// been_swapped/defined_editor_keys_that_have_been_swapped) -- the "recently
// swapped, highlight it" UI flag array. Shaped so a third table (e.g. a
// future Possession-mode key set) could reuse this by constructing one more
// struct KeyBindingTable, not by copy-pasting these functions again.
struct KeyBindingTable {
    struct GameKey *keys;
    TbBool *swapped;
    int64_t count;
};

// Resolved lazily (not a single static-initialized const) purely so the two
// getters below sit next to each other and read the same way -- there's no
// actual runtime cost or staleness risk, settings.kbkeys[]/editor_kbkeys[]
// don't move.
static const struct KeyBindingTable *get_game_key_table(void)
{
    static struct KeyBindingTable kt;
    kt.keys = settings.kbkeys;
    kt.swapped = defined_keys_that_have_been_swapped;
    kt.count = GAME_KEYS_COUNT;
    return &kt;
}

static const struct KeyBindingTable *get_editor_key_table(void)
{
    static struct KeyBindingTable kt;
    kt.keys = settings.editor_kbkeys;
    kt.swapped = defined_editor_keys_that_have_been_swapped;
    kt.count = EDITOR_GAME_KEYS_COUNT;
    return &kt;
}

static void swap_assigned_keys_in(const struct KeyBindingTable *kt, int64_t current_key_id, struct GameKey* current_kbk, int64_t new_key_id, unsigned char new_key, uint64_t new_mods)
{
    struct GameKey* kbk_swap = current_kbk;
    struct GameKey* new_kbk = &kt->keys[new_key_id];
    kbk_swap->code = new_kbk->code;
    kbk_swap->mods = new_kbk->mods;
    new_kbk->code = new_key;
    new_kbk->mods = new_mods;
    kt->swapped[current_key_id] = true;
    if (kt->swapped[new_key_id])
    {
        kt->swapped[new_key_id] = false;
    }
}

static void assign_key_in(const struct KeyBindingTable *kt, int64_t key_id, unsigned char key, uint64_t mods)
{
    struct GameKey* kbk = &kt->keys[key_id];
    kbk->code = key;
    kbk->mods = mods;
    if (kt->swapped[key_id])
    {
        kt->swapped[key_id] = false;
    }
}

int64_t mod_key_to_normal_key(uint64_t mods)
{
    int64_t ncode;
    if (mods & KMod_SHIFT)
    {
        ncode = KC_LSHIFT;
    }
    else if (mods & KMod_CONTROL)
    {
        ncode = KC_LCONTROL;
    }
    else if (mods & KMod_ALT)
    {
        ncode = KC_LALT;
    }
    else
    {
        ERRORLOG("Reached a place we should not be able to");
        ncode = KC_UNASSIGNED;
    }
    return ncode;
}

static void check_and_assign_mod_keys_group_in(const struct KeyBindingTable *kt, int64_t key_id, uint64_t mods, int64_t reference_key_ids[], int64_t reference_key_count)
{
    int64_t ncode = mod_key_to_normal_key(mods);
    // Do not allow the key if it is used as other mod key by any in reference_key_ids[]
    struct GameKey *kbk;
    for (int64_t i = 0; i < reference_key_count; i++)
    {
        kbk = &kt->keys[reference_key_ids[i]];
        if ((reference_key_ids[i] != key_id) && (kbk->code == ncode))
        {
            swap_assigned_keys_in(kt, reference_key_ids[i], kbk, key_id, ncode, 0);
            return;
        }
    }
    assign_key_in(kt, key_id, ncode, 0);
}

static void check_and_assign_mod_keys_in(const struct KeyBindingTable *kt, int64_t key_id, uint64_t mods, int64_t reference_key_id)
{
    // This only works for a pair of adjacent "linked" keys (i.e Speed/Rotate and Query/Possess)
    int64_t ncode = mod_key_to_normal_key(mods);
    // Do not allow the key if it is used as other mod key
    int64_t other_key_id = ((uint64_t)(key_id - reference_key_id) < 1) + reference_key_id;
    struct GameKey* kbk = &kt->keys[other_key_id];
    if (kbk->code != ncode)
    {
        assign_key_in(kt, key_id, ncode, 0);
    }
    else
    {
        swap_assigned_keys_in(kt, other_key_id, kbk, key_id, ncode, 0);
    }
}

static void check_and_assign_normal_keys_in(const struct KeyBindingTable *kt, int64_t key_id, unsigned char key, uint64_t mods, uint64_t set_mod)
{
    struct GameKey *kbk;
    for (int64_t i = 0; i < kt->count; i++)
    {
        kbk = &kt->keys[i];
        if ((i != key_id) && (kbk->code == key) && (kbk->mods == mods))
        {
            swap_assigned_keys_in(kt, i, kbk, key_id, key, (set_mod ? mods & (KMod_SHIFT|KMod_CONTROL|KMod_ALT) : 0));
            return;
        }
    }
    assign_key_in(kt, key_id, key, (set_mod ? mods & (KMod_SHIFT|KMod_CONTROL|KMod_ALT) : 0));
}

int64_t set_game_key(int64_t key_id, unsigned char key, uint64_t mods)
{
    if (!key_to_string[key])
    {
      return 0;
    }
    const struct KeyBindingTable *kt = get_game_key_table();

    struct GameKey *kbk = &kt->keys[key_id];
    if ((kbk->code == key && kbk->mods == mods)
        || (mods != KC_UNASSIGNED && kbk->code == mod_key_to_normal_key(mods)))
    {
        kbk->code = KC_UNASSIGNED;
        kbk->mods = KC_UNASSIGNED;
        kt->swapped[key_id] = false;
        return 1;
    }

    // One-Click Build & Sell Trap on Subtile - allow lone modifiers and normal keys
    if (key_id == Gkey_SellTrapOnSubtile || key_id == Gkey_SquareRoomSpace || key_id == Gkey_BestRoomSpace)
    {
        if ((mods & KMod_SHIFT) || (mods & KMod_CONTROL) || (mods & KMod_ALT))
        {
            int64_t reference_key_ids[3] = {Gkey_SellTrapOnSubtile, Gkey_SquareRoomSpace, Gkey_BestRoomSpace};
            check_and_assign_mod_keys_group_in(kt, key_id, mods, reference_key_ids, 3);
            return 1;
        }
        else
        {
            check_and_assign_normal_keys_in(kt, key_id, key, mods, 0);
            return 1;
        }
    }
    // Rotate & speed - allow lone modifiers and normal keys
    if (key_id == Gkey_RotateMod || key_id == Gkey_SpeedMod)
    {
        if ((mods & KMod_SHIFT) || (mods & KMod_CONTROL) || (mods & KMod_ALT))
        {
            check_and_assign_mod_keys_in(kt, key_id, mods, Gkey_RotateMod);
            return 1;
        }
        else
        {
            check_and_assign_normal_keys_in(kt, key_id, key, mods, 0);
            return 1;
        }
    }
    // Possess & query - allow lone modifiers and normal keys
    if (key_id == Gkey_CrtrContrlMod || key_id == Gkey_CrtrQueryMod)
    {
        if ((mods & KMod_SHIFT) || (mods & KMod_CONTROL) || (mods & KMod_ALT))
        {
            check_and_assign_mod_keys_in(kt, key_id, mods, Gkey_CrtrContrlMod);
            return 1;
        }
        else
        {
            check_and_assign_normal_keys_in(kt, key_id, key, mods, 0);
            return 1;
        }
    }
    // Single control keys - just ignore these keystrokes
    if ( key == KC_LSHIFT || key == KC_RSHIFT || key == KC_LCONTROL || key == KC_RCONTROL  || key == KC_LALT || key == KC_RALT )
    {
        return 0;
    }
    // The normal keys - allow a key alone, or with one modifier
    {
        if (((mods & KMod_SHIFT) && (mods & KMod_CONTROL))
         || ((mods & KMod_SHIFT) && (mods & KMod_ALT))
         || ((mods & KMod_CONTROL) && (mods & KMod_ALT)))
        {
            return 0;
        }
        check_and_assign_normal_keys_in(kt, key_id, key, mods, 1);
        return 1;
    }
}

// docs/refactor/editor/10-definable-keybindings.md -- editor keys' own
// counterpart to set_game_key() above, now sharing its swap-on-conflict
// logic via the same check_and_assign_normal_keys_in()/KeyBindingTable
// machinery instead of silently allowing collisions. No modifier-key-group
// handling (check_and_assign_mod_keys*_in(), the "allow binding to a bare
// Shift/Ctrl/Alt" flexibility Gkey_RotateMod/SpeedMod etc. get) -- none of
// the current editor keys need that, so it's not wired up here, though the
// generalized helpers would support it if a future editor key ever does.
static int64_t set_editor_game_key(int64_t key_id, unsigned char key, uint64_t mods)
{
    if (!key_to_string[key])
    {
        return 0;
    }
    const struct KeyBindingTable *kt = get_editor_key_table();

    struct GameKey *kbk = &kt->keys[key_id];
    if ((kbk->code == key && kbk->mods == mods)
        || (mods != KC_UNASSIGNED && kbk->code == mod_key_to_normal_key(mods)))
    {
        kbk->code = KC_UNASSIGNED;
        kbk->mods = KC_UNASSIGNED;
        kt->swapped[key_id] = false;
        return 1;
    }
    if (key == KC_LSHIFT || key == KC_RSHIFT || key == KC_LCONTROL || key == KC_RCONTROL || key == KC_LALT || key == KC_RALT)
    {
        return 0;
    }
    if (((mods & KMod_SHIFT) && (mods & KMod_CONTROL))
     || ((mods & KMod_SHIFT) && (mods & KMod_ALT))
     || ((mods & KMod_CONTROL) && (mods & KMod_ALT)))
    {
        return 0;
    }
    check_and_assign_normal_keys_in(kt, key_id, key, mods, 1);
    return 1;
}

void define_key_input(void)
{
  if (lbInkey == KC_ESCAPE)
  {
      lbKeyOn[KC_ESCAPE] = 0;
      lbInkey = KC_UNASSIGNED;
      defining_a_key = 0;
  } else
  if (right_button_clicked)
  {
      right_button_clicked = 0;
      defining_a_key = 0;
  } else
  if (lbInkey != KC_UNASSIGNED)
  {
      update_key_modifiers();
      int64_t assigned = defining_editor_key
          ? set_editor_game_key(defining_a_key_id, lbInkey, key_modifiers)
          : set_game_key(defining_a_key_id, lbInkey, key_modifiers);
      if (assigned)
        defining_a_key = 0;
      lbInkey = KC_UNASSIGNED;
  }
}

/**
 * Fills the array of keyboard key names.
 */
void init_key_to_strings(void)
{
    memset(key_to_string, 0, sizeof(key_to_string));
    for (struct KeyToStringInit* ktsi = &key_to_string_init[0]; ktsi->chr != 0; ktsi++)
    {
        int64_t k = ktsi->chr;
        key_to_string[k] = ktsi->str_idx;
    }
}

/**
 * Returns if the mouse is over "pannel map" - the circular minimap area on top left.
 * @param x Pannel map circle start X coordinate.
 * @param y Pannel map circle start Y coordinate.
 * @return
 */
TbBool mouse_is_over_panel_map(ScreenCoord x, ScreenCoord y)
{
    int64_t cmx = GetMouseX();
    int64_t cmy = GetMouseY();
    int64_t units_per_px = (16 * status_panel_width + 140 / 2) / 140;
    int64_t px = (cmx - (x + PANEL_MAP_RADIUS * units_per_px / 16));
    int64_t py = (cmy - (y + PANEL_MAP_RADIUS * units_per_px / 16));
    return (LbSqrL(px*px + py*py) < PANEL_MAP_RADIUS*units_per_px/16);
}

/**
 * Returns if the mouse is over the bottom part of the side menu, below the tab buttons and above the placefiller.
 * @return
 */
TbBool mouse_is_over_side_panel_bottom()
{
    if (!flag_is_set(kfx_sim_state.operation_flags, GOF_ShowGui))
        return false;
    struct GuiMenu* gmnu = get_active_menu(menu_id_to_number(GMnu_MAIN));
    // GUI_POSITION: the legacy menu itself is always created flush-left
    // (frontgui_ingame_panel.cpp's own read_menu_rect() mirrors it onto the
    // right edge purely for its own ImGui draw/hit-test, without touching
    // gmnu->pos_x) -- so mirror the same test here rather than the panel's
    // own (unmoved) pos_x.
    if (keeperfx_ui_config.hud_position == 2) // HudPos_Right
        return ((GetMouseX() > MyScreenWidth - status_panel_width) && (GetMouseY() > scale_ui_value(185)) && (GetMouseY() < gmnu->height));
    // Bottom (docs/refactor/ingame-gui/11-horizontal-layout.md) has no
    // vertical "side panel" at all -- this legacy flush-left rect isn't
    // drawn there, and falling through to the Left check below wrongly
    // flagged the empty space where it used to be (same bug class as
    // point_is_over_gui_menu(), gui_frontmenu.c).
    if (keeperfx_ui_config.hud_position == 3) // HudPos_Bottom
        return false;
    return ((GetMouseX() < status_panel_width) && (GetMouseY() > scale_ui_value(185)) && (GetMouseY() < gmnu->height));
}

TbBool add_input_text_to_message(char *message, int64_t max_message_length, struct TbSpriteSheet *font, int64_t max_width)
{
    LbTextSetFont(font);
    clear_key_pressed(lbInkey);

    if (pixel_size * LbTextStringWidth(message) >= max_width)
        return false;

    char text_input[64];
    int64_t text_len = LbGetTextInput(text_input, sizeof(text_input));
    if (text_len <= 0)
        return false;

    int64_t chpos = strlen(message);
    int64_t ti = 0;
    while (ti < text_len)
    {
        size_t seq_len = 0;
        uint64_t codepoint = read_utf_8_codepoint(&text_input[ti], &seq_len);
        // Accept any printable character; rendering falls back on unifont
        // for glyphs missing from the sprite fonts.
        TbBool acceptable = (codepoint >= 0x20 && codepoint != 0x7f);
        if (acceptable && (chpos + (int64_t)seq_len < max_message_length)) {
            memcpy(&message[chpos], &text_input[ti], seq_len);
            chpos += seq_len;
            message[chpos] = '\0';

            // Enforce max_width even when multiple characters arrive in one frame.
            if (pixel_size * LbTextStringWidth(message) >= max_width) {
                chpos -= seq_len;
                message[chpos] = '\0';
                break;
            }
        }
        ti += seq_len;
    }
    return true;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
