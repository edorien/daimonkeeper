#include <stdint.h>
#include "pre_inc.h"
#include "frontgui_ingame_relief.h"
#include "fe_noise.h" // fe::fbm -- marble surface pass
#include <imgui.h>
#include <imgui_internal.h> // ImDrawListSharedData::TexUvWhitePixel -- per-vertex-colour annulus
#include <cmath>
#include "post_inc.h"

// docs/refactor/ingame-gui/09-relief-and-emboss-pass.md -- procedural
// relief for the ImGui sidebar. ImDrawList primitives only.

namespace relief {

namespace {

// Lerp two packed colours through linear float space.
ImU32 col_lerp(ImU32 a, ImU32 b, double t)
{
    const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        ca.x + (cb.x - ca.x) * t,
        ca.y + (cb.y - ca.y) * t,
        ca.z + (cb.z - ca.z) * t,
        ca.w + (cb.w - ca.w) * t));
}

// Vertical gradient fill. AddRectFilledMultiColor can't round, so a
// rounded rect gets a flat mid fill instead (still reads recessed/raised
// once the bevel is on top).
void fill_grad(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b, ImU32 top, ImU32 bot, double rounding)
{
    if (rounding <= 0.0)
        dl->AddRectFilledMultiColor(a, b, top, top, bot, bot);
    else
        dl->AddRectFilled(a, b, col_lerp(top, bot, 0.5), rounding);
}

// Bezel shade at one point on the ring: k = cos(angle - lit_dir), so +1 at
// the lit direction, -1 opposite. Smooth (no hard arc-to-base seam).
ImU32 ring_shade(uint64_t base, uint64_t hi, uint64_t lo, double k)
{
    if (k >= 0.0)
        return col_lerp(base, hi, std::pow(k, 0.85) * 0.92);
    return col_lerp(base, lo, std::pow(-k, 0.90) * 0.88);
}

} // namespace

const Tones &tones()
{
    // Ramp built around the panel base colour rgb(60,44,12) (sampled from
    // the reference art -- a warm brown, almost no blue).
    static const Tones T = {
        /* base        */ IM_COL32( 60,  44,  12, 236),
        /* plateau_top */ IM_COL32( 86,  64,  20, 242), // clearly lit -- a visible top-down gradient
        /* plateau_bot */ IM_COL32( 38,  28,   8, 242),
        /* well_top    */ IM_COL32( 20,  14,   4, 244), // a dark brown pocket -- not near-black
        /* well_bot    */ IM_COL32( 37,  27,   8, 242),
        /* hi          */ IM_COL32(204, 168, 104, 235), // bright bronze -- the raised-edge catch-light
        /* lo          */ IM_COL32(  0,   0,   0, 205),
        /* crown_lit   */ IM_COL32(176, 110,  34, 246),
        /* groove_dk   */ IM_COL32(  0,   0,   0, 225),
        /* groove_lt   */ IM_COL32(172, 136,  78, 150),
        /* mottle_lt   */ IM_COL32(184, 146,  86, 255),
        /* mottle_dk   */ IM_COL32(  6,   3,   0, 255),
    };
    return T;
}

const Accents &accents()
{
    static const Accents A = {
        /* text     */ IM_COL32(238, 226, 198, 255),
        /* subtext  */ IM_COL32(210, 190, 140, 255),
        /* border   */ IM_COL32( 92,  70,  42, 255),
        /* disabled */ IM_COL32(255, 255, 255, 110),
        /* sel      */ IM_COL32(255, 219, 102, 255),
        /* hover    */ IM_COL32(220,  62,  40, 255),
        /* have     */ IM_COL32(120, 200, 120, 255),
        /* hotkey   */ IM_COL32(255, 235, 120, 255),
        /* bar_good */ IM_COL32( 96, 194, 235, 255),
        /* bar_warn */ IM_COL32(210,  90,  60, 255),
        /* bar_bad  */ IM_COL32(220,  60,  50, 255),
    };
    return A;
}

uint64_t mix(uint64_t a, uint64_t b, double t)
{
    return col_lerp(a, b, t);
}

uint64_t tab_fill(bool active)
{
    const Tones &T = tones();
    const uint64_t face_mid = col_lerp(T.plateau_top, T.plateau_bot, 0.55);
    if (active)
        return face_mid;
    return col_lerp(face_mid, col_lerp(T.well_top, T.well_bot, 0.5), 0.5);
}

void bevel(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b, double width, bool raised, double rounding,
           bool omit_bottom)
{
    (void)rounding; // lines track the rect edges; corner rounding is cosmetic-minor here
    const Tones &T = tones();
    const ImU32 e_hi = raised ? T.hi : T.lo;
    const ImU32 e_lo = raised ? T.lo : T.hi;

    const double shorter = (b.x - a.x < b.y - a.y) ? (b.x - a.x) : (b.y - a.y);
    int64_t w = (int64_t)(width + 0.5);
    if (w < 1) w = 1;
    int64_t wmax = (int64_t)(shorter * 0.5);
    if (wmax < 1) wmax = 1;
    if (w > wmax) w = wmax;

    for (int64_t i = 0; i < w; i++)
    {
        // Fade outer->inner but keep the innermost line at ~0.45, not 0.
        const double f = (w > 1) ? 1.0 - 0.55 * (double)i / (double)(w - 1) : 1.0;
        const ImU32 h = ImGui::GetColorU32(e_hi, f);
        const ImU32 l = ImGui::GetColorU32(e_lo, f);
        const double x0 = a.x + (double)i + 0.5, y0 = a.y + (double)i + 0.5;
        const double x1 = b.x - (double)i - 0.5, y1 = b.y - (double)i - 0.5;
        dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y0), h); // top
        dl->AddLine(ImVec2(x0, y0), ImVec2(x0, y1), h); // left
        if (!omit_bottom)
            dl->AddLine(ImVec2(x0, y1), ImVec2(x1, y1), l); // bottom
        dl->AddLine(ImVec2(x1, y0), ImVec2(x1, y1), l); // right
    }
}

void face(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b)
{
    const Tones &T = tones();
    fill_grad(dl, a, b, T.plateau_top, T.plateau_bot, 0.0);
    mottle(dl, a, b);
}

void plateau(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b, double rounding)
{
    const Tones &T = tones();
    fill_grad(dl, a, b, T.plateau_top, T.plateau_bot, rounding);
    bevel(dl, a, b, 2.0, true, rounding);
}

void well(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b, double rounding)
{
    const Tones &T = tones();
    fill_grad(dl, a, b, T.well_top, T.well_bot, rounding);
    bevel(dl, a, b, 2.0, false, rounding);
    dl->AddLine(ImVec2(a.x + 1.5, a.y + 1.5), ImVec2(b.x - 1.5, a.y + 1.5),
                ImGui::GetColorU32(T.lo, 0.55));
}

void well_circle(ImDrawList *dl, const ImVec2 &c, double radius)
{
    if (radius < 1.0) return;
    dl->AddCircleFilled(c, radius, tones().well_bot, 72);
}

void well_tri(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b, const ImVec2 &c)
{
    const Tones &T = tones();
    dl->AddTriangleFilled(a, b, c, col_lerp(T.well_top, T.well_bot, 0.5));
    const double cx = (a.x + b.x + c.x) / 3.0;
    const double cy = (a.y + b.y + c.y) / 3.0;
    auto edge = [&](const ImVec2 &p, const ImVec2 &q) {
        const double mx = (p.x + q.x) * 0.5, my = (p.y + q.y) * 0.5;
        const bool up_left = ((mx - cx) + (my - cy)) < 0.0; // edge faces the light
        dl->AddLine(p, q, up_left ? ImGui::GetColorU32(T.lo, 0.85)
                                  : ImGui::GetColorU32(T.hi, 0.85), 1.5);
    };
    edge(a, b); edge(b, c); edge(c, a);
}

void boss(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b, double lit, double rounding, bool omit_bottom)
{
    const Tones &T = tones();
    if (!omit_bottom)
        dl->AddRectFilled(ImVec2(a.x + 1.5, a.y + 2.5), ImVec2(b.x + 1.5, b.y + 2.5),
                          IM_COL32(0, 0, 0, 70), rounding);
    const double k = (lit < 0.0) ? 0.0 : (lit > 1.0 ? 1.0 : lit);
    const ImU32 crown = (k > 0.0) ? col_lerp(T.plateau_top, T.crown_lit, k) : T.plateau_top;
    fill_grad(dl, a, b, crown, T.base, rounding);
    bevel(dl, a, b, 2.0, true, rounding, omit_bottom);
}

void groove_h(ImDrawList *dl, double x0, double x1, double y)
{
    const Tones &T = tones();
    dl->AddLine(ImVec2(x0, y + 0.5), ImVec2(x1, y + 0.5), T.groove_dk, 2.0);
    dl->AddLine(ImVec2(x0, y + 2.5), ImVec2(x1, y + 2.5), T.groove_lt, 1.0);
}

void groove(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b)
{
    const Tones &T = tones();
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0) return;
    const double nx = -dy / len, ny = dx / len; // one perpendicular
    // Put the light line on whichever side faces down-right.
    const double s = (nx + ny >= 0.0) ? 1.6 : -1.6;
    dl->AddLine(a, b, T.groove_dk, 2.0);
    dl->AddLine(ImVec2(a.x + nx * s, a.y + ny * s), ImVec2(b.x + nx * s, b.y + ny * s),
                T.groove_lt, 1.0);
}

void ring(ImDrawList *dl, const ImVec2 &c, double r_outer, double r_inner, double lit_dir)
{
    const Tones &T = tones();
    if (r_outer < r_inner + 2.0) r_outer = r_inner + 2.0;

    // Filled annulus with a smooth angular gradient -- brightest toward
    // lit_dir, darkest opposite -- via per-vertex colour. No hard seam
    // where a lit arc would butt into the base tone.
    const int64_t N = 72;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(N * 6, N * 4);
    for (int64_t i = 0; i < N; i++)
    {
        const double a0 = (double)i       * (6.2831853 / (double)N);
        const double a1 = (double)(i + 1) * (6.2831853 / (double)N);
        const ImU32 s0 = ring_shade(T.base, T.hi, T.lo, std::cos(a0 - lit_dir));
        const ImU32 s1 = ring_shade(T.base, T.hi, T.lo, std::cos(a1 - lit_dir));
        const double ca0 = std::cos(a0), sa0 = std::sin(a0);
        const double ca1 = std::cos(a1), sa1 = std::sin(a1);
        const uint64_t bi = dl->_VtxCurrentIdx;
        dl->PrimWriteIdx((ImDrawIdx)(bi + 0)); dl->PrimWriteIdx((ImDrawIdx)(bi + 1)); dl->PrimWriteIdx((ImDrawIdx)(bi + 2));
        dl->PrimWriteIdx((ImDrawIdx)(bi + 0)); dl->PrimWriteIdx((ImDrawIdx)(bi + 2)); dl->PrimWriteIdx((ImDrawIdx)(bi + 3));
        dl->PrimWriteVtx(ImVec2(c.x + ca0 * r_outer, c.y + sa0 * r_outer), uv, s0);
        dl->PrimWriteVtx(ImVec2(c.x + ca0 * r_inner, c.y + sa0 * r_inner), uv, s0);
        dl->PrimWriteVtx(ImVec2(c.x + ca1 * r_inner, c.y + sa1 * r_inner), uv, s1);
        dl->PrimWriteVtx(ImVec2(c.x + ca1 * r_outer, c.y + sa1 * r_outer), uv, s1);
    }
    // Soft lip lines -- gentle, not the earlier hard black rings.
    dl->AddCircle(c, r_inner, ImGui::GetColorU32(T.lo, 0.5), 96, 1.5);
    dl->AddCircle(c, r_outer, ImGui::GetColorU32(T.groove_dk, 0.6), 96, 1.0);
}

void edge_frame(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b)
{
    bevel(dl, a, b, 3.0, true, 0.0);
}

void mottle(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b)
{
    const Tones &T = tones();
    const double x0 = a.x + 2.0, y0 = a.y + 2.0;
    const double x1 = b.x - 2.0, y1 = b.y - 2.0;
    if (x1 <= x0 || y1 <= y0) return;

    // Procedural marble: veins where sin(freq*(p + amp*fbm(p))) peaks. A
    // domain-warped sine gives graduated wavy veins rather than random
    // flecks. Sampled in rect-relative coords -> deterministic, stable
    // between runs, just re-lays when the rect changes.
    const double cell = 6.0;
    const double ns   = 0.012;  // noise sampling frequency
    const double amp  = 44.0;   // domain warp strength
    const double freq = 0.030;  // vein frequency
    for (double y = y0; y < y1; y += cell)
        for (double x = x0; x < x1; x += cell)
        {
            const double px = x - a.x, py = y - a.y;
            const double warp = fe::fbm(px * ns, py * ns) - 0.4;
            const double m = std::sin(freq * (px + py * 0.4 + amp * warp)); // -1..1
            const double t = 0.5 + 0.5 * m;
            const double lightv = t * t * t;                     // veins
            const double darkv  = (1.0 - t) * (1.0 - t) * (1.0 - t);
            if (lightv < 0.09 && darkv < 0.09)
                continue;
            const ImU32 col = (lightv >= darkv)
                ? ImGui::GetColorU32(T.mottle_lt, lightv * 0.18)
                : ImGui::GetColorU32(T.mottle_dk, darkv  * 0.22);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell + 0.5, y + cell + 0.5), col);
        }
}

} // namespace relief
