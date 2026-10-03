#ifndef FRONTGUI_INGAME_CELLS_H
#define FRONTGUI_INGAME_CELLS_H

#include <stdint.h>
// Shared vocabulary behind the in-game sidebar's tab-content grids/panels
// (docs/refactor/ingame-gui/10-maintainability-refactors.md §6/§7):
// the 140x400 virtual-grid mapping, the one interactive-cell primitive and
// the one bar primitive, and the scrolling 4-column icon-grid geometry.
// Callers: frontgui_ingame_grids.cpp, frontgui_ingame_creature.cpp,
// frontgui_ingame_tabcontent.cpp.

#ifdef __cplusplus

#include <functional>

struct ImDrawList;
struct ImVec2;

// The sidebar's scaled screen rect for the current frame -- set once by
// ingame_tabcontent_draw() before any grid_pt()/grid_sz()/grid_begin() call.
void fe_hud_set_panel_rect(double x, double y, double w, double h);
// Reads it back raw (no 140x400 division) -- for callers that lay out
// directly against the panel rect rather than through grid_pt()/grid_sz(),
// e.g. GUI_POSITION Bottom's own layouts
// (docs/refactor/ingame-gui/11-horizontal-layout.md), which aren't part of
// any 140x400 virtual space. Any argument may be null.
void fe_hud_get_panel_rect(double *x, double *y, double *w, double *h);

// A point / size in the 140x400 virtual panel grid -> screen coords/pixels.
ImVec2 grid_pt(double vx, double vy);
ImVec2 grid_sz(double vw, double vh);

// Local aliases into the shared HUD accent palette (relief::accents(),
// §4). COL_CELL_DIM is a surface tint (the "locked/unavailable" cell), not
// a state signal, so it stays a literal rather than an accents() field.
extern const uint64_t COL_CELL_DIM;
extern const uint64_t COL_BORDER;
extern const uint64_t COL_SEL;
extern const uint64_t COL_HOVER;
extern const uint64_t COL_TEXT;
extern const uint64_t COL_SUBTEXT;
extern const uint64_t COL_HAVE;

void blit_fit(ImDrawList *dl, int64_t spr, const ImVec2 &p0, const ImVec2 &sz, uint64_t tint);

// Same aspect-fit centring as blit_fit(), for a texture handle already in
// hand (a resolved icon-pack override, docs/refactor/ingame-gui/12-png-icon-overrides.md)
// rather than a legacy sprite index to resolve.
void blit_fit_tex(ImDrawList *dl, void *tex, int64_t w, int64_t h, const ImVec2 &p0, const ImVec2 &sz,
                  uint64_t tint);

// A single glyph filling ~70% of the cell height, centred. Uses the heavy
// UI font (a real vector face, crisp at any requested px).
void draw_big_glyph(ImDrawList *dl, const ImVec2 &p0, const ImVec2 &sz, const char *g, uint64_t col);


// §6 -- the shared interactive-cell skeleton behind build_icon/sell_icon/
// unknown_cell/cell_button/instance_cell/stat_cell:
//   SetCursorScreenPos + PushID + InvisibleButton + (opt tooltip) + PopID +
//   relief::well + content + (opt badges) + selected/hover ring.
// Content is either the declarative sprite/glyph/text fields below, or --
// when set -- the `content` callback, for a composite look that doesn't
// fit those (instance_cell's icon + cooldown bar). Returns 0/1/2 for
// none/left-click/right-click.
struct FeHudCellOpts {
    double rounding = 3.0;
    bool  rclick    = false;   // enable the right-click return value
    bool  swallow   = false;   // eat clicks; draw no hover/selected ring
    bool  selected  = false;

    int64_t sprite = 0;          // aspect-fit sprite, centred, inset 3px ...
    bool  dim    = false;      //   ... translucent when true (unaffordable/on cooldown)
    const char  *glyph     = nullptr;   // ... or a big vector glyph, centred ...
    uint64_t glyph_col = 0;         //   (0 -> COL_TEXT)
    const char  *text      = nullptr;   // ... or centred text; with `sprite` also
                                         //   set, a label beside a left-aligned square icon

    std::function<void(ImDrawList *, const ImVec2 &, const ImVec2 &)> content;   // overrides the above when set

    bool  have_dot = false;    // upper-left green dot ("already own one")
    int64_t   count    = -1;       // >=0 -> lower-right count badge (drawn when >0)
    const char *hotkey  = nullptr;   // upper-left hotkey number
    const char *tooltip = nullptr;   // word-wrapped, shown on hover
};
int64_t fe_hud_cell(const char *str_id, const ImVec2 &p0, const ImVec2 &sz, const FeHudCellOpts &o);

// §6 -- prog_bar/bar_row/vbar/info_band's capacity bar/instance_cell's
// cooldown bar: relief::well + inset fill + (opt) centred label.
// `fill_rounding` preserves a pre-existing inconsistency between call
// sites (some round the fill rect at 1.5px, some leave it square) rather
// than unifying it as a side effect of this refactor.
struct FeHudBarOpts {
    uint64_t fill;
    double rounding      = 2.0;    // well rounding
    double fill_rounding = 0.0;    // fill-rect rounding
    bool  vertical       = false;  // fill grows upward from the bottom, not rightward
    const char *label    = nullptr;
};
void fe_hud_bar(const ImVec2 &p0, const ImVec2 &p1, double frac, const FeHudBarOpts &o);

// Scrolling icon-grid geometry (scroll-independent cell size). `cols` is 4
// for the vertical layouts (Left/Right), 6 for Bottom
// (docs/refactor/ingame-gui/11-horizontal-layout.md) -- chosen by
// grid_begin() from the live GUI_POSITION setting, not a caller argument,
// so room_grid()/spell_grid()/trap_grid() etc. don't need to know or care.
// Plain floats, not ImVec2 -- this header must stay imgui.h-free (that
// stays scoped to frontgui_widgets.h and screen code, frontgui_widgets.h's
// own header comment).
struct GridGeom { double cell_w, cell_h, pitch_x, pitch_y; int64_t cols; };

// Opens the scrolling icon-grid child (a scrollbar appears once the item
// count overflows the visible region). Returns the geometry; pair with
// grid_end(g, slots_used).
GridGeom grid_begin(const char *id);
ImVec2 grid_cell_pos(const GridGeom &g, int64_t slot);
void grid_end(const GridGeom &g, int64_t slots_used);

#endif // __cplusplus
#endif // FRONTGUI_INGAME_CELLS_H
