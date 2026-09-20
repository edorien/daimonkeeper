/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_icon_grid.cpp
 *     The toolbox's icon palette.
 * @par Purpose:
 *     See editor_icon_grid.h.
 * @par Comment:
 *     Reuses fe_hud_cell() (frontgui_ingame_cells.cpp) for the tile chrome,
 *     hover/selected rings and click handling, and blit_fit()/
 *     blit_fit_tex() for the icon, so a tile here is the same object as a
 *     tile in the in-game sidebar. Layout is a plain flow: the child's
 *     content width fixes the column count, tiles are placed by index, and
 *     a heading ends the current row. The toolbox window is
 *     AlwaysAutoResize, so the child needs an explicit width (a 0/negative
 *     "fill" width would collapse it).
 */
#include "pre_inc.h"
#include "editor_icon_grid.h"
#include "frontgui_widgets.h"
#include "frontgui_style.h"
#include "frontgui_ingame_cells.h"
#include "frontgui_sprite_tex.h"
#include "frontgui_ingame_icon_overrides.h"
#include <imgui.h>
#include <cstring>
#include <string>
#include "post_inc.h"

/******************************************************************************/
namespace {

const float kGridWidth = 240.0f;
const float kGap = 4.0f;

// Test seam: the UI fonts (FeStylePushFont) load from game data, which a
// unit test doesn't have; with this set the heading uses ImGui's default font.
bool s_test_default_font = false;

struct GridState
{
    ImVec2 base = ImVec2(0, 0);
    float cell_w = 0, cell_h = 0;
    int cols = 1;
    int slot = 0;      // next tile index within the current row group
    float y_offset = 0; // height consumed by earlier row groups
    bool text_tiles = false;
} s;

float row_pitch() { return s.cell_h + kGap; }

// Finishes a partially filled row so the next thing starts on a fresh one.
void end_row_group()
{
    if (s.slot > 0)
    {
        s.y_offset += ((s.slot + s.cols - 1) / s.cols) * row_pitch();
        s.slot = 0;
    }
}

} // namespace

void editor_icon_grid_test_use_default_font(bool use_default)
{
    s_test_default_font = use_default;
}

const char *editor_icon_grid_pretty(const char *code_name)
{
    static char bufs[4][64];
    static int next = 0;
    char *b = bufs[next++ & 3];
    size_t i = 0;
    for (; code_name && code_name[i] != '\0' && i < 63; i++)
        b[i] = (code_name[i] == '_') ? ' ' : code_name[i];
    b[i] = '\0';
    return b;
}

void editor_icon_grid_begin(const char *id, float height, int cols)
{
    if (cols < 1)
        cols = 1;
    ImGui::BeginChild(id, ImVec2(kGridWidth, height), false, ImGuiWindowFlags_NoBackground);
    s.cols = cols;
    s.text_tiles = (cols <= 3);
    // Leave room for the scrollbar so the last column never sits under it.
    float usable = kGridWidth - ImGui::GetStyle().ScrollbarSize - 2.0f;
    s.cell_w = (usable - (cols - 1) * kGap) / (float)cols;
    s.cell_h = s.text_tiles ? 34.0f : s.cell_w;
    s.base = ImGui::GetCursorScreenPos();
    s.slot = 0;
    s.y_offset = 0.0f;
}

void editor_icon_grid_heading(const char *text)
{
    end_row_group();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (!s_test_default_font)
        FeStylePushFont(FeFont_Caption);
    dl->AddText(ImVec2(s.base.x + 2.0f, s.base.y + s.y_offset), IM_COL32(200, 190, 160, 255), text);
    s.y_offset += ImGui::GetTextLineHeight() + kGap;
    if (!s_test_default_font)
        FeStylePopFont();
}

bool editor_icon_grid_tile(const EditorIconTile &t)
{
    const int col = s.slot % s.cols;
    const int row = s.slot / s.cols;
    const ImVec2 p0(s.base.x + col * (s.cell_w + kGap), s.base.y + s.y_offset + row * row_pitch());
    const ImVec2 sz(s.cell_w, s.cell_h);
    s.slot++;

    FeHudCellOpts o;
    o.selected = t.selected;
    o.tooltip = t.tooltip;

    // Icon resolution order matches the in-game grids: PNG pack override,
    // then the legacy panel sprite, then a text tile.
    void *otex = nullptr;
    int ow = 0, oh = 0;
    bool odim = false;
    if (t.ov_kind != EIO_None && t.ov_category != nullptr && t.ov_code != nullptr && t.ov_code[0] != '\0')
    {
        otex = (t.ov_kind == EIO_Single)
            ? FeIconOverrideSingle(t.ov_category, t.ov_code, &ow, &oh)
            : FeIconOverrideActiveInactive(t.ov_category, t.ov_code, true, &ow, &oh, &odim);
    }
    if (otex != nullptr)
    {
        o.content = [otex, ow, oh](ImDrawList *dl, const ImVec2 &cp0, const ImVec2 &csz) {
            blit_fit_tex(dl, otex, ow, oh, ImVec2(cp0.x + 3.0f, cp0.y + 3.0f),
                ImVec2(csz.x - 6.0f, csz.y - 6.0f), IM_COL32_WHITE);
        };
    }
    else if (t.sprite > 0)
    {
        o.sprite = t.sprite;
    }
    else if (t.thumb != nullptr && t.thumb_w > 0 && t.thumb_h > 0)
    {
        // Picture only -- the name is the tooltip, like the in-game grids.
        void *tex = t.thumb;
        const int tw = t.thumb_w, th = t.thumb_h;
        o.content = [tex, tw, th](ImDrawList *dl, const ImVec2 &cp0, const ImVec2 &csz) {
            blit_fit_tex(dl, tex, tw, th, ImVec2(cp0.x + 3.0f, cp0.y + 3.0f),
                ImVec2(csz.x - 6.0f, csz.y - 6.0f), IM_COL32_WHITE);
        };
    }
    else
    {
        const std::string label = (t.label != nullptr) ? t.label : "";
        o.content = [label](ImDrawList *dl, const ImVec2 &cp0, const ImVec2 &csz) {
            // Wrapped, clipped to the tile; small so two short words fit.
            ImFont *font = ImGui::GetFont();
            const float fs = ImGui::GetFontSize() * 0.85f;
            dl->PushClipRect(ImVec2(cp0.x + 2.0f, cp0.y + 1.0f), ImVec2(cp0.x + csz.x - 2.0f, cp0.y + csz.y - 1.0f), true);
            const ImVec2 ts = font->CalcTextSizeA(fs, 1e9f, csz.x - 6.0f, label.c_str());
            dl->AddText(font, fs, ImVec2(cp0.x + 3.0f, cp0.y + (csz.y - ts.y) * 0.5f), IM_COL32(230, 220, 190, 255),
                label.c_str(), nullptr, csz.x - 6.0f);
            dl->PopClipRect();
        };
    }
    return fe_hud_cell(t.id, p0, sz, o) == 1;
}

void editor_icon_grid_end()
{
    end_row_group();
    ImGui::SetCursorScreenPos(s.base);
    ImGui::Dummy(ImVec2(1.0f, s.y_offset + 2.0f));
    ImGui::EndChild();
}
