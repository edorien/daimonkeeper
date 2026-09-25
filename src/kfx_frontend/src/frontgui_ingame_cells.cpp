#include "pre_inc.h"
#include "frontgui_ingame_cells.h"

#include "frontgui_widgets.h"          // imgui.h
#include "frontgui_style.h"            // FeStylePushFont/PopFont, FeFont_*
#include "frontgui_sprite_tex.h"       // FeGuiPanelTexture
#include "frontgui_ingame_relief.h"    // relief::well / accents()
#include "frontgui_ingame_layout.h"    // tcl:: -- named virtual-grid positions

#include "config_keeperfx.h"           // keeperfx_ui_config.hud_position -- GUI_POSITION

#include "post_inc.h"

#include <cfloat>
#include <cstdio>

const uint64_t COL_CELL_DIM = IM_COL32(30, 23, 14, 235);
const uint64_t COL_BORDER   = relief::accents().border;
const uint64_t COL_SEL      = relief::accents().sel;
const uint64_t COL_HOVER    = relief::accents().hover;
const uint64_t COL_TEXT     = relief::accents().text;
const uint64_t COL_SUBTEXT  = relief::accents().subtext;
const uint64_t COL_HAVE     = relief::accents().have;

namespace {

// The sidebar's scaled screen rect (== GMnu_MAIN's create_menu rect).
struct { double x, y, w, h; } s_panel = { 0, 0, 0, 0 };

} // namespace

void fe_hud_set_panel_rect(double x, double y, double w, double h)
{
    s_panel = { x, y, w, h };
}

void fe_hud_get_panel_rect(double *x, double *y, double *w, double *h)
{
    if (x != nullptr) *x = s_panel.x;
    if (y != nullptr) *y = s_panel.y;
    if (w != nullptr) *w = s_panel.w;
    if (h != nullptr) *h = s_panel.h;
}

ImVec2 grid_pt(double vx, double vy)
{
    return ImVec2(s_panel.x + vx * s_panel.w / 140.0,
                  s_panel.y + vy * s_panel.h / 400.0);
}
ImVec2 grid_sz(double vw, double vh)
{
    return ImVec2(vw * s_panel.w / 140.0, vh * s_panel.h / 400.0);
}

void blit_fit_tex(ImDrawList *dl, void *tex, int64_t w, int64_t h, const ImVec2 &p0, const ImVec2 &sz,
                  uint64_t tint)
{
    if (tex == nullptr || w <= 0 || h <= 0)
        return;
    double iw = sz.x, ih = sz.y;
    const double ar = (double)w / (double)h;
    if (iw / ih > ar) iw = ih * ar; else ih = iw / ar;
    const ImVec2 ip0(p0.x + (sz.x - iw) * 0.5, p0.y + (sz.y - ih) * 0.5);
    dl->AddImage((ImTextureID)(intptr_t)tex, ip0, ImVec2(ip0.x + iw, ip0.y + ih),
                 ImVec2(0, 0), ImVec2(1, 1), tint);
}

void blit_fit(ImDrawList *dl, int64_t spr, const ImVec2 &p0, const ImVec2 &sz, uint64_t tint)
{
    int64_t w = 0, h = 0;
    void *t = FeGuiPanelTexture(spr, &w, &h);
    blit_fit_tex(dl, t, w, h, p0, sz, tint);
}

void draw_big_glyph(ImDrawList *dl, const ImVec2 &p0, const ImVec2 &sz, const char *g, uint64_t col)
{
    FeStylePushFont(FeFont_Heading);
    ImFont *font = ImGui::GetFont();
    const double fs = sz.y * 0.70;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0, g);
    dl->AddText(font, fs, ImVec2(p0.x + (sz.x - ts.x) * 0.5, p0.y + (sz.y - ts.y) * 0.5), col, g);
    FeStylePopFont();
}

void wrapped_tooltip(const char *s)
{
    if (s == nullptr || s[0] == '\0')
        return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 16.0);
    ImGui::TextUnformatted(s);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

int64_t fe_hud_cell(const char *str_id, const ImVec2 &p0, const ImVec2 &sz, const FeHudCellOpts &o)
{
    ImGui::SetCursorScreenPos(p0);
    ImGui::PushID(str_id);
    const ImGuiButtonFlags flags = o.rclick
        ? (ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight)
        : ImGuiButtonFlags_MouseButtonLeft;
    const bool pressed = ImGui::InvisibleButton("b", sz, flags);
    const bool hovered = !o.swallow && ImGui::IsItemHovered();
    const bool rclick   = o.rclick && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (hovered && o.tooltip != nullptr && o.tooltip[0] != '\0')
        wrapped_tooltip(o.tooltip);
    ImGui::PopID();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
    relief::well(dl, p0, p1, o.rounding);

    if (o.content)
    {
        o.content(dl, p0, sz);
    }
    else if (o.sprite != 0 && o.text != nullptr && o.text[0] != '\0')
    {
        // icon + label beside it (stat_cell).
        blit_fit(dl, o.sprite, ImVec2(p0.x + 2.0, p0.y + 2.0), ImVec2(sz.y - 4.0, sz.y - 4.0), IM_COL32_WHITE);
        FeStylePushFont(FeFont_Body);
        dl->AddText(ImVec2(p0.x + sz.y + 3.0, p0.y + (sz.y - ImGui::GetFontSize()) * 0.5), COL_TEXT, o.text);
        FeStylePopFont();
    }
    else if (o.sprite != 0)
    {
        // centred aspect-fit icon (build_icon).
        blit_fit(dl, o.sprite, ImVec2(p0.x + 3.0, p0.y + 3.0), ImVec2(sz.x - 6.0, sz.y - 6.0),
                 o.dim ? IM_COL32(255, 255, 255, 110) : IM_COL32_WHITE);
    }
    else if (o.glyph != nullptr)
    {
        draw_big_glyph(dl, p0, sz, o.glyph, o.glyph_col != 0 ? o.glyph_col : COL_TEXT);
    }
    else if (o.text != nullptr && o.text[0] != '\0')
    {
        const ImVec2 ts = ImGui::CalcTextSize(o.text);
        dl->AddText(ImVec2(p0.x + (sz.x - ts.x) * 0.5, p0.y + (sz.y - ts.y) * 0.5), COL_TEXT, o.text);
    }

    if (o.have_dot)
        dl->AddCircleFilled(ImVec2(p0.x + 4.0, p0.y + 4.0), 2.5, COL_HAVE);
    if (o.count > 0)
    {
        char b[8]; std::snprintf(b, sizeof(b), "%" PRId64, (int64_t)(o.count));
        FeStylePushFont(FeFont_Body);
        const ImVec2 ts = ImGui::CalcTextSize(b);
        dl->AddText(ImVec2(p1.x - ts.x - 2.0, p1.y - ts.y - 1.0), COL_TEXT, b);
        FeStylePopFont();
    }
    if (o.hotkey != nullptr && o.hotkey[0] != '\0')
        dl->AddText(ImVec2(p0.x + 3.0, p0.y + 1.0), relief::accents().hotkey, o.hotkey);

    if (!o.swallow)
    {
        if (o.selected)   dl->AddRect(p0, p1, COL_SEL,   o.rounding, 0, 2.0);
        else if (hovered) dl->AddRect(p0, p1, COL_HOVER, o.rounding, 0, 2.0);
    }

    if (rclick)  return 2;
    if (pressed) return 1;
    return 0;
}

void fe_hud_bar(const ImVec2 &p0, const ImVec2 &p1, double frac, const FeHudBarOpts &o)
{
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    relief::well(dl, p0, p1, o.rounding);
    if (frac > 0.0)
    {
        if (o.vertical)
            dl->AddRectFilled(ImVec2(p0.x + 1.0, p1.y - (p1.y - p0.y - 2.0) * frac),
                              ImVec2(p1.x - 1.0, p1.y - 1.0), o.fill, o.fill_rounding);
        else
            dl->AddRectFilled(ImVec2(p0.x + 1.0, p0.y + 1.0),
                              ImVec2(p0.x + (p1.x - p0.x) * frac, p1.y - 1.0), o.fill, o.fill_rounding);
    }
    if (o.label != nullptr && o.label[0] != '\0')
    {
        FeStylePushFont(FeFont_Caption);
        const ImVec2 ts = ImGui::CalcTextSize(o.label);
        dl->AddText(ImVec2(p0.x + ((p1.x - p0.x) - ts.x) * 0.5, p0.y + ((p1.y - p0.y) - ts.y) * 0.5),
                    COL_TEXT, o.label);
        FeStylePopFont();
    }
}

namespace {

ImVec2 s_grid_base = { 0, 0 };

// True while GUI_POSITION is Bottom (config_settingschema.c) -- the only
// other value grid_begin()/grid_geom() branch on.
// docs/refactor/ingame-gui/11-horizontal-layout.md.
bool hud_bottom(void) { return keeperfx_ui_config.hud_position == 3; }

// Cell size + pitch: 4 columns against the vertical panel's own 140-wide
// virtual space (matching the legacy room menu metrics), or 6 columns
// against region B's actual width directly for Bottom -- that region isn't
// part of any 140x400 virtual space (§11), so this sizes off `s_panel.w`
// itself rather than through grid_sz(). 6 (not the original plan doc's 2)
// fills region B's actual width live-tested -- 2 left most of it empty.
GridGeom grid_geom(void)
{
    GridGeom g;
    if (hud_bottom())
    {
        // grid_begin()'s scrolling child reserves ImGui's own scrollbar
        // width out of s_panel.w -- not accounting for it here left the
        // last of the 6 columns cramped against (or under) the scrollbar
        // (live-tested). Assume one shows: most room/spell/trap/creature
        // lists overflow a single row at this cell size anyway.
        g.cols    = 6;
        g.pitch_x = (s_panel.w - ImGui::GetStyle().ScrollbarSize) / 6.0;
        g.cell_w  = g.pitch_x * 0.88;
        g.cell_h  = g.cell_w * (34.0 / 30.0);   // same cell aspect as the 4-col grid
        g.pitch_y = g.cell_h + 4.0;
    }
    else
    {
        g.cols = 4;
        const ImVec2 cell = grid_sz(30.0, 34.0);
        g.cell_w  = cell.x;
        g.cell_h  = cell.y;
        g.pitch_x = 32.0 * s_panel.w / 140.0;
        g.pitch_y = 38.0 * s_panel.h / 400.0;
    }
    return g;
}

} // namespace

// Opens the scrolling icon-grid child (a scrollbar appears once the item
// count overflows the visible region -- replaces the legacy 2nd page).
// Bottom uses the whole panel rect handed to fe_hud_set_panel_rect() (region
// B's TabContent slice, already excluding the tab-header row -- see
// ingame_tabcontent_draw()); the vertical layouts carve BODY_X0/GRID_Y0..Y1
// out of their own larger single-region rect instead.
GridGeom grid_begin(const char *id)
{
    ImVec2 r0, r1;
    if (hud_bottom())
    {
        r0 = ImVec2(s_panel.x, s_panel.y);
        r1 = ImVec2(s_panel.x + s_panel.w, s_panel.y + s_panel.h);
    }
    else
    {
        r0 = grid_pt(tcl::BODY_X0, tcl::GRID_Y0);
        r1 = grid_pt(136.0, tcl::GRID_Y1);
    }
    ImGui::SetCursorScreenPos(r0);
    ImGui::BeginChild(id, ImVec2(r1.x - r0.x, r1.y - r0.y), false, ImGuiWindowFlags_NoBackground);
    s_grid_base = ImGui::GetCursorScreenPos();
    s_grid_base.x += grid_sz(2.0, 0.0).x;   // small left inset off the scroll edge
    return grid_geom();
}
ImVec2 grid_cell_pos(const GridGeom &g, int64_t slot)
{
    return ImVec2(s_grid_base.x + (slot % g.cols) * g.pitch_x,
                  s_grid_base.y + (slot / g.cols) * g.pitch_y);
}
void grid_end(const GridGeom &g, int64_t slots_used)
{
    ImGui::Dummy(ImVec2(1.0, ((slots_used + g.cols - 1) / g.cols) * g.pitch_y + 4.0));
    ImGui::EndChild();
}
