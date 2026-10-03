#ifndef FRONTGUI_INGAME_RELIEF_H
#define FRONTGUI_INGAME_RELIEF_H

#include <stdint.h>
// Procedural relief / emboss primitives for the ImGui in-game sidebar
// (docs/refactor/ingame-gui/09-relief-and-emboss-pass.md). ImDrawList only,
// no textures -- turns the flat HUD chrome into raised plateaus, recessed
// wells, bevelled bosses and engraved grooves.
//
// This is the VerticalRight layout's skin. The primitives are geometry-
// driven (take a rect / centre), so a sibling layout can reuse them, but a
// different composition module may call them differently or bring its own.
//
// C++ only (ImGui types). Callers: frontgui_ingame_panel.cpp,
// frontgui_ingame_tabcontent.cpp. Every bevel width is an absolute pixel
// count -- deliberately not resolution-scaled (09 §3).

#ifdef __cplusplus

struct ImDrawList;
struct ImVec2;

namespace relief {

// One shared tone ramp -- the single place to retune, or to later sample
// from front.pal instead of these placeholders (09 §5 "palette drift").
struct Tones {
    uint32_t base;         // flat mid surface
    uint32_t plateau_top;  // raised-plateau gradient, lit top
    uint32_t plateau_bot;  // raised-plateau gradient, bottom
    uint32_t well_top;     // recessed-well gradient, dark top
    uint32_t well_bot;     // recessed-well gradient, bottom
    uint32_t hi;           // bevel highlight (top/left of a raised edge)
    uint32_t lo;           // bevel shadow    (bottom/right of a raised edge)
    uint32_t crown_lit;    // boss crown when lit (selected / hover / on)
    uint32_t groove_dk;    // engraved groove, dark line
    uint32_t groove_lt;    // engraved groove, light line
    uint32_t mottle_lt;    // mottle fleck, light
    uint32_t mottle_dk;    // mottle fleck, dark
};
const Tones &tones();

// State-signal + bar-fill colours -- the accents layered on top of the
// relief material by the HUD cell / bar code (frontgui_ingame_tabcontent.cpp,
// frontgui_ingame_panel.cpp). One place to retune, alongside `Tones`.
// docs/refactor/ingame-gui/10-maintainability-refactors.md §4.
struct Accents {
    uint64_t text;      // primary label
    uint64_t subtext;   // secondary / dim label
    uint64_t border;    // resting cell outline
    uint64_t disabled;  // greyed content
    uint64_t sel;       // selected (gold rim)
    uint64_t hover;     // hovered (red rim)
    uint64_t have;      // "already own one" dot (green)
    uint64_t hotkey;    // ability hotkey number (yellow)
    uint64_t bar_good;  // capacity / xp / cooldown-ready fill
    uint64_t bar_warn;  // anger / mid health
    uint64_t bar_bad;   // low health / sell "$"
};
const Accents &accents();

// Linear blend of two packed colours (through float space).
uint64_t mix(uint64_t a, uint64_t b, double t);

// Tab fills: `active` matches the panel face (so the active tab blends into
// the content); inactive sits halfway between the face and the recess.
uint64_t tab_fill(bool active);

// Generalised two-tone bevel on a rect: `raised` => hi on top+left, lo on
// bottom+right; !raised => sunken (swapped). `width` px, clamped so it
// never exceeds half the smaller side. `omit_bottom` skips the bottom edge
// (an element that merges downward into whatever is below it -- e.g. the
// active tab into the content area).
void bevel(ImDrawList *dl, const ImVec2 &p_min, const ImVec2 &p_max,
           double width, bool raised, double rounding = 0.0, bool omit_bottom = false);

// The panel's raised stone face: vertical gradient (lit top) + one mottle
// pass, no bevel. For large surfaces that are framed by something else
// (the outer edge_frame, an adjacent well) rather than standing alone.
void face(ImDrawList *dl, const ImVec2 &p_min, const ImVec2 &p_max);

// Recessed well: inverted gradient (dark top) + 2px sunken bevel + a hard
// inner shadow line along the top lip.
void well(ImDrawList *dl, const ImVec2 &p_min, const ImVec2 &p_max, double rounding = 3.0);

// Circular recessed well floor (the minimap sits in one).
void well_circle(ImDrawList *dl, const ImVec2 &c, double radius);

// Recessed triangular pocket (the corner nav-button wells around the
// minimap): filled with the well tone + a sunken bevel on the 3 edges.
void well_tri(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b, const ImVec2 &c);

// Engraved horizontal groove between plate sections: dark line, light line
// one px below.
void groove_h(ImDrawList *dl, double x0, double x1, double y);

// Engraved groove along an arbitrary line: a dark line with a light line
// offset one px to its lower-right side (for the diagonal facets around
// the minimap / the chamfered panel corners).
void groove(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b);

// Raised bezel ring around a circular recess. `lit_dir` = screen-space
// angle (radians) the light comes from; default up-left.
void ring(ImDrawList *dl, const ImVec2 &c, double r_outer, double r_inner, double lit_dir = -2.356);

// The whole-panel raised outer rim (bevel only for now -- chamfered top
// corners are 09 §7's open question).
void edge_frame(ImDrawList *dl, const ImVec2 &p_min, const ImVec2 &p_max);


} // namespace relief

#endif // __cplusplus
#endif // FRONTGUI_INGAME_RELIEF_H
