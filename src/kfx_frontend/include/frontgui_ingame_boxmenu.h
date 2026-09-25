#ifndef FRONTGUI_INGAME_BOXMENU_H
#define FRONTGUI_INGAME_BOXMENU_H

// Phase 2 (docs/refactor/ingame-gui/03-debug-overlays-and-box-menus.md §2):
// the draggable cheat / service box menus (gui_boxmenu.c's GuiBox /
// GuiBoxOption system) drawn as ImGui windows unless the player has chosen
// the classic HUD (ingame_gui_use_classic_hud(), config_keeperfx.h). The
// boxes are still created / tracked by the legacy machinery
// (gui_create_box, kfx_frontend_local.gui_cheat_box_*, first_box/last_box) --
// this only swaps drawing + hit-testing.

#include "globals.h" // TbBool

#ifdef __cplusplus
extern "C" {
#endif

// Draws every open GuiBox as an ImGui window. Called from
// ingame_imgui_frame().
void ingame_boxmenu_frame(void);

// True when a cheat-box ImGui window currently has the mouse -- so
// gui_process_inputs() can swallow the click before the world sees it
// (the legacy path did the equivalent via its own hit-test).
TbBool ingame_boxmenu_consumes_mouse(void);

#ifdef __cplusplus
}
#endif

#endif // FRONTGUI_INGAME_BOXMENU_H
