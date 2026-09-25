#include <stdint.h>
#ifndef FRONTGUI_INGAME_TABCONTENT_H
#define FRONTGUI_INGAME_TABCONTENT_H

// Phase 5 (docs/refactor/ingame-gui/06-tab-content-panels.md): the five
// sidebar tab bodies -- room / power / manufacture build grids, the
// information tab, the creatures tab -- as ImGui, one GMnu_* at a time.
// The grids iterate config / dungeon state directly (no GuiButtonInit
// array mutation, no 2nd page). Drawn inside the sidebar frame's
// tab-content region (frontgui_ingame_panel.cpp).

#ifdef __cplusplus
// Draws the active tab's body into the *currently open* ImGui window
// (the sidebar frame's window -- frontgui_ingame_panel.cpp calls this
// between its tab strip and ImGui::End, so the grid shares one window
// with the frame and there is no z-order seam). (px,py,pw,ph) is the
// scaled GMnu_MAIN menu rect.
void ingame_tabcontent_draw(double px, double py, double pw, double ph);
#endif

#ifdef FUNCTESTING
#ifdef __cplusplus
extern "C" {
#endif
// Fires a migrated tab-panel action exactly the way its ImGui click
// handler does -- for the packet-parity ftest, since the ImGui
// InvisibleButtons cannot be driven headless. `arg` is a room / power /
// manufacture kind, or a player number for the ally toggle.
enum IngameTabTestAction {
    ITTA_RoomBuild = 0,   /* arg = RoomKind */
    ITTA_RoomSell,
    ITTA_SpellChoose,     /* arg = PowerKind */
    ITTA_TrapChoose,      /* arg = manufacture index */
    ITTA_TrapSell,
    ITTA_CreaturePickAny, /* arg = creature model */
    ITTA_QueryMode,
    ITTA_AllyToggle,      /* arg = PlayerNumber */
    ITTA_TendImprison,
    ITTA_TendFlee,
};
void ingame_tabcontent_test_fire(int64_t action, int64_t arg);
#ifdef __cplusplus
}
#endif
#endif // FUNCTESTING

#endif // FRONTGUI_INGAME_TABCONTENT_H
