#ifndef FRONTGUI_INGAME_DEBUG_H
#define FRONTGUI_INGAME_DEBUG_H

// Phase 2 (docs/refactor/ingame-gui/03-debug-overlays-and-box-menus.md):
// the in-game debug / script-visible overlays as ImGui, replacing
// frame_compose.c's draw_debug_overlays() unless the player has
// chosen the classic HUD (ingame_gui_use_classic_hud(), config_keeperfx.h).
// Same per-overlay *_enabled() gates as the legacy path -- this only swaps
// the drawing, never the triggers.

#ifdef __cplusplus
extern "C" {
#endif

void ingame_debug_overlays_frame(void);

#ifdef __cplusplus
}
#endif

#endif // FRONTGUI_INGAME_DEBUG_H
