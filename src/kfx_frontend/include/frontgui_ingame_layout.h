#ifndef FRONTGUI_INGAME_LAYOUT_H
#define FRONTGUI_INGAME_LAYOUT_H

// Named positions for the sidebar tab-content bodies, in the shared
// 140x400 virtual grid that grid_pt()/grid_sz() (frontgui_ingame_tabcontent.cpp)
// map to screen pixels. Replaces the inline magic y-coords that made every
// "nudge this row" change a scattered hand-edit + collision hunt.
// docs/refactor/ingame-gui/10-maintainability-refactors.md §5.
//
// This is the *within-region* layout. frontgui_hud_layout.h stays the
// *between-region* layout for the horizontal / minimal HUD variants
// (05-sidebar-frame-and-minimap.md §0); the two compose.
//
// C++ only.

#ifdef __cplusplus

namespace tcl {

// The whole tab-content region (below the tab strip, above the panel foot).
constexpr double BODY_Y0 = 190.0;
constexpr double BODY_Y1 = 398.0;
constexpr double BODY_X0 = 4.0;
constexpr double BODY_X1 = 136.0;

// Room / spell / trap panels: the info strip, then the scrolling icon grid.
constexpr double INFO_Y0 = 196.0;
constexpr double INFO_Y1 = 240.0;
constexpr double GRID_Y0 = 242.0;
constexpr double GRID_Y1 = 396.0;

// Creature-query / possession panel.
namespace q {
    // Header: portrait + the two vertical anger/xp bars
    // (10:60:10:10:5:10:5 horizontal split -- see 09 retest 5).
    constexpr double HEADER_Y0 = 190.0;
    constexpr double HEADER_Y1 = 243.0;
    constexpr double BARS_Y0   = 192.0;
    constexpr double BARS_Y1   = 241.0;
    constexpr double LEVEL_Y   = 193.0;  // level number, over the xp bar top

    // Health bar (name centred, no numeric value).
    constexpr double HEALTH_Y0 = 248.0;
    constexpr double HEALTH_Y1 = 266.0;

    // ABILITIES / STATS toggle strip.
    constexpr double DETAIL_TABS_Y = 270.0;
    constexpr double DETAIL_TABS_H = 16.0;

    // ABILITIES: the instance grid.
    constexpr double ABIL_ORG_Y  = 291.0;
    constexpr double ABIL_CELL_W = 43.0;
    constexpr double ABIL_CELL_H = 37.0;
    constexpr double ABIL_PITCH  = 40.0;

    // STATS: the scrolling 2-per-row list.
    constexpr double STATS_Y0 = 289.0;
    constexpr double STATS_Y1 = 396.0;
}

// GMnu_SPELL_LOST (top-down lost-keeper state).
namespace lost {
    constexpr double TEXT_Y   = 202.0;
    constexpr double ICON_Y0  = 250.0;
    constexpr double ICON_X0  = 50.0;
    constexpr double ICON_W   = 40.0;
    constexpr double ICON_H   = 44.0;
}

} // namespace tcl

#endif // __cplusplus
#endif // FRONTGUI_INGAME_LAYOUT_H
