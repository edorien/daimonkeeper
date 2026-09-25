/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_settings.c
 *     List of language-specific strings support.
 * @par Purpose:
 *     Support of configuration files for game strings.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     19 Nov 2011 - 01 Aug 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "config_settings.h"
#include "globals.h"
#include "bflib_basics.h"
#include "bflib_sound.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_keybrd.h"
#include "bflib_video.h"
#include "bflib_joyst.h"
#include "config_strings.h"
// GAMMA_LEVELS_COUNT (kfx_frontend's frontmenu_options.h) literal-duplicated
// -- only this file uses it, not worth pulling in frontmenu_options.h just
// for one constant.
#define GAMMA_LEVELS_COUNT 5
#include "config.h"
#include "value_util.h"
#include <ctype.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Moved from kfx_frontend's front_input.c (2026-08-29, docs/refactor/
// todo/check-layering-symbol-level-blind-spot.md) -- struct
// GamekeySettings/enum BindingMenuVisibility already lived in this
// file's own config_settings.h; the data itself belongs with them, and
// this file is the lowest-ranked real consumer.
const struct GamekeySettings game_key_settings[GAME_KEYS_COUNT] = {
    {"MoveUp",                GUIStr_CtrlUp,                  KC_W, KMod_NONE,               CBtn_LS_UP,               BMV_Visible,        NULL, },       // Gkey_MoveUp
    {"MoveDown",              GUIStr_CtrlDown,                KC_S, KMod_NONE,               CBtn_LS_DOWN,             BMV_Visible,        NULL, },       // Gkey_MoveDown
    {"MoveLeft",              GUIStr_CtrlLeft,                KC_A, KMod_NONE,               CBtn_LS_LEFT,             BMV_Visible,        NULL, },       // Gkey_MoveLeft
    {"MoveRight",             GUIStr_CtrlRight,               KC_D, KMod_NONE,               CBtn_LS_RIGHT,            BMV_Visible,        NULL, },       // Gkey_MoveRight
    {"RotateMod",             GUIStr_CtrlRotate,              KC_LCONTROL, KMod_NONE,        CBtn_B,                   BMV_Visible,        NULL, },       // Gkey_RotateMod
    {"SpeedMod",              GUIStr_CtrlSpeed,               KC_LSHIFT, KMod_NONE,          CBtn_A,                   BMV_Visible,        NULL, },       // Gkey_SpeedMod
    {"RotateCW",              GUIStr_CtrlRotateLeft,          KC_DELETE, KMod_NONE,          CBtn_A|CBtn_DPAD_LEFT,    BMV_Visible,        NULL, },       // Gkey_RotateCW
    {"RotateCCW",             GUIStr_CtrlRotateRight,         KC_PGDOWN, KMod_NONE,          CBtn_A|CBtn_DPAD_RIGHT,   BMV_Visible,        NULL, },       // Gkey_RotateCCW
    {"ZoomIn",                GUIStr_CtrlZoomIn,              KC_HOME, KMod_NONE,            CBtn_A|CBtn_DPAD_UP,      BMV_Visible,        NULL, },       // Gkey_ZoomIn
    {"ZoomOut",               GUIStr_CtrlZoomOut,             KC_END, KMod_NONE,             CBtn_A|CBtn_DPAD_DOWN,    BMV_Visible,        NULL, },       // Gkey_ZoomOut
    {"ZoomRoomTreasure",      CpgStr_RoomKind1+0,             KC_T, KMod_NONE,               CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomTreasure
    {"ZoomRoomLibrary",       CpgStr_RoomKind1+1,             KC_L, KMod_NONE,               CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomLibrary
    {"ZoomRoomLair",          CpgStr_RoomKind1+2,             KC_L, KMod_SHIFT,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomLair
    {"ZoomRoomPrison",        CpgStr_RoomKind1+3,             KC_P, KMod_SHIFT,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomPrison
    {"ZoomRoomTorture",       CpgStr_RoomKind1+4,             KC_T, KMod_ALT,                CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomTorture
    {"ZoomRoomTraining",      CpgStr_RoomKind1+5,             KC_T, KMod_SHIFT,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomTraining
    {"ZoomRoomHeart",         CpgStr_RoomKind1+6,             KC_H, KMod_NONE,               CBtn_Y,                   BMV_Visible,        NULL, },       // Gkey_ZoomRoomHeart
    {"ZoomRoomWorkshop",      CpgStr_RoomKind1+7,             KC_W, KMod_ALT,                CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomWorkshop
    {"ZoomRoomScavenger",     CpgStr_RoomKind1+8,             KC_S, KMod_ALT,                CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomScavenger
    {"ZoomRoomTemple",        CpgStr_RoomKind1+9,             KC_T, KMod_CONTROL,            CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomTemple
    {"ZoomRoomGraveyard",     CpgStr_RoomKind1+10,            KC_G, KMod_NONE,               CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomGraveyard
    {"ZoomRoomBarracks",      CpgStr_RoomKind1+11,            KC_B, KMod_NONE,               CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomBarracks
    {"ZoomRoomHatchery",      CpgStr_RoomKind1+12,            KC_H, KMod_SHIFT,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomHatchery
    {"ZoomRoomGuardPost",     CpgStr_RoomKind1+13,            KC_G, KMod_SHIFT,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomGuardPost
    {"ZoomRoomBridge",        CpgStr_RoomKind1+14,            KC_B, KMod_SHIFT,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomBridge
    {"ZoomRoomPortal",        CpgStr_RoomKind2,               KC_P, KMod_CONTROL,            CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomRoomPortal
    {"ZoomToFight",           GUIStr_StateFight,              KC_F, KMod_NONE,               CBtn_X,                   BMV_Visible,        NULL, },       // Gkey_ZoomToFight
    {"ZoomCrAnnoyed",         GUIStr_StateAnnoyed,            KC_A, KMod_ALT,                CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomCrAnnoyed
    {"CrtrContrlMod",         CpgStr_PowerKind1,              KC_LSHIFT, KMod_NONE,          CBtn_A,                   BMV_Visible,        NULL, },       // Gkey_CrtrContrlMod
    {"CrtrQueryMod",          GUIStr_Query,                   KC_Q, KMod_NONE,               CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_CrtrQueryMod
    {"DumpToOldPos",          GUIStr_UndoPickup,              KC_BACK, KMod_NONE,            CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_DumpToOldPos
    {"TogglePause",           GUIStr_Pause,                   KC_P, KMod_NONE,               CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_TogglePause
    {"SwitchToMap",           GUIStr_Map,                     KC_M, KMod_NONE,               CBtn_LEFTSTICK,           BMV_Visible,        NULL, },       // Gkey_SwitchToMap
    {"ToggleMessage",         GUIStr_ToggleMessage,           KC_E, KMod_NONE,               CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ToggleMessage
    {"SnapCamera",            GUIStr_SnapCamera,              KC_MOUSE3, KMod_NONE,          CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_SnapCamera
    {"BestRoomSpace",         GUIStr_BestRoomSpace,           KC_LSHIFT, KMod_NONE,          CBtn_A,                   BMV_Visible,        NULL, },       // Gkey_BestRoomSpace
    {"SquareRoomSpace",       GUIStr_SquareRoomSpace,         KC_LCONTROL, KMod_NONE,        CBtn_B,                   BMV_Visible,        NULL, },       // Gkey_SquareRoomSpace
    {"RoomSpaceIncSize",      GUIStr_RoomSpaceIncrease,       KC_MOUSEWHEEL_DOWN, KMod_NONE, CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_RoomSpaceIncSize
    {"RoomSpaceDecSize",      GUIStr_RoomSpaceDecrease,       KC_MOUSEWHEEL_UP, KMod_NONE,   CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_RoomSpaceDecSize
    {"SellTrapOnSubtile",     GUIStr_SellTrapOnSubtile,       KC_LALT, KMod_NONE,            CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_SellTrapOnSubtile
    {"TiltUp",                GUIStr_CtrlTiltUp,              KC_PGUP, KMod_SHIFT,           CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_TiltUp
    {"TiltDown",              GUIStr_CtrlTiltDown,            KC_PGDOWN, KMod_SHIFT,         CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_TiltDown
    {"TiltReset",             GUIStr_CtrlTiltReset,           KC_INSERT, KMod_SHIFT,         CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_TiltReset
    {"Ascend",                GUIStr_CtrlAscend,              KC_X, KMod_NONE,               CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_Ascend
    {"Descend",               GUIStr_CtrlDescend,             KC_Z, KMod_NONE,               CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_Descend
    {"ScreenRecord",          GUIStr_ScreenRecord,            KC_M, KMod_SHIFT,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ScreenRecord,
    {"ScreenShot",            GUIStr_ScreenShot,              KC_C, KMod_SHIFT,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ScreenShot,
    {"FrameSkipIncrease",     GUIStr_FrameSkipIncrease,       KC_ADD, KMod_CONTROL,          CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_FrameSkipIncrease,
    {"FrameSkipDecrease",     GUIStr_FrameSkipDecrease,       KC_SUBTRACT, KMod_CONTROL,     CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_FrameSkipDecrease,
    {"ZoomMinimapIn",         GUIStr_ZoomMinimapIn,           KC_ADD, KMod_NONE,             CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomMinimapIn,
    {"ZoomMinimapOut",        GUIStr_ZoomMinimapOut,          KC_SUBTRACT, KMod_NONE,        CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ZoomMinimapOut,
    {"ToggleGui",             GUIStr_ToggleGui,               KC_TAB, KMod_CONTROL,          CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ToggleGui,
    {"ToggleTooltips",        GUIStr_ToggleTooltips,          KC_F8, KMod_NONE,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ToggleTooltips,
    {"ExitGame",              GUIStr_ExitGame,                KC_X,   KMod_ALT,              CBtn_START|CBtn_BACK,     BMV_Visible,        NULL, },       // Gkey_ExitGame,
    {"DisablePacketMode",     GUIStr_DisablePacketMode,       KC_T,   KMod_ALT,              CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_DisablePacketMode,
    {"ToggleConsole",         GUIStr_ToggleConsole,           KC_GRAVE, KMod_NONE,           CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ToggleConsole,
    {"FinishLevel",           GUIStr_FinishLevel,             KC_SPACE, KMod_NONE,           CBtn_BACK,                BMV_Visible,        NULL, },       // Gkey_FinishLevel,
    {"ToggleHeroHealthFlower",GUIStr_ToggleHeroHealthFlowers, KC_F, KMod_ALT,                CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_ToggleHeroHealthFlowers,
    {"TeleportLastWorkroom",  GUIStr_TeleportLastWorkroom,    KC_SEMICOLON, KMod_NONE,       CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_TeleportLastWorkroom,
    {"TeleportCallToArms",    GUIStr_TeleportCallToArms,      KC_SLASH, KMod_NONE,           CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_TeleportCallToArms,
    {"TeleportDefault",       GUIStr_TeleportDefault,         KC_COMMA, KMod_NONE,           CBtn_NONE,                BMV_Visible,        NULL, },       // Gkey_TeleportDefault,
    {"CheatMenu1",            GUIStr_MnuUnused,               KC_NUMPADENTER, KMod_NONE,     CBtn_NONE,                BMV_Hidden,         NULL, },       // Gkey_CheatMenu1,
    {"CheatMenu2",            GUIStr_MnuUnused,               KC_F12, KMod_NONE,             CBtn_NONE,                BMV_Hidden,         NULL, },       // Gkey_CheatMenu2,
    {"LVShowAllEnsigns",      GUIStr_MnuUnused,               KC_F11, KMod_CONTROL,          CBtn_NONE,                BMV_Hidden,         NULL, },       // Gkey_LVShowAllEnsigns,
    {"LVNextLevel",           GUIStr_MnuUnused,               KC_F10, KMod_CONTROL,          CBtn_NONE,                BMV_Hidden,         NULL, },       // Gkey_LVNextLevel,
    {"LVPrevLevel",           GUIStr_MnuUnused,               KC_F9,  KMod_CONTROL,          CBtn_NONE,                BMV_Hidden,         NULL, },       // Gkey_LVPrevLevel,
    //TODO these are currently fixed, as controllers can't be reconfigured yet
    {"NextInstance",          GUIStr_NextInstance,            KC_UNASSIGNED, KMod_NONE,      CBtn_RIGHTSHOULDER,       BMV_ControllerOnly, NULL, },       // Gkey_NextInstance,
    {"PrevInstance",          GUIStr_PrevInstance,            KC_UNASSIGNED, KMod_NONE,      CBtn_LEFTSHOULDER,        BMV_ControllerOnly, NULL, },       // Gkey_PrevInstance,
    {"ButtonSnapLeft",        GUIStr_Keeper,                  KC_UNASSIGNED, KMod_NONE,      CBtn_DPAD_LEFT,           BMV_ControllerOnly, NULL, },       // Gkey_ButtonSnapLeft,
    {"ButtonSnapRight",       GUIStr_Keeper,                  KC_UNASSIGNED, KMod_NONE,      CBtn_DPAD_RIGHT,          BMV_ControllerOnly, NULL, },       // Gkey_ButtonSnapRight,
    {"ButtonSnapUp",          GUIStr_Keeper,                  KC_UNASSIGNED, KMod_NONE,      CBtn_DPAD_UP,             BMV_ControllerOnly, NULL, },       // Gkey_ButtonSnapUp,
    {"ButtonSnapDown",        GUIStr_Keeper,                  KC_UNASSIGNED, KMod_NONE,      CBtn_DPAD_DOWN,           BMV_ControllerOnly, NULL, },       // Gkey_ButtonSnapDown,
    {"PauseMenu",             GUIStr_Keeper,                  KC_UNASSIGNED, KMod_NONE,      CBtn_START,               BMV_ControllerOnly, NULL, },       // Gkey_PauseMenu,
    {"LeftClick",             GUIStr_Keeper,                  KC_UNASSIGNED, KMod_NONE,      CBtn_R2,                  BMV_ControllerOnly, NULL, },       // Gkey_LeftClick,
    {"RightClick",            GUIStr_Keeper,                  KC_UNASSIGNED, KMod_NONE,      CBtn_L2,                  BMV_ControllerOnly, NULL, },       // Gkey_RightClick,
    {"MouseUp",               GUIStr_CtrlUp,                  KC_UNASSIGNED, KMod_NONE,      CBtn_RS_UP,               BMV_ControllerOnly, NULL, },       // Gkey_MouseUp
    {"MouseDown",             GUIStr_CtrlDown,                KC_UNASSIGNED, KMod_NONE,      CBtn_RS_DOWN,             BMV_ControllerOnly, NULL, },       // Gkey_MouseDown
    {"MouseLeft",             GUIStr_CtrlLeft,                KC_UNASSIGNED, KMod_NONE,      CBtn_RS_LEFT,             BMV_ControllerOnly, NULL, },       // Gkey_MouseLeft
    {"MouseRight",            GUIStr_CtrlRight,               KC_UNASSIGNED, KMod_NONE,      CBtn_RS_RIGHT,            BMV_ControllerOnly, NULL, },       // Gkey_MouseRight
};

// docs/refactor/editor/10-definable-keybindings.md -- editor keybindings'
// own table (EditorGameKeys, globals.h), separate from game_key_settings[]
// above. KC_DELETE (the manual's own suggested eraser default) is already
// Gkey_RotateCW's default in game_key_settings[] -- found while first
// picking a default for this key, before it had its own separate table;
// KC_R ("Remove") is free. label_literal (not GUIStr_Empty via string_id)
// supplies the Define-Keys menu label -- see GamekeySettings's own comment
// for why.
const struct GamekeySettings editor_key_settings[EDITOR_GAME_KEYS_COUNT] = {
    {"EditorEraseTool",       GUIStr_Empty,                   KC_R, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Erase Tool", },  // Gkey_EditorEraseTool,
    // Camera/console keys, duplicated from game_key_settings[] rather than
    // shared with it (docs/refactor/editor/10-definable-keybindings.md) --
    // get_isometric_view_nonaction_inputs()/the console-toggle check
    // (front_input.c) read these instead of the Gkey_* originals while an
    // editor session is active. Defaults copied verbatim from the matching
    // game_key_settings[] row -- duplicating a physical default key across
    // two independent tables causes no collision (that's the whole point
    // of separate storage), and reusing the *same* GUIStr_* label as the
    // gameplay counterpart is correct here (unlike EditorEraseTool above,
    // "Move Up"/"Rotate CW"/etc. mean the exact same thing in both tables,
    // so there's a real localized string to reuse, not a label-less new
    // concept needing label_literal).
    {"EditorMoveUp",          GUIStr_CtrlUp,                  KC_W, KMod_NONE,               CBtn_LS_UP,               BMV_Visible,        NULL, },  // Gkey_EditorMoveUp
    {"EditorMoveDown",        GUIStr_CtrlDown,                KC_S, KMod_NONE,               CBtn_LS_DOWN,             BMV_Visible,        NULL, },  // Gkey_EditorMoveDown
    {"EditorMoveLeft",        GUIStr_CtrlLeft,                KC_A, KMod_NONE,               CBtn_LS_LEFT,             BMV_Visible,        NULL, },  // Gkey_EditorMoveLeft
    {"EditorMoveRight",       GUIStr_CtrlRight,               KC_D, KMod_NONE,               CBtn_LS_RIGHT,            BMV_Visible,        NULL, },  // Gkey_EditorMoveRight
    {"EditorRotateMod",       GUIStr_CtrlRotate,              KC_LCONTROL, KMod_NONE,        CBtn_B,                   BMV_Visible,        NULL, },  // Gkey_EditorRotateMod
    {"EditorSpeedMod",        GUIStr_CtrlSpeed,               KC_LSHIFT, KMod_NONE,          CBtn_A,                   BMV_Visible,        NULL, },  // Gkey_EditorSpeedMod
    {"EditorRotateCW",        GUIStr_CtrlRotateLeft,          KC_DELETE, KMod_NONE,          CBtn_A|CBtn_DPAD_LEFT,    BMV_Visible,        NULL, },  // Gkey_EditorRotateCW
    {"EditorRotateCCW",       GUIStr_CtrlRotateRight,         KC_PGDOWN, KMod_NONE,          CBtn_A|CBtn_DPAD_RIGHT,   BMV_Visible,        NULL, },  // Gkey_EditorRotateCCW
    {"EditorZoomIn",          GUIStr_CtrlZoomIn,              KC_HOME, KMod_NONE,            CBtn_A|CBtn_DPAD_UP,      BMV_Visible,        NULL, },  // Gkey_EditorZoomIn
    {"EditorZoomOut",         GUIStr_CtrlZoomOut,             KC_END, KMod_NONE,             CBtn_A|CBtn_DPAD_DOWN,    BMV_Visible,        NULL, },  // Gkey_EditorZoomOut
    {"EditorTiltUp",          GUIStr_CtrlTiltUp,              KC_PGUP, KMod_SHIFT,           CBtn_NONE,                BMV_Visible,        NULL, },  // Gkey_EditorTiltUp
    {"EditorTiltDown",        GUIStr_CtrlTiltDown,            KC_PGDOWN, KMod_SHIFT,         CBtn_NONE,                BMV_Visible,        NULL, },  // Gkey_EditorTiltDown
    {"EditorTiltReset",       GUIStr_CtrlTiltReset,           KC_INSERT, KMod_SHIFT,         CBtn_NONE,                BMV_Visible,        NULL, },  // Gkey_EditorTiltReset
    {"EditorToggleConsole",   GUIStr_ToggleConsole,           KC_GRAVE, KMod_NONE,           CBtn_NONE,                BMV_Visible,        NULL, },  // Gkey_EditorToggleConsole,
    // Tool-switch hotkeys (docs/refactor/editor/10-definable-keybindings.md)
    // -- editor-specific tool names, no existing GUIStr_*, so label_literal
    // like EditorEraseTool above rather than a gameplay-shared string.
    {"EditorTerrainTool",     GUIStr_Empty,                   KC_T, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Terrain Tool", },     // Gkey_EditorTerrainTool,
    {"EditorQueryTool",       GUIStr_Empty,                   KC_Q, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Query Tool", },       // Gkey_EditorQueryTool,
    {"EditorEyedropperTool",  GUIStr_Empty,                   KC_E, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Eyedropper Tool", },  // Gkey_EditorEyedropperTool,
    {"EditorFillTool",        GUIStr_Empty,                   KC_F, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Fill Tool", },        // Gkey_EditorFillTool,
    {"EditorStampTool",       GUIStr_Empty,                   KC_B, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Stamp Tool", },       // Gkey_EditorStampTool,
    {"EditorPointsTool",      GUIStr_Empty,                   KC_L, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Points Tool", },      // Gkey_EditorPointsTool,
    {"EditorCreatureTool",    GUIStr_Empty,                   KC_C, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Creature Tool", },    // Gkey_EditorCreatureTool,
    {"EditorThingTool",       GUIStr_Empty,                   KC_O, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Thing Tool", },       // Gkey_EditorThingTool,
    {"EditorReinforceTool",   GUIStr_Empty,                   KC_N, KMod_NONE,               CBtn_NONE,                BMV_Visible, "Map Editor: Switch to Reinforce Tool", },   // Gkey_EditorReinforceTool,
};

// Mirror engine_camera.h's camera zoom/tilt bounds, used here only as
// clamp/default values (not runtime state), so this file doesn't need
// engine_camera.h directly. See docs/refactor/stage-04-kfx-config.md
// issue B.
#define CAMERA_ZOOM_MAX 12000
#define CAMERA_ZOOM_MIN 520
#define FRONTVIEW_CAMERA_ZOOM_MAX 65536
#define FRONTVIEW_CAMERA_ZOOM_MIN 3000
#define CAMERA_TILT_DEFAULT -266
#define CAMERA_TILT_MIN -350
#define CAMERA_TILT_MAX -200

unsigned char i_can_see_levels[] = {30, 45, 60, 254,};
struct GameSettings settings;
static struct GameSettings settings_saved;
/******************************************************************************/

static const struct { unsigned char code; const char *name; } keycode_table[] = {
    { KC_UNASSIGNED,       "UNASSIGNED" },
    { KC_ESCAPE,           "ESCAPE" },
    { KC_1,                "1" },
    { KC_2,                "2" },
    { KC_3,                "3" },
    { KC_4,                "4" },
    { KC_5,                "5" },
    { KC_6,                "6" },
    { KC_7,                "7" },
    { KC_8,                "8" },
    { KC_9,                "9" },
    { KC_0,                "0" },
    { KC_MINUS,            "MINUS" },
    { KC_EQUALS,           "EQUALS" },
    { KC_BACK,             "BACK" },
    { KC_TAB,              "TAB" },
    { KC_Q,                "Q" },
    { KC_W,                "W" },
    { KC_E,                "E" },
    { KC_R,                "R" },
    { KC_T,                "T" },
    { KC_Y,                "Y" },
    { KC_U,                "U" },
    { KC_I,                "I" },
    { KC_O,                "O" },
    { KC_P,                "P" },
    { KC_LBRACKET,         "LBRACKET" },
    { KC_RBRACKET,         "RBRACKET" },
    { KC_RETURN,           "RETURN" },
    { KC_LCONTROL,         "LCONTROL" },
    { KC_A,                "A" },
    { KC_S,                "S" },
    { KC_D,                "D" },
    { KC_F,                "F" },
    { KC_G,                "G" },
    { KC_H,                "H" },
    { KC_J,                "J" },
    { KC_K,                "K" },
    { KC_L,                "L" },
    { KC_SEMICOLON,        "SEMICOLON" },
    { KC_APOSTROPHE,       "APOSTROPHE" },
    { KC_GRAVE,            "GRAVE" },
    { KC_LSHIFT,           "LSHIFT" },
    { KC_BACKSLASH,        "BACKSLASH" },
    { KC_Z,                "Z" },
    { KC_X,                "X" },
    { KC_C,                "C" },
    { KC_V,                "V" },
    { KC_B,                "B" },
    { KC_N,                "N" },
    { KC_M,                "M" },
    { KC_COMMA,            "COMMA" },
    { KC_PERIOD,           "PERIOD" },
    { KC_SLASH,            "SLASH" },
    { KC_RSHIFT,           "RSHIFT" },
    { KC_MULTIPLY,         "MULTIPLY" },
    { KC_LALT,             "LALT" },
    { KC_SPACE,            "SPACE" },
    { KC_CAPITAL,          "CAPITAL" },
    { KC_F1,               "F1" },
    { KC_F2,               "F2" },
    { KC_F3,               "F3" },
    { KC_F4,               "F4" },
    { KC_F5,               "F5" },
    { KC_F6,               "F6" },
    { KC_F7,               "F7" },
    { KC_F8,               "F8" },
    { KC_F9,               "F9" },
    { KC_F10,              "F10" },
    { KC_NUMLOCK,          "NUMLOCK" },
    { KC_SCROLL,           "SCROLL" },
    { KC_NUMPAD7,          "NUMPAD7" },
    { KC_NUMPAD8,          "NUMPAD8" },
    { KC_NUMPAD9,          "NUMPAD9" },
    { KC_SUBTRACT,         "SUBTRACT" },
    { KC_NUMPAD4,          "NUMPAD4" },
    { KC_NUMPAD5,          "NUMPAD5" },
    { KC_NUMPAD6,          "NUMPAD6" },
    { KC_ADD,              "ADD" },
    { KC_NUMPAD1,          "NUMPAD1" },
    { KC_NUMPAD2,          "NUMPAD2" },
    { KC_NUMPAD3,          "NUMPAD3" },
    { KC_NUMPAD0,          "NUMPAD0" },
    { KC_DECIMAL,          "DECIMAL" },
    { KC_F11,              "F11" },
    { KC_F12,              "F12" },
    { KC_NUMPADENTER,      "NUMPADENTER" },
    { KC_RCONTROL,         "RCONTROL" },
    { KC_DIVIDE,           "DIVIDE" },
    { KC_RALT,             "RALT" },
    { KC_HOME,             "HOME" },
    { KC_UP,               "UP" },
    { KC_PGUP,             "PGUP" },
    { KC_LEFT,             "LEFT" },
    { KC_RIGHT,            "RIGHT" },
    { KC_END,              "END" },
    { KC_DOWN,             "DOWN" },
    { KC_PGDOWN,           "PGDOWN" },
    { KC_INSERT,           "INSERT" },
    { KC_DELETE,           "DELETE" },
    { KC_MOUSE9,           "MOUSE9" },
    { KC_MOUSE8,           "MOUSE8" },
    { KC_MOUSE7,           "MOUSE7" },
    { KC_MOUSE6,           "MOUSE6" },
    { KC_MOUSE5,           "MOUSE5" },
    { KC_MOUSE4,           "MOUSE4" },
    { KC_MOUSE3,           "MOUSE3" },
    { KC_MOUSE2,           "MOUSE2" },
    { KC_MOUSE1,           "MOUSE1" },
    { KC_MOUSEWHEEL_DOWN,  "MOUSEWHEEL_DOWN" },
    { KC_MOUSEWHEEL_UP,    "MOUSEWHEEL_UP" },
};
#define KEYCODE_TABLE_SIZE ((int64_t)(sizeof(keycode_table)/sizeof(keycode_table[0])))

static const struct { TbControllerButtons button; const char *name; } controller_button_table[] = {
    { CBtn_A,              "A" },
    { CBtn_B,              "B" },
    { CBtn_X,              "X" },
    { CBtn_Y,              "Y" },
    { CBtn_BACK,           "BACK" },
    { CBtn_START,          "START" },
    { CBtn_LEFTSTICK,      "LEFTSTICK" },
    { CBtn_RIGHTSTICK,     "RIGHTSTICK" },
    { CBtn_LEFTSHOULDER,   "LEFTSHOULDER" },
    { CBtn_RIGHTSHOULDER,  "RIGHTSHOULDER" },
    { CBtn_DPAD_UP,        "DPAD_UP" },
    { CBtn_DPAD_DOWN,      "DPAD_DOWN" },
    { CBtn_DPAD_LEFT,      "DPAD_LEFT" },
    { CBtn_DPAD_RIGHT,     "DPAD_RIGHT" },
    { CBtn_MISC1,          "MISC1" },
    { CBtn_PADDLE1,        "PADDLE1" },
    { CBtn_PADDLE2,        "PADDLE2" },
    { CBtn_PADDLE3,        "PADDLE3" },
    { CBtn_PADDLE4,        "PADDLE4" },
    { CBtn_TOUCHPAD,       "TOUCHPAD" },
    { CBtn_L2,             "L2" },
    { CBtn_R2,             "R2" },
    { CBtn_LS_UP,          "LS_UP" },
    { CBtn_LS_DOWN,        "LS_DOWN" },
    { CBtn_LS_LEFT,        "LS_LEFT" },
    { CBtn_LS_RIGHT,       "LS_RIGHT" },
    { CBtn_RS_UP,          "RS_UP" },
    { CBtn_RS_DOWN,        "RS_DOWN" },
    { CBtn_RS_LEFT,        "RS_LEFT" },
    { CBtn_RS_RIGHT,       "RS_RIGHT" },
};
#define CONTROLLER_BUTTON_TABLE_SIZE ((int64_t)(sizeof(controller_button_table)/sizeof(controller_button_table[0])))

static const char *keycode_to_name(unsigned char code)
{
    for (int64_t i = 0; i < KEYCODE_TABLE_SIZE; i++)
        if (keycode_table[i].code == code)
            return keycode_table[i].name;
    return "UNASSIGNED";
}

static unsigned char name_to_keycode(const char *name)
{
    for (int64_t i = 0; i < KEYCODE_TABLE_SIZE; i++)
        if (strcmp(keycode_table[i].name, name) == 0)
            return keycode_table[i].code;
    return KC_UNASSIGNED;
}

static void kmod_to_name(unsigned char mods, char *buf, size_t buflen)
{
    int64_t len = 0;
    unsigned char flags = mods & (KMod_SHIFT | KMod_CONTROL | KMod_ALT);

    if (buflen == 0)
        return;

    if (flags == KMod_NONE)
    {
        snprintf(buf, buflen, "NONE");
        return;
    }

    buf[0] = '\0';
    if ((flags & KMod_SHIFT) != 0)
        len += snprintf(buf + len, buflen - len, "%sSHIFT", (len > 0) ? "|" : "");
    if ((flags & KMod_CONTROL) != 0)
        len += snprintf(buf + len, buflen - len, "%sCTRL", (len > 0) ? "|" : "");
    if ((flags & KMod_ALT) != 0)
        snprintf(buf + len, buflen - len, "%sALT", (len > 0) ? "|" : "");
}

static unsigned char name_to_kmod(const char *name)
{
    unsigned char mods = KMod_NONE;
    char token[16];
    int64_t token_len = 0;

    if (name == NULL)
        return mods;

    for (const char *p = name;; p++)
    {
        unsigned char c = (unsigned char)(*p);
        TbBool token_end = (c == '\0' || c == '|' || c == '+' || c == ',' || isspace(c));

        if (!token_end)
        {
            if (token_len < (int64_t)sizeof(token) - 1)
                token[token_len++] = (char)toupper(c);
            continue;
        }

        if (token_len > 0)
        {
            token[token_len] = '\0';
            if (strcmp(token, "SHIFT") == 0)
                mods |= KMod_SHIFT;
            else if (strcmp(token, "CTRL") == 0)
                mods |= KMod_CONTROL;
            else if (strcmp(token, "ALT") == 0)
                mods |= KMod_ALT;
            token_len = 0;
        }

        if (c == '\0')
            break;
    }

    return mods;
}

static void controller_buttons_to_name(TbControllerButtons buttons, char *buf, size_t buflen)
{
    size_t len = 0;

    if (buflen == 0)
        return;

    if (buttons == CBtn_NONE)
    {
        snprintf(buf, buflen, "NONE");
        return;
    }

    buf[0] = '\0';
    for (int64_t i = 0; i < CONTROLLER_BUTTON_TABLE_SIZE; i++)
    {
        int64_t written;

        if ((buttons & controller_button_table[i].button) == 0)
            continue;

        if (len >= buflen)
            break;

        written = snprintf(buf + len, buflen - len, "%s%s", (len > 0) ? "|" : "", controller_button_table[i].name);
        if (written < 0)
            break;
        if ((size_t)written >= buflen - len)
        {
            len = buflen;
            break;
        }
        len += (size_t)written;
    }

    if (len == 0)
        snprintf(buf, buflen, "NONE");
}

static TbControllerButtons name_to_controller_buttons(const char *name)
{
    TbControllerButtons buttons = CBtn_NONE;
    char token[32];
    int64_t token_len = 0;

    if (name == NULL)
        return buttons;

    for (const char *p = name;; p++)
    {
        unsigned char c = (unsigned char)(*p);
        TbBool token_end = (c == '\0' || c == '|' || c == '+' || c == ',' || isspace(c));

        if (!token_end)
        {
            if (token_len < (int64_t)sizeof(token) - 1)
                token[token_len++] = (char)toupper(c);
            continue;
        }

        if (token_len > 0)
        {
            token[token_len] = '\0';
            if (strcmp(token, "NONE") != 0)
            {
                for (int64_t i = 0; i < CONTROLLER_BUTTON_TABLE_SIZE; i++)
                {
                    if (strcmp(token, controller_button_table[i].name) == 0)
                    {
                        buttons |= controller_button_table[i].button;
                        break;
                    }
                }
            }
            token_len = 0;
        }

        if (c == '\0')
            break;
    }

    return buttons;
}

#ifdef __cplusplus
}
#endif
/******************************************************************************/
void setup_default_settings(void)
{
    settings.video_detail_level            = 0;
    settings.video_shadows                 = 4;
    settings.view_distance                 = 3;
    settings.video_rotate_mode             = 0;
    settings.video_textures                = 1;
    settings.video_cluedo_mode             = 0;
    settings.sound_volume                  = 127;
    settings.music_volume                  = 90;
    settings.roomflags_on                  = 1;
    settings.gamma_correction              = 0;
    settings.tooltips_on                   = true;
    settings.first_person_move_invert      = 0;
    settings.first_person_move_sensitivity = 6;
    settings.minimap_zoom                  = 256;
    settings.isometric_view_zoom_level     = 8192;
    settings.frontview_zoom_level          = FRONTVIEW_CAMERA_ZOOM_MAX;
    settings.mentor_volume                 = 127;
    settings.isometric_tilt                = CAMERA_TILT_DEFAULT;
    settings.highlight_mode                = false;

    for (int64_t i = 0; i < GAME_KEYS_COUNT; i++)
    {
        settings.kbkeys[i].code = game_key_settings[i].default_code;
        settings.kbkeys[i].mods = game_key_settings[i].default_mods;
        settings.kbkeys[i].controller_buttons = game_key_settings[i].default_controller_buttons;
    }
    // docs/refactor/editor/10-definable-keybindings.md -- editor keys' own
    // separate table.
    for (int64_t i = 0; i < EDITOR_GAME_KEYS_COUNT; i++)
    {
        settings.editor_kbkeys[i].code = editor_key_settings[i].default_code;
        settings.editor_kbkeys[i].mods = editor_key_settings[i].default_mods;
        settings.editor_kbkeys[i].controller_buttons = editor_key_settings[i].default_controller_buttons;
    }
}

TbBool load_settings(void)
{
    SYNCDBG(6,"Starting");
    setup_default_settings();

    char *fname = prepare_file_path(FGrp_Save, "settings.toml");
    VALUE root;
    if (!load_toml_file(fname, &root, CnfLd_IgnoreErrors))
    {
        save_settings();
        settings_saved = settings;
        return false;
    }

    VALUE *vsec;
    VALUE *val;

    /* [video] */
    vsec = value_dict_get(&root, "video");
    if (vsec)
    {
        val = value_dict_get(vsec, "detail_level");
        if (val && value_type(val) == VALUE_INT32) settings.video_detail_level = (unsigned char)value_int32(val);
        val = value_dict_get(vsec, "shadows");
        if (val && value_type(val) == VALUE_INT32) settings.video_shadows = (unsigned char)value_int32(val);
        val = value_dict_get(vsec, "view_distance");
        if (val && value_type(val) == VALUE_INT32) settings.view_distance = (unsigned char)value_int32(val);
        val = value_dict_get(vsec, "rotate_mode");
        if (val && value_type(val) == VALUE_INT32) settings.video_rotate_mode = (unsigned char)value_int32(val);
        val = value_dict_get(vsec, "textures");
        if (val && value_type(val) == VALUE_INT32) settings.video_textures = (unsigned char)value_int32(val);
        val = value_dict_get(vsec, "cluedo_mode");
        if (val && value_type(val) == VALUE_INT32) settings.video_cluedo_mode = (unsigned char)value_int32(val);
        val = value_dict_get(vsec, "gamma_correction");
        if (val && value_type(val) == VALUE_INT32) settings.gamma_correction = (int64_t)value_int32(val);
        val = value_dict_get(vsec, "roomflags_on");
        if (val && value_type(val) == VALUE_INT32) settings.roomflags_on = (unsigned char)value_int32(val);
    }

    /* [audio] */
    vsec = value_dict_get(&root, "audio");
    if (vsec)
    {
        val = value_dict_get(vsec, "sound_volume");
        if (val && value_type(val) == VALUE_INT32) settings.sound_volume = (unsigned char)value_int32(val);
        val = value_dict_get(vsec, "music_volume");
        if (val && value_type(val) == VALUE_INT32) settings.music_volume = (unsigned char)value_int32(val);
        val = value_dict_get(vsec, "mentor_volume");
        if (val && value_type(val) == VALUE_INT32) settings.mentor_volume = (int64_t)value_int32(val);
    }

    /* [display] */
    vsec = value_dict_get(&root, "display");
    if (vsec)
    {
        val = value_dict_get(vsec, "minimap_zoom");
        if (val && value_type(val) == VALUE_INT32) settings.minimap_zoom = (uint64_t)value_int32(val);
        val = value_dict_get(vsec, "isometric_view_zoom_level");
        if (val && value_type(val) == VALUE_INT32) settings.isometric_view_zoom_level = (uint64_t)value_int32(val);
        val = value_dict_get(vsec, "frontview_zoom_level");
        if (val && value_type(val) == VALUE_INT32) settings.frontview_zoom_level = (uint64_t)value_int32(val);
        val = value_dict_get(vsec, "isometric_tilt");
        if (val && value_type(val) == VALUE_INT32) settings.isometric_tilt = value_int32(val);
        val = value_dict_get(vsec, "tooltips_on");
        if (val) settings.tooltips_on = (TbBool)value_coerce_bool(val);
        val = value_dict_get(vsec, "highlight_mode");
        if (val) settings.highlight_mode = (TbBool)value_coerce_bool(val);
    }

    /* [gameplay] */
    vsec = value_dict_get(&root, "gameplay");
    if (vsec)
    {
        val = value_dict_get(vsec, "first_person_move_invert");
        if (val && value_type(val) == VALUE_INT32) settings.first_person_move_invert = (unsigned char)value_int32(val);
        val = value_dict_get(vsec, "first_person_move_sensitivity");
        if (val && value_type(val) == VALUE_INT32) settings.first_person_move_sensitivity = (unsigned char)value_int32(val);
    }

    /* [keys] */
    vsec = value_dict_get(&root, "keys");
    if (vsec)
    {
        for (int64_t i = 0; i < GAME_KEYS_COUNT; i++)
        {
            val = value_dict_get(vsec, game_key_settings[i].toml_name);
            if (val && value_type(val) == VALUE_DICT)
            {
                VALUE *vcode = value_dict_get(val, "code");
                VALUE *vmods = value_dict_get(val, "mods");
                VALUE *vcontroller_buttons = value_dict_get(val, "controller_buttons");
                if (vcode && value_type(vcode) == VALUE_STRING)
                    settings.kbkeys[i].code = name_to_keycode(value_string(vcode));
                if (vmods && value_type(vmods) == VALUE_STRING)
                    settings.kbkeys[i].mods = name_to_kmod(value_string(vmods));
                if (vcontroller_buttons)
                {
                    if (value_type(vcontroller_buttons) == VALUE_STRING)
                        settings.kbkeys[i].controller_buttons = name_to_controller_buttons(value_string(vcontroller_buttons));
                    else if (value_type(vcontroller_buttons) == VALUE_INT32)
                        settings.kbkeys[i].controller_buttons = (TbControllerButtons)(uint64_t)value_int32(vcontroller_buttons);
                }
            }
        }
    }

    /* [editor_keys] -- docs/refactor/editor/10-definable-keybindings.md,
       same shape as [keys] above but editor keybindings' own separate
       TOML section/array, not sharing game_key_settings[]'s namespace. */
    vsec = value_dict_get(&root, "editor_keys");
    if (vsec)
    {
        for (int64_t i = 0; i < EDITOR_GAME_KEYS_COUNT; i++)
        {
            val = value_dict_get(vsec, editor_key_settings[i].toml_name);
            if (val && value_type(val) == VALUE_DICT)
            {
                VALUE *vcode = value_dict_get(val, "code");
                VALUE *vmods = value_dict_get(val, "mods");
                VALUE *vcontroller_buttons = value_dict_get(val, "controller_buttons");
                if (vcode && value_type(vcode) == VALUE_STRING)
                    settings.editor_kbkeys[i].code = name_to_keycode(value_string(vcode));
                if (vmods && value_type(vmods) == VALUE_STRING)
                    settings.editor_kbkeys[i].mods = name_to_kmod(value_string(vmods));
                if (vcontroller_buttons)
                {
                    if (value_type(vcontroller_buttons) == VALUE_STRING)
                        settings.editor_kbkeys[i].controller_buttons = name_to_controller_buttons(value_string(vcontroller_buttons));
                    else if (value_type(vcontroller_buttons) == VALUE_INT32)
                        settings.editor_kbkeys[i].controller_buttons = (TbControllerButtons)(uint64_t)value_int32(vcontroller_buttons);
                }
            }
        }
    }

    value_fini(&root);

    // sanity checks
    settings.video_shadows = clamp(settings.video_shadows, 0, 3);
    settings.view_distance = clamp(settings.view_distance, 0, 3);
    settings.video_rotate_mode = clamp(settings.video_rotate_mode, 0, 2);
    settings.video_textures = clamp(settings.video_textures, 0, 1);
    settings.video_cluedo_mode = clamp(settings.video_cluedo_mode, 0, 1);
    settings.mentor_volume = clamp(settings.mentor_volume, 0, FULL_LOUDNESS);
    settings.gamma_correction = clamp(settings.gamma_correction, 0, GAMMA_LEVELS_COUNT);
    settings.first_person_move_sensitivity = clamp(settings.first_person_move_sensitivity, 0, 1000);
    settings.minimap_zoom = clamp(settings.minimap_zoom, 256, 2048);
    settings.isometric_view_zoom_level = clamp(settings.isometric_view_zoom_level, CAMERA_ZOOM_MIN, CAMERA_ZOOM_MAX);
    settings.frontview_zoom_level = clamp(settings.frontview_zoom_level, FRONTVIEW_CAMERA_ZOOM_MIN, FRONTVIEW_CAMERA_ZOOM_MAX);
    settings.isometric_tilt = clamp(settings.isometric_tilt, CAMERA_TILT_MIN, CAMERA_TILT_MAX);
    settings.highlight_mode = clamp(settings.highlight_mode, false, true);
    settings_saved = settings;
    bf_sound_set_volume_config(settings.sound_volume, settings.mentor_volume);
    return true;
}

int64_t save_settings(void)
{
    bf_sound_set_volume_config(settings.sound_volume, settings.mentor_volume);
    if (memcmp(&settings, &settings_saved, sizeof(settings)) == 0)
        return true;

    char *fname = prepare_file_path(FGrp_Save, "settings.toml");

    char *buf = (char *)malloc(16384);
    if (!buf) return false;
    int64_t len = 0;
    int64_t maxlen = 16384;
#define TOSAVE(...) len += snprintf(buf + len, maxlen - len, __VA_ARGS__)

    TOSAVE("# This file gets written by the game automatically.\n");
    TOSAVE("# Do not edit manually unless you know what you're doing.\n");
    TOSAVE("[video]\n");
    TOSAVE("detail_level = %" PRId64 "\n", (int64_t)settings.video_detail_level);
    TOSAVE("shadows = %" PRId64 "\n", (int64_t)settings.video_shadows);
    TOSAVE("view_distance = %" PRId64 "\n", (int64_t)settings.view_distance);
    TOSAVE("rotate_mode = %" PRId64 "\n", (int64_t)settings.video_rotate_mode);
    TOSAVE("textures = %" PRId64 "\n", (int64_t)settings.video_textures);
    TOSAVE("cluedo_mode = %" PRId64 "\n", (int64_t)settings.video_cluedo_mode);
    TOSAVE("gamma_correction = %" PRId64 "\n", (int64_t)settings.gamma_correction);
    TOSAVE("roomflags_on = %" PRId64 "\n", (int64_t)settings.roomflags_on);
    TOSAVE("\n[audio]\n");
    TOSAVE("sound_volume = %" PRId64 "\n", (int64_t)settings.sound_volume);
    TOSAVE("music_volume = %" PRId64 "\n", (int64_t)settings.music_volume);
    TOSAVE("mentor_volume = %" PRId64 "\n", (int64_t)(settings.mentor_volume));
    TOSAVE("\n[display]\n");
    TOSAVE("minimap_zoom = %" PRIu64 "\n", (uint64_t)(settings.minimap_zoom));
    TOSAVE("isometric_view_zoom_level = %" PRIu64 "\n", (uint64_t)(settings.isometric_view_zoom_level));
    TOSAVE("frontview_zoom_level = %" PRIu64 "\n", (uint64_t)(settings.frontview_zoom_level));
    TOSAVE("isometric_tilt = %" PRId64 "\n", (int64_t)(settings.isometric_tilt));
    TOSAVE("tooltips_on = %s\n", settings.tooltips_on ? "true" : "false");
    TOSAVE("highlight_mode = %s\n", settings.highlight_mode ? "true" : "false");
    TOSAVE("\n[gameplay]\n");
    TOSAVE("first_person_move_invert = %" PRId64 "\n", (int64_t)settings.first_person_move_invert);
    TOSAVE("first_person_move_sensitivity = %" PRId64 "\n", (int64_t)settings.first_person_move_sensitivity);
    TOSAVE("\n[keys]\n");
    for (int64_t i = 0; i < GAME_KEYS_COUNT; i++)
    {
        char mods_buf[32];
        char controller_buttons_buf[512];
        kmod_to_name(settings.kbkeys[i].mods, mods_buf, sizeof(mods_buf));
        controller_buttons_to_name(settings.kbkeys[i].controller_buttons, controller_buttons_buf, sizeof(controller_buttons_buf));
        TOSAVE("%s = { code = \"%s\", mods = \"%s\", controller_buttons = \"%s\" }\n",
            game_key_settings[i].toml_name,
            keycode_to_name(settings.kbkeys[i].code),
            mods_buf,
            controller_buttons_buf);
    }
    // docs/refactor/editor/10-definable-keybindings.md -- editor keys' own
    // section, same shape as [keys] above but not sharing its namespace.
    TOSAVE("\n[editor_keys]\n");
    for (int64_t i = 0; i < EDITOR_GAME_KEYS_COUNT; i++)
    {
        char mods_buf[32];
        char controller_buttons_buf[512];
        kmod_to_name(settings.editor_kbkeys[i].mods, mods_buf, sizeof(mods_buf));
        controller_buttons_to_name(settings.editor_kbkeys[i].controller_buttons, controller_buttons_buf, sizeof(controller_buttons_buf));
        TOSAVE("%s = { code = \"%s\", mods = \"%s\", controller_buttons = \"%s\" }\n",
            editor_key_settings[i].toml_name,
            keycode_to_name(settings.editor_kbkeys[i].code),
            mods_buf,
            controller_buttons_buf);
    }
#undef TOSAVE

    LbFileSaveAt(fname, buf, len);
    free(buf);
    settings_saved = settings;
    return true;
}

int64_t get_max_i_can_see_from_settings(void)
{
    return i_can_see_levels[settings.view_distance % 4];
}
/******************************************************************************/
