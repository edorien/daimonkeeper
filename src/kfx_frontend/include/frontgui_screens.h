#ifndef FRONTGUI_SCREENS_H
#define FRONTGUI_SCREENS_H

// Phase C (docs/refactor/renderer/04-imgui-gui-foundation.md §7): the
// per-FrontendMenuState ImGui submission dispatch. frontend.cpp's draw and
// input dispatch both call frontend_imgui_screen_active() so the two agree
// on which system owns a given frame -- §8's "a given FrontendMenuState
// belongs entirely to one system" risk mitigation.

#include "bflib_basics.h" // TbBool

#ifdef __cplusplus
extern "C" {
#endif

// True when `state` (a FrontendMenuState) should be drawn/driven by ImGui
// this frame rather than the legacy sprite path -- "ImGui enabled AND this
// state has been migrated" (§3.5). Un-migrated states always return false,
// regardless of the -classicmenu/-noimgui toggle.
TbBool frontend_imgui_screen_active(int64_t state);

// Phase E: runs the master-detail screens' land preview panel input
// (pan/click/highlight) at the correct point in the frame -- call from
// frontend_input(), before frontscreen_end_input() -- rather than from
// the draw/present path. See the .cpp definition's comment for why the
// ordering matters. No-op for any state without a preview panel, or when
// ImGui isn't driving `state`.
void FrontendImGuiLandPreviewInput(int64_t state);

// The submit member of main.cpp's RendererImGuiCallbacks (via app_imgui_frame,
// renderer/RendererManager.h): dispatches to the active migrated screen's
// submission, plus the Phase B style-sheet debug overlay (independent of
// migration state -- see frontgui_stylesheet_test.h).
void FrontendImGuiFrame(void);

// docs/refactor/ingame-gui/02-pause-menu-and-options.md: submit the shared
// settings window in its in-game form. Called from frontgui_ingame.cpp's
// pause-menu launcher when "Options" is expanded.
void frontgui_options_frame_ingame(void);

#ifdef __cplusplus
}
#endif

#endif // FRONTGUI_SCREENS_H
