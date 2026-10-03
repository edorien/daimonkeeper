#ifndef FRONTGUI_INGAME_DEBUG_H
#define FRONTGUI_INGAME_DEBUG_H

// Phase 2 (docs/refactor/ingame-gui/03-debug-overlays-and-box-menus.md):
// the in-game debug / script-visible overlays as ImGui, replacing
// frame_compose.c's draw_debug_overlays() unless the player has
// chosen the classic HUD (ingame_gui_use_classic_hud(), config_keeperfx.h).
// Same per-overlay *_enabled() gates as the legacy path -- this only swaps
// the drawing, never the triggers.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ingame_debug_overlays_frame(void);

#ifdef __cplusplus
}

// One displayed script variable (DISPLAY_VARIABLE, DISPLAY_VARIABLE_WITH_LABEL and their Lua twins) in the
// top-right readout: its value against the target, and the icon a labelled one shows.
struct ScriptVariableRow
{
    char text[32];
    int64_t icon;      // panel sprite, or -1 for none
    bool on_tile;      // the variable kind's own icon: drawn over the blank message tile, at...
    double dx, dy;     // ...this offset from the tile's corner, in tile heights
};

// The displayed variables' rows, newest first, as upstream's draw_script_variable_list() lists them (none
// unless display_variable_enabled()); at most max.
int64_t script_variable_rows(struct ScriptVariableRow *rows, int64_t max);
#endif

#endif // FRONTGUI_INGAME_DEBUG_H
