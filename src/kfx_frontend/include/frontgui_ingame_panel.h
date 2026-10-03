#ifndef FRONTGUI_INGAME_PANEL_H
#define FRONTGUI_INGAME_PANEL_H

#include <stdint.h>
// Phase 4 (docs/refactor/ingame-gui/05-sidebar-frame-and-minimap.md): the
// always-on sidebar frame -- procedural background, gold counter, the 5
// tab headers, the zoom / map / autopilot buttons and the 13 event
// markers -- as ImGui. First pass keeps the current fixed-width column and
// leaves the minimap + tab content legacy-drawn (they show through a
// transparent hole; their geometry and this panel's both derive from
// status_panel_width / the GMnu_MAIN menu rect).

#ifdef __cplusplus
// C++ only: called from ingame_imgui_frame().
void ingame_panel_frame(void);

// Minimal layout only (docs/refactor/ingame-gui/13-minimal-layout.md §2.2):
// whether the pop-up panel should currently draw -- independent of
// menu_is_active(GMnu_*)'s own radio-group selection. Read by
// ingame_tabcontent_draw() (frontgui_ingame_tabcontent.cpp); meaningless
// (always false) for every other GUI_POSITION.
bool ingame_minimal_popup_is_open(void);
#endif

// GUI_POSITION (config_settingschema.c): the horizontal shift read_menu_rect()
// (frontgui_ingame_panel.cpp) applies to mirror the panel onto the right
// screen edge -- 0 when it's on the left (the legacy GMnu_MAIN menu's own
// position, which this never touches). Legacy screen-space math that keys
// off the panel's position but isn't reached by btn_rect()'s own offsetting
// (front_input.c's minimap click/drag hit-testing) adds this in directly.
// Valid only after the first ingame_panel_frame() call each session; 0
// before that, which is also correct (no panel drawn yet).
// The minimap's current absolute screen-pixel origin (top-left of its
// bounding square), valid after the first ingame_panel_frame() call each
// session -- 0,0 before that. Correct for every GUI_POSITION value,
// including Bottom (docs/refactor/ingame-gui/11-horizontal-layout.md),
// where the minimap isn't a simple offset from its old position. Replaces
// front_input.c's old `local_info.minimap_pos_x * mm_units_per_px / 16`
// (a formula that only ever assumed the legacy flush-left position).
#ifdef __cplusplus
extern "C" {
#endif
void ingame_panel_minimap_screen_pos(int64_t *x, int64_t *y);

// Minimal layout only (docs/refactor/ingame-gui/13-minimal-layout.md §2.2):
// the keyboard tab hotkeys (front_input.c: keys 1-5, Gkey_Next/PrevInstance)
// drive the pop-up the way the button cluster's clicks do. Call before the
// hotkey's own tab switch. With `toggle` set, pressing the key of the tab
// that is already selected and open closes the pop-up and returns non-zero:
// the caller then skips its tab switch. Otherwise the pop-up is opened and
// 0 is returned. A no-op returning 0 in every other GUI_POSITION.
int ingame_minimal_tab_hotkey(int64_t menu_id, int toggle);
#ifdef __cplusplus
}
#endif

#endif // FRONTGUI_INGAME_PANEL_H
