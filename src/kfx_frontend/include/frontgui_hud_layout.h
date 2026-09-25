#ifndef FRONTGUI_HUD_LAYOUT_H
#define FRONTGUI_HUD_LAYOUT_H

#include <stdint.h>
// Phase 4 (docs/refactor/ingame-gui/05-sidebar-frame-and-minimap.md §0):
// the in-game HUD is composed of named regions, and a layout descriptor
// assigns each one a screen rect. Region code lays out *within* its rect --
// never against a 140-virtual-px constant or a hard screen edge -- so the
// current vertical-right sidebar and a future horizontal / minimal variant
// share one set of region code.
//
// This phase wires only HudLayout_VerticalRight. The other two exist in
// the enum so callers are written against the abstraction from the start.

#ifdef __cplusplus

struct HudRect { double x0, y0, x1, y1;
    double w() const { return x1 - x0; }
    double h() const { return y1 - y0; } };

enum HudPanelLayout {
    HudLayout_VerticalRight = 0, // default -- current KeeperFX / DK1
    HudLayout_HorizontalBottom,  // DK2-style bottom strip (not wired yet)
    HudLayout_Minimal,           // icons only, no tab content (not wired yet)
};

// HorizontalBottom only (docs/refactor/ingame-gui/11-horizontal-layout.md):
// region B's width isn't one constant -- Room/Spell/Trap want it capped
// (a wide screen otherwise spread a 6-column grid's cells apart with
// wasteful gaps, live-tested); the creature strip wants it as wide as
// possible (more creatures visible without scrolling, live-tested: "only
// being able to view 5 creatures isn't ideal"); the possession/query
// panel wants it *narrower* than the default (live-tested: "reduce the
// whitespace on the right"). The active tab decides which -- see
// frontgui_ingame_panel.cpp's determine_bottom_width_mode().
enum HudBottomWidthMode {
    HudBottomWidth_Normal = 0, // Room/Spell/Trap and anything not called out below
    HudBottomWidth_Wide,       // the creature strip
    HudBottomWidth_Narrow,     // possession / creature-query
};

// Minimal only (docs/refactor/ingame-gui/13-minimal-layout.md): which upper
// corner a cluster sits in. Used for both the minimap (+ gold + event
// markers) cluster and, independently, the button cluster + pop-up panel --
// build_minimal() takes one of these for each (MINIMAP_CORNER /
// PANEL_CORNER, config_keeperfx.h), so either may take either corner,
// including both sharing one.
enum HudMinimalCorner {
    HudMinimalCorner_UpperLeft = 0,  // default
    HudMinimalCorner_UpperRight,
};

enum HudRegion {
    HudRegion_Minimap = 0, // minimap texture + compass
    // Minimal (docs/refactor/ingame-gui/13-minimal-layout.md): repurposed as
    // the free-floating button cluster's anchor point (a nominal box, not a
    // channel-and-plinth strip -- the cluster is an auto-resize window, so
    // only its corner/pivot point is actually read).
    HudRegion_TabStrip,    // the 5 tab headers + zoom / map / autopilot buttons
    // Minimal: the pop-up panel's rect (sized off the vertical layout's own
    // panel width/height math, anchored off HudRegion_TabStrip's corner) --
    // unlike VerticalRight/HorizontalBottom this *is* read directly (the
    // popup isn't docked to anything else that already has a rect).
    HudRegion_TabContent,  // the active tab's body (Phase 5)
    HudRegion_Gold,        // the gold counter
    // Minimal: a vertical stack directly below the minimap, same edge
    // (live-tested decision -- doc 13 §1.3).
    HudRegion_Events,      // the event-notification markers
    // HorizontalBottom only (docs/refactor/ingame-gui/11-horizontal-layout.md):
    // the message queue has its own region there, since it sits in a fixed
    // place beside the grid rather than floating top-left like it does for
    // VerticalRight/Left/Right (frontgui_ingame_text.cpp).
    HudRegion_Messages,
    HudRegion_COUNT,
};

struct HudLayout {
    HudPanelLayout kind;
    HudRect region[HudRegion_COUNT];
    // How far the 3D viewport must inset, on this layout's axis:
    // a left-edge width for VerticalRight, a bottom-edge height for
    // HorizontalBottom. Consumed via render_overlay->get_status_panel_width().
    double viewport_inset;
};

// Recompute every region rect for `kind` at the given display size.
// `b_width_mode` only matters for HudLayout_HorizontalBottom (see
// HudBottomWidthMode above); `corner` (the minimap cluster) and
// `panel_corner` (the button cluster + pop-up) only matter for
// HudLayout_Minimal (see HudMinimalCorner above) -- pass
// HudBottomWidth_Normal / HudMinimalCorner_UpperLeft for every kind/param
// they don't apply to. Cheap -- call it on resize / layout switch /
// width-mode / corner change, not per frame.
void hud_layout_build(HudLayout *out, HudPanelLayout kind, HudBottomWidthMode b_width_mode,
                      HudMinimalCorner corner, HudMinimalCorner panel_corner,
                      double display_w, double display_h);

// The process-wide current layout, rebuilt by hud_layout_frame() when
// `kind`, `b_width_mode`, either corner, or the display size changes.
// hud_layout_frame() must run once per ImGui frame before any region is
// laid out.
const HudLayout &hud_layout_current(void);
void hud_layout_frame(HudPanelLayout kind, HudBottomWidthMode b_width_mode,
                      HudMinimalCorner corner, HudMinimalCorner panel_corner,
                      double display_w, double display_h);
inline const HudRect &hud_region_rect(HudRegion r) { return hud_layout_current().region[r]; }

#endif // __cplusplus

// C-callable: the current viewport inset (panel width for VerticalRight),
// for render_overlay->get_status_panel_width(). 0 before the first frame.
#ifdef __cplusplus
extern "C" {
#endif
int64_t hud_layout_viewport_inset(void);
#ifdef __cplusplus
}
#endif

#endif // FRONTGUI_HUD_LAYOUT_H
