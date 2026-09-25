#include "pre_inc.h"
#include "frontgui_ingame_panel.h"

#include "frontgui_widgets.h"
#include "frontgui_style.h"
#include "frontgui_sprite_tex.h"      // FeGuiPanelTexture
#include "config_spritecolors.h"     // get_player_colored_icon_idx (event marker room/creature icons)
#include "frontgui_ingame_tabcontent.h" // ingame_tabcontent_draw
#include "frontgui_ingame_relief.h"    // relief::plateau / well / ring / ...
#include "frontgui_offscreen.h"       // FeOffscreenTarget -- minimap raster capture
#include "frontgui_hud_layout.h"      // HudLayout_HorizontalBottom, hud_layout_frame/_current (GUI_POSITION Bottom)
#include "renderer/RendererManager.h" // dynamic textures
#include "frontgui_ingame_icon_overrides.h" // FeIconOverrideTexture -- docs/refactor/ingame-gui/12-png-icon-overrides.md

#include "globals.h"
#include "bflib_guibtns.h"            // struct GuiButton, LbBtnF_*
#include "sprites.h"                  // GPS_rpanel_*
#include "frontend.h"                 // active_buttons, BID_*, get_active_menu
#include "gui_frontmenu.h"            // menu_id_to_number, get_gui_button
#include "gui_frontbtns.h"            // get_gui_button, gui_set_menu_mode
#include "frontmenu_ingame_tabs.h"    // gui_zoom_in/out, gui_go_to_map, gui_turn_on_autopilot
#include "frontmenu_ingame_map.h"     // MapDiagonalLength, panel_map_draw_slabs / _overlay_things
#include "frontmenu_ingame_evnt.h"    // gui_open_event, gui_kill_event, get_my_event_button_index
#include "player_data.h"              // get_my_player, my_player_number, minimap_*
#include "dungeon_data.h"             // get_players_dungeon, total_money_owned
#include "map_events.h"               // kfx_sim_state.event[], event_button_info, EvF_BtnFalling
#include "kfx_sim_state.h"
#include "kfx_config_state.h"         // kfx_config_state.gui_blink_rate
#include "config_keeperfx.h"          // keeperfx_ui_config.hud_position
#include "kfx_frontend_state.h"       // flash_button_index (tab spangle)
#include "bflib_video.h"              // scale_value_for_resolution_with_upp, TbPixel
#include "bflib_math.h"               // LbSinL / LbCosL / LbFPMath_TrigmBits
#include "engine_render.h"            // interpolate_synced
#include "custom_sprites.h"           // is_custom_icon, get_custom_icon_frame_count
#include "config_strings.h"           // GUIStr_MapN/S/E/W
#include "local_camera.h"             // get_local_camera

#include "post_inc.h"

#include <algorithm> // std::min -- draw_event_markers_horizontal
#include <cfloat>  // FLT_MAX
#include <cmath>   // std::sin
#include <cstdio>
#include <cstring> // std::memset
#include <vector>
#include <imgui.h>

namespace {

// ---- deferred click dispatch -------------------------------------------
// The GMnu_MAIN handlers (gui_set_menu_mode, gui_open_event, ...) do
// turn_on/off_menu -- never from inside an ImGui window. Capture the small
// values they read off their GuiButton and replay through a stack button
// at the top of the next frame.
struct PendingBtn {
    void (*fn)(struct GuiButton *) = nullptr;
    int64_t btype_value = 0;
    int64_t content_lval = 0;
};
PendingBtn s_pending;

void request_btn(void (*fn)(struct GuiButton *), int64_t btype_value, int64_t content_lval)
{
    s_pending.fn = fn;
    s_pending.btype_value = btype_value;
    s_pending.content_lval = content_lval;
}

void apply_pending(void)
{
    if (s_pending.fn == nullptr)
        return;
    struct GuiButton gb;
    std::memset(&gb, 0, sizeof(gb));
    gb.btype_value = s_pending.btype_value;
    gb.content.lval = s_pending.content_lval;
    void (*fn)(struct GuiButton *) = s_pending.fn;
    s_pending.fn = nullptr;
    fn(&gb);
}

// ---- helpers ----------------------------------------------------------

// GUI_POSITION (config_settingschema.c): the legacy GMnu_MAIN menu is
// always created flush-left (create_menu()'s one-time compute_menu_position_x,
// never touched). read_menu_rect() below re-decides the live side every
// frame and writes the horizontal shift needed to mirror the panel onto
// the right edge here; btn_rect() applies it to every legacy button rect
// it hands back, so tab-strip hit-testing tracks the redraw exactly.
// (0 for HudPos_Left -- no-op.)
double s_panel_offset_x = 0.0;
bool s_panel_on_right = false;

const struct GuiButton *find_btn(int64_t id)
{
    const struct GuiButton *b = get_gui_button(id);
    if (b == nullptr || (b->flags & LbBtnF_Active) == 0)
        return nullptr;
    return b;
}

// Screen rect of a legacy GMnu_MAIN button (create_menu already scaled its
// scr_pos_*, shifted here by s_panel_offset_x). We only draw + hit-test in
// ImGui; the legacy sprite draw and input sweep are skipped for GMnu_MAIN
// (menu_is_migrated).
bool btn_rect(int64_t id, ImVec2 *p0, ImVec2 *p1, bool *enabled)
{
    const struct GuiButton *b = find_btn(id);
    if (b == nullptr)
        return false;
    *p0 = ImVec2((double)b->scr_pos_x + s_panel_offset_x, (double)b->scr_pos_y);
    *p1 = ImVec2((double)(b->scr_pos_x + b->width) + s_panel_offset_x, (double)(b->scr_pos_y + b->height));
    if (enabled != nullptr)
        *enabled = (b->flags & LbBtnF_Enabled) != 0;
    return true;
}

// A nav control (M / + / - / cpu) seated in a recessed triangular pocket
// at a corner of the minimap frame -- the right-angle vertex at
// (cx, cy), the legs running `dx`/`dy` (each +/-1) toward the panel
// interior, the hypotenuse parallel to the minimap's octagon facet. The
// caption sits by the vertex; a marble gap is left between the pocket and
// the panel edge by the caller. `sel` keeps it lit (autopilot toggle).
// Returns 1 on left-click.
int64_t corner_nav(const char *id, const char *label, double cx, double cy, double dx, double dy,
               double leg, bool enabled, bool sel)
{
    if (leg < 22.0) leg = 22.0;
    const ImVec2 v0(cx, cy);
    const ImVec2 v1(cx, cy + dy * leg);
    const ImVec2 v2(cx + dx * leg, cy);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    relief::well_tri(dl, v0, v1, v2);
    if (sel)
        dl->AddTriangle(v0, v1, v2, IM_COL32(210, 160, 70, 190), 1.5);

    FeStylePushFont(FeFont_Subheading);
    const ImVec2 ls = ImGui::CalcTextSize(label);
    const double bw = ls.x + 8.0, bh = ls.y + 6.0;
    const double tx = (dx > 0.0) ? cx + 2.0 : cx - 2.0 - bw;
    const double ty = (dy > 0.0) ? cy + 2.0 : cy - 2.0 - bh;

    ImGui::SetCursorScreenPos(ImVec2(tx, ty));
    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton("b", ImVec2(bw, bh), ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    const ImU32 col = !enabled ? IM_COL32(180, 170, 150, 90)
                    : hovered  ? ImGui::GetColorU32(ImVec4(0.86, 0.24, 0.16, 1.0))
                    : sel      ? ImGui::GetColorU32(ImVec4(1.0, 0.86, 0.45, 1.0))
                               : ImGui::GetColorU32(ImVec4(0.95, 0.88, 0.7, 1.0));
    dl->AddText(ImVec2(tx + 4.0, ty + 3.0), col, label);
    FeStylePopFont();

    return (enabled && pressed) ? 1 : 0;
}

// An icon button occupying an explicit screen rect (a legacy button's
// slot). Panel-sprite icon centred in the rect; blood-red hover ring;
// One sidebar tab -- a rounded key on the shared recessed channel. The
// active tab takes the panel-face colour and drops downward into the
// content; inactive tabs sit halfway between face and recess, flat, with
// no side bevel (so no double lines between neighbours). Icon is a
// query-panel sprite (optionally keeper-coloured) or a vector glyph.
// Returns 1 on left-click.
int64_t tab_button(const char *id, int64_t spr, bool colorize, const char *glyph, ImU32 glyph_col,
               const ImVec2 &r0, const ImVec2 &r1, bool selected)
{
    const ImVec2 p0(r0.x + 1.5, r0.y + 1.0); // inset -> a gap between neighbours
    const ImVec2 p1(r1.x - 1.5, r1.y);
    const ImVec2 sz(p1.x - p0.x, p1.y - p0.y);

    ImGui::SetCursorScreenPos(p0);
    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton("b", sz, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const double rnd = 15.0; // upper corners only
    if (selected)
    {
        const ImVec2 e1(p1.x, p1.y + 8.0); // drop through the channel into the content
        dl->AddRectFilled(p0, e1, relief::tab_fill(true), rnd, ImDrawFlags_RoundCornersTop);
        relief::bevel(dl, p0, e1, 2.0, true, rnd, /*omit_bottom*/ true);
    }
    else
    {
        const ImU32 fill = hovered
            ? relief::mix(relief::tab_fill(false), relief::tab_fill(true), 0.5)
            : relief::tab_fill(false);
        dl->AddRectFilled(p0, p1, fill, rnd, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(p0.x + 3.0, p0.y + 1.0), ImVec2(p1.x - 3.0, p0.y + 1.0),
                    ImGui::GetColorU32(relief::tones().hi, hovered ? 0.35 : 0.18));
    }

    const ImVec2 ic(p0.x + sz.x * 0.5, p0.y + sz.y * 0.5);
    if (glyph != nullptr)
    {
        FeStylePushFont(FeFont_Heading);
        ImFont *f = ImGui::GetFont();
        const double fs = sz.y * 0.60;
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0, glyph);
        dl->AddText(f, fs, ImVec2(ic.x - ts.x * 0.5, ic.y - ts.y * 0.5), glyph_col, glyph);
        FeStylePopFont();
    }
    else
    {
        const int64_t s = colorize ? get_player_colored_icon_idx(spr, my_player_number) : spr;
        int64_t sw = 0, sh = 0;
        void *tex = FeGuiPanelTexture(s, &sw, &sh);
        if (tex != nullptr && sw > 0 && sh > 0)
        {
            double iw = sz.x * 0.76, ih = sz.y * 0.76;
            const double ar = (double)sw / (double)sh;
            if (iw / ih > ar) iw = ih * ar; else ih = iw / ar;
            dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(ic.x - iw * 0.5, ic.y - ih * 0.5),
                         ImVec2(ic.x + iw * 0.5, ic.y + ih * 0.5));
        }
    }
    ImGui::PopID();
    return pressed ? 1 : 0;
}

// ---- the panel ------------------------------------------------------

struct { int64_t x, y, w, h; } s_menu_rect = {0, 0, 0, 0};

// Minimal only (docs/refactor/ingame-gui/13-minimal-layout.md §2.2): the
// pop-up panel's own visibility, independent of menu_is_active(GMnu_*)'s
// existing radio-group selection -- clicking a button always selects that
// menu (unchanged legacy behaviour), but only this flag decides whether
// the pop-up actually draws. Read by ingame_tabcontent_draw() (frontgui_
// ingame_tabcontent.cpp) via ingame_minimal_popup_is_open() below.
bool s_minimal_popup_open = false;

bool read_menu_rect(void)
{
    const MenuNumber n = menu_id_to_number(GMnu_MAIN);
    if (n < 0)
        return false;
    const struct GuiMenu *m = get_active_menu(n);
    if (m == nullptr || m->visual_state == 0)
        return false;
    // HudPos_Right (2): shift the flush-left legacy rect so it sits
    // flush-right instead. See s_panel_offset_x's own comment above.
    s_panel_on_right = keeperfx_ui_config.hud_position == 2; // HudPos_Right
    s_panel_offset_x = s_panel_on_right
        ? ImGui::GetIO().DisplaySize.x - (double)m->width - (double)m->pos_x
        : 0.0;
    s_menu_rect.x = m->pos_x + (int64_t)s_panel_offset_x;
    s_menu_rect.y = m->pos_y;
    s_menu_rect.w = m->width;
    s_menu_rect.h = m->height;
    return true;
}

// docs/refactor/ingame-gui/12-png-icon-overrides.md §7: a background PNG
// override (stretch-to-fill, no aspect preservation -- it's a backdrop,
// not an icon) drawn in place of relief::face()'s procedural marble fill.
// Structural chrome (edge_frame/groove_h/every widget's own well/bevel)
// still draws after this, unchanged, whichever branch runs.
void draw_face_or_override(ImDrawList *dl, const ImVec2 &p0, const ImVec2 &p1, const char *ov_name)
{
    int64_t w = 0, h = 0;
    void *tex = FeIconOverrideTexture(ov_name, &w, &h);
    if (tex != nullptr)
        dl->AddImage((ImTextureID)(intptr_t)tex, p0, p1);
    else
        relief::face(dl, p0, p1);
}

void draw_background(void)
{
    // Procedural chrome over the whole panel column. Leave a transparent
    // gap over the minimap area (top-left of the panel, legacy raster) and
    // the tab-content area (lower part, legacy grids) so those show
    // through. Approximate the two holes from the legacy virtual grid
    // (minimap at ~(11,11) size ~104; tab content from y~230).
    const double x0 = (double)s_menu_rect.x;
    const double y0 = (double)s_menu_rect.y;
    const double x1 = x0 + (double)s_menu_rect.w;
    const double sy = (double)s_menu_rect.h / 400.0;

    const double tab_y0 = y0 + 196.0 * sy; // above the tab-content grids

    ImDrawList *dl = ImGui::GetWindowDrawList();

    // The panel head (minimap + gold + tab strip) as one raised, mottled
    // stone face -- continuous with the tab-content face below; the tab
    // strip's own recessed channel is the divider there, so no groove.
    // A raised outer rim frames the whole column; one engraved groove above
    // the tab strip. The minimap texture (transparent outside its diamond)
    // composites on top of this.
    draw_face_or_override(dl, ImVec2(x0, y0), ImVec2(x1, tab_y0), "background_vertical_head");
    relief::groove_h(dl, x0 + 4.0, x1 - 4.0, y0 + 150.0 * sy);
    relief::edge_frame(dl, ImVec2(x0, y0), ImVec2(x1, y0 + (double)s_menu_rect.h));
}

// ---- minimap (off-screen raster -> dynamic texture) ------------------
// Reuses the legacy panel_map_draw_slabs / _overlay_things verbatim, with
// the framebuffer redirected at a local buffer. Rendered at the same
// PanelMapX/Y the legacy path used (derived from player->minimap_pos_*),
// so front_input.c's mouse_is_over_panel_map() hit-test is unchanged and
// the ImGui::Image lands exactly where the legacy raster would have.
void *s_mm_tex = nullptr;
int64_t s_mm_tex_dim = 0;
std::vector<TbPixel> s_mm_pixels;
int64_t s_mm_diag = 0, s_mm_px = 0, s_mm_py = 0;

void render_minimap(void)
{
    const int64_t mm_upp = (s_menu_rect.w * 16 + 140 / 2) / 140;
    if (mm_upp < 1)
        return;

    // Force one rebuild of the minimap's background-blend tables so
    // setup_background() re-samples our (transparent -> black) off-screen
    // buffer rather than whatever a prior session left cached -- otherwise
    // the fog-of-war / wall / gold classes stay blended toward a stale
    // background colour (was reddish, from the old near-black-red clear).
    static bool s_bg_reset = false;
    if (!s_bg_reset)
    {
        reset_panel_map_background_cache();
        s_bg_reset = true;
    }
    int64_t mmzoom;
    if (16 / mm_upp < 3)
        mmzoom = local_state.minimap_zoom / scale_value_for_resolution_with_upp(2, mm_upp);
    else
        mmzoom = local_state.minimap_zoom;

    s_mm_px = scale_value_for_resolution_with_upp(local_state.minimap_pos_x, mm_upp);
    s_mm_py = scale_value_for_resolution_with_upp(local_state.minimap_pos_y, mm_upp);

    // Buffer generously covers [0 .. px + diamond]. Fixed slack (512) so a
    // stale MapDiagonalLength never makes it too small (which showed as
    // half-drawn rows). Capped at 1024.
    int64_t dim = (int64_t)(s_mm_px + 512);
    if (dim > 1024) dim = 1024;
    if (dim < 128) dim = 128;
    if (dim != s_mm_tex_dim || s_mm_tex == nullptr)
    {
        if (s_mm_tex != nullptr) RendererDestroyDynamicTexture(s_mm_tex);
        s_mm_tex = RendererCreateDynamicTexture(dim, dim);
        s_mm_tex_dim = dim;
    }
    if (s_mm_tex == nullptr)
        return;
    // Transparent clear -- outside the minimap diamond the panel's own
    // solid fill (draw_background) shows through, so the diamond's corners
    // read as panel rather than a black square. panel_map_draw_slabs draws
    // every in-diamond slab opaque (revealed or shadowed), so only the
    // out-of-diamond corners stay transparent.
    s_mm_pixels.assign((size_t)dim * (size_t)dim, TbPixel{0, 0, 0, 0});

    // panel_map_draw_slabs / setup_panel_colors resolve every slab colour
    // through RendererGetActivePalette() -- at ImGui present time that is
    // NOT the game's engine palette (same gotcha as the panel sprites), so
    // rock/lava/water/fog all decoded to the wrong (reddish) hues. Force
    // engine_palette for the capture, exactly like FeGuiPanelTexture.
    {
        FeOffscreenTarget cap(s_mm_pixels.data(), dim, dim, engine_palette);
        panel_map_draw_slabs(local_state.minimap_pos_x, local_state.minimap_pos_y, mm_upp, mmzoom);
        panel_map_draw_overlay_things(mm_upp, mmzoom, local_state.minimap_zoom);
    }
    s_mm_diag = MapDiagonalLength;

    RendererUpdateDynamicTexture(s_mm_tex, s_mm_pixels.data(), dim, dim);
}

// The minimap's absolute screen origin, captured here rather than
// recomputed by callers -- draw_panel_horizontal() aliases s_menu_rect to
// region A only for the duration of these calls, so front_input.c's
// minimap click/drag/zoom hit-testing (ingame_panel_minimap_screen_pos())
// needs the value from *this* frame's actual draw, not whatever
// s_menu_rect holds afterward.
int64_t s_mm_abs_x = 0, s_mm_abs_y = 0;

void draw_minimap_and_compass(void)
{
    if (s_mm_tex == nullptr || s_mm_diag <= 0 || s_mm_tex_dim <= 0)
        return;
    const double d = (double)s_mm_diag;
    const double dim = (double)s_mm_tex_dim;
    s_mm_abs_x = s_menu_rect.x + s_mm_px;
    s_mm_abs_y = s_menu_rect.y + s_mm_py;
    const ImVec2 org((double)(s_menu_rect.x + s_mm_px), (double)(s_menu_rect.y + s_mm_py));
    const ImVec2 uv0((double)s_mm_px / dim, (double)s_mm_py / dim);
    const ImVec2 uv1(((double)s_mm_px + d) / dim, ((double)s_mm_py + d) / dim);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 c(org.x + d * 0.5, org.y + d * 0.5);

    // Octagonal groove frame around the minimap recess -- 8 engraved
    // segments (top/bottom horizontals, left/right verticals, 4 corner
    // diagonals), each facing the circle centre, like the legacy panel's
    // plate divisions.
    {
        const double R = d * 0.5 + 3.0;    // just outside the bezel
        const double k = R * 0.41421356;    // regular-octagon half-side
        const ImVec2 N0(c.x - k, c.y - R), N1(c.x + k, c.y - R);
        const ImVec2 S0(c.x - k, c.y + R), S1(c.x + k, c.y + R);
        const ImVec2 W0(c.x - R, c.y - k), W1(c.x - R, c.y + k);
        const ImVec2 E0(c.x + R, c.y - k), E1(c.x + R, c.y + k);
        relief::groove(dl, N0, N1);
        relief::groove(dl, S0, S1);
        relief::groove(dl, W0, W1);
        relief::groove(dl, E0, E1);
        relief::groove(dl, W0, N0);
        relief::groove(dl, N1, E0);
        relief::groove(dl, W1, S0);
        relief::groove(dl, S1, E1);
    }

    // Deep circular recess + raised bronze bezel: the diamond map sits in
    // the well and the bezel laps over its points.
    relief::well_circle(dl, c, d * 0.5 + 2.0);
    dl->AddImage((ImTextureID)(intptr_t)s_mm_tex, org, ImVec2(org.x + d, org.y + d), uv0, uv1);
    relief::ring(dl, c, d * 0.5 + 3.0, d * 0.5 - 7.0); // ~10px bezel

    // Compass -- N/S/E/W letters rotated by the camera angle around the
    // minimap centre (mirrors draw_overlay_compass()).
    struct PlayerInfo *player = get_my_player();
    const struct Camera *cam = get_local_active_camera(player);
    if (cam == nullptr)
        return;
    const double r = d * 0.5 - 21.0; // pulled in from the bezel toward the centre
    const int64_t a = cam->rotation_angle_x;
    const double s = (double)LbSinL(a) / (double)(1 << LbFPMath_TrigmBits);
    const double co = (double)LbCosL(a) / (double)(1 << LbFPMath_TrigmBits);
    struct { const char *t; double dx, dy; } dirs[] = {
        { get_string(GUIStr_MapN), -s, -co },
        { get_string(GUIStr_MapS),  s,  co },
        { get_string(GUIStr_MapE),  co, -s },
        { get_string(GUIStr_MapW), -co,  s },
    };
    FeStylePushFont(FeFont_Body);
    for (const auto &dir : dirs)
    {
        const ImVec2 tsz = ImGui::CalcTextSize(dir.t);
        dl->AddText(ImVec2(c.x + dir.dx * r - tsz.x * 0.5, c.y + dir.dy * r - tsz.y * 0.5),
                    ImGui::GetColorU32(ImVec4(1, 1, 1, 0.9)), dir.t);
    }
    FeStylePopFont();
}

void draw_gold(void)
{
    const struct PlayerInfo *player = get_my_player();
    const struct Dungeon *dungeon = get_players_dungeon(player);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%lld", (long long)dungeon->total_money_owned);

    FeStylePushFont(FeFont_Heading);
    const ImVec2 tsz = ImGui::CalcTextSize(buf);
    const double cx = (double)s_menu_rect.x + (double)s_menu_rect.w * 0.5;
    const double cy = (double)s_menu_rect.y + (double)s_menu_rect.h * (67.0 / 200.0);
    ImGui::GetWindowDrawList()->AddText(ImVec2(cx - tsz.x * 0.5, cy),
        ImGui::GetColorU32(ImVec4(1.0, 0.86, 0.4, 1.0)), buf);
    FeStylePopFont();
}

void draw_tabs(void)
{
    // Icons from the query panel's vocabulary (not the legacy tab sprites,
    // which bake in their own frame): a pale-green "?" for info, the
    // keeper-coloured room / creature player-symbols, and the research /
    // workshop room icons for spells / manufacture.
    struct TabSpec {
        int64_t id; MenuID target; int64_t spr; bool colorize; const char *glyph; ImU32 glyph_col;
    };
    const TabSpec tabs[] = {
        { BID_INFO_TAB,   GMnu_QUERY,    0, false, "?", IM_COL32(168, 224, 168, 255) },
        { BID_ROOM_TAB,   GMnu_ROOM,     (int64_t)GPS_plyrsym_symbol_room_red_std_a,   true,  nullptr, 0 },
        { BID_SPELL_TAB,  GMnu_SPELL,    (int64_t)GPS_room_research_std_s,             false, nullptr, 0 },
        { BID_MNFCT_TAB,  GMnu_TRAP,     (int64_t)GPS_room_workshop_std_s,             false, nullptr, 0 },
        { BID_CREATR_TAB, GMnu_CREATURE, (int64_t)GPS_plyrsym_symbol_player_red_std_a, true,  nullptr, 0 },
    };
    // A tab "spangles" when it has an unseen new item -- mirrors
    // draw_menu_spangle(): flash_button_index, mapped to a tab id.
    const int64_t flash_tab = (kfx_frontend_state.flash_button_index != 0)
        ? button_designation_to_tab_designation(kfx_frontend_state.flash_button_index) : 0;

    ImVec2 flash_p0(0, 0), flash_p1(0, 0);
    bool have_flash = false;

    // Recessed channel behind the whole tab row -- the plinths sit in it.
    // Span the panel's full inner width (x0+2 .. x1-2), the same extent the
    // tab-content well below uses, so their edges line up.
    const double chx0 = (double)s_menu_rect.x + 2.0;
    const double chx1 = (double)(s_menu_rect.x + s_menu_rect.w) - 2.0;
    double chy0 = FLT_MAX, chy1 = -FLT_MAX;
    for (const auto &t : tabs)
    {
        ImVec2 p0, p1; bool en = true;
        if (!btn_rect(t.id, &p0, &p1, &en))
            continue;
        chy0 = fminf(chy0, p0.y);
        chy1 = fmaxf(chy1, p1.y);
    }
    if (chy1 > chy0)
    {
        // Flat recess-tone fill, no bevel -- the tabs sit right at the
        // channel's left/right edges, so a sunken bevel there just peeked
        // out past the end tabs as a stray vertical line.
        const ImU32 recess = relief::mix(relief::tones().well_top, relief::tones().well_bot, 0.5);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(chx0, chy0 - 3.0), ImVec2(chx1, chy1 + 3.0), recess, 3.0);
    }

    for (const auto &t : tabs)
    {
        ImVec2 p0, p1; bool en = true;
        if (!btn_rect(t.id, &p0, &p1, &en))
            continue;
        const bool sel = menu_is_active(t.target);
        char sid[16]; std::snprintf(sid, sizeof(sid), "tab%" PRId64, (int64_t)(t.id));
        if (tab_button(sid, t.spr, t.colorize, t.glyph, t.glyph_col, p0, p1, sel) == 1)
            request_btn(&gui_set_menu_mode, (int64_t)t.target, 0);

        if (t.id == flash_tab && !sel)
        {
            flash_p0 = p0; flash_p1 = p1; have_flash = true;
        }
    }

    // Spangle in a second pass so it's over every tab icon, never tucked
    // behind an adjacent one. Pulsing glow ring, corner spark dots.
    if (have_flash)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const double t = (double)ImGui::GetTime();
        const double pulse = 0.45 + 0.45 * std::sin(t * 6.0);
        dl->AddRect(flash_p0, flash_p1,
                    ImGui::GetColorU32(ImVec4(1.0, 0.93, 0.4, pulse)), 2.0, 0, 3.0);
        const double rad = 2.0 + 1.5 * std::sin(t * 9.0);
        const ImU32 spark = ImGui::GetColorU32(ImVec4(1.0, 1.0, 0.75, 0.9));
        dl->AddCircleFilled(flash_p0, rad, spark);
        dl->AddCircleFilled(ImVec2(flash_p1.x, flash_p0.y), rad, spark);
        dl->AddCircleFilled(ImVec2(flash_p0.x, flash_p1.y), rad, spark);
        dl->AddCircleFilled(flash_p1, rad, spark);
    }
}

void draw_nav_buttons(void)
{
    // M / + / - / cpu seated in recessed triangular pockets at the four
    // corners of the minimap frame: a 12px marble gap from the panel edge,
    // and each pocket's hypotenuse held 12px clear of the minimap's octagon
    // facet (leg length solved per corner). cpu (autopilot) stays lit.
    const double GAP = 12.0;
    const double ccx = (double)(s_menu_rect.x + s_mm_px) + (double)s_mm_diag * 0.5;
    const double ccy = (double)(s_menu_rect.y + s_mm_py) + (double)s_mm_diag * 0.5;
    const double octR = (double)s_mm_diag * 0.5 + 3.0;      // minimap octagon radius
    const double clr  = octR + 12.0;                        // octagon radius + hyp clearance

    const double xl = (double)s_menu_rect.x + GAP;
    const double xr = (double)(s_menu_rect.x + s_menu_rect.w) - GAP;
    const double yt = (double)s_menu_rect.y + GAP;
    const double yb = ccy + octR;                            // aligned with the octagon's base
    // leg = D - sqrt(2)*clr, D = dot((corner->centre), inward dir)
    auto leg_for = [&](double px, double py, double sx, double sy) {
        return sx * (ccx - px) + sy * (ccy - py) - 1.41421356 * clr;
    };

    if (corner_nav("navM", "M", xl, yt, +1.0, +1.0, leg_for(xl, yt, +1, +1), true, false) == 1)
        request_btn(&gui_go_to_map, 0, 0);
    if (corner_nav("navP", "+", xr, yt, -1.0, +1.0, leg_for(xr, yt, -1, +1), true, false) == 1)
        request_btn(&gui_zoom_in, 0, 0);
    if (corner_nav("navN", "-", xr, yb, -1.0, -1.0, leg_for(xr, yb, -1, -1), true, false) == 1)
        request_btn(&gui_zoom_out, 0, 0);

    const struct GuiButton *asst = find_btn(BID_ASSIST);
    const bool asst_en = asst == nullptr || (asst->flags & LbBtnF_Enabled) != 0;
    const struct Dungeon *d = get_players_dungeon(get_my_player());
    const bool asst_on = d != nullptr && (d->computer_enabled & 0x01) != 0;
    if (corner_nav("navCPU", "cpu", xl, yb, +1.0, -1.0, leg_for(xl, yb, +1, -1), asst_en, asst_on) == 1)
        request_btn(&gui_turn_on_autopilot, 0, 0);
}

// Event markers -- fully ImGui-drawn (the legacy sprites didn't sit with
// the rest of the HUD): a rounded body + a coloured glyph or one of the
// room/spell/trap/creature tab icons, pulsing gold while the event is
// unread, steady gold while it's the open one.
struct EvStyle {
    const char *glyph;
    int64_t icon_spr;
    ImU32 col;
    bool grad_top;
    bool colorize;     // remap the icon to my keeper colour (room / player symbols)
    bool button_spr;   // icon is a GBS_ (FeSpriteTexture) not a GPS_ (FeGuiPanelTexture)
};

EvStyle event_style(int64_t kind)
{
    // Same icons the query panel uses: room / player symbols
    // (get_player_colored_icon_idx), research + workshop room icons; the
    // crossed-swords fight symbol for combat / attacks.
    switch (kind)
    {
    case EvKind_NewRoomResrch: case EvKind_RoomTakenOver:
    case EvKind_TreasureRoomFull: case EvKind_NeedTreasureRoom:
        return { nullptr, (int64_t)GPS_plyrsym_symbol_room_red_std_a, 0, false, true, false };
    case EvKind_NewCreature: case EvKind_CreatrScavenged: case EvKind_CreaturePayday:
    case EvKind_CreatrIsAnnoyed: case EvKind_CreatrHungry: case EvKind_PrisonerStarving:
        return { nullptr, (int64_t)GPS_plyrsym_symbol_player_red_std_a, 0, false, true, false };
    case EvKind_NewSpellResrch: case EvKind_SpellPickedUp:
        return { nullptr, (int64_t)GPS_room_research_std_s, 0, false, false, false };
    case EvKind_NewTrap: case EvKind_NewDoor:
    case EvKind_TrapCrateFound: case EvKind_DoorCrateFound:
        return { nullptr, (int64_t)GPS_room_workshop_std_s, 0, false, false, false };
    case EvKind_HeartAttacked: case EvKind_EnemyFight: case EvKind_FriendlyFight:
    case EvKind_Breach: case EvKind_RoomUnderAttack: case EvKind_RoomLost:
    case EvKind_AlarmTriggered:
        return { nullptr, (int64_t)GBS_guisymbols_sym_fight, 0, false, false, true };
    case EvKind_Objective:
        return { "?", 0, IM_COL32(120, 220, 110, 255), false, false, false };
    case EvKind_Information:
        return { "i", 0, IM_COL32(120, 175, 240, 255), false, false, false };
    case EvKind_QuickInformation:
        return { "i", 0, IM_COL32(120, 220, 120, 255), false, false, false };
    default:
        return { "!", 0, IM_COL32(150, 195, 255, 255), true, false, false };
    }
}

void ev_glyph(ImDrawList *dl, const ImVec2 &p0, const ImVec2 &sz, const char *g, ImU32 col, bool grad_top)
{
    FeStylePushFont(FeFont_Heading);
    ImFont *font = ImGui::GetFont();
    const double fs = sz.y * 0.82;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0, g);
    const ImVec2 tp(p0.x + (sz.x - ts.x) * 0.5, p0.y + (sz.y - ts.y) * 0.5);
    dl->AddText(font, fs, tp, col, g);
    if (grad_top)   // fade toward white over the top of the glyph
    {
        dl->PushClipRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y * 0.42), true);
        dl->AddText(font, fs, tp, IM_COL32(235, 245, 255, 255), g);
        dl->PopClipRect();
    }
    FeStylePopFont();
}

// One marker's button + content + border, at an explicit rect -- shared by
// the vertical (stacked) and horizontal (row, GUI_POSITION Bottom,
// docs/refactor/ingame-gui/11-horizontal-layout.md) layouts, which only
// differ in where they place each slot.
void draw_one_event_marker(int64_t slot, const ImVec2 &p0, const ImVec2 &sz, ImDrawFlags outer_round)
{
    const EventIndex evidx = get_my_event_button_index(slot);
    if (evidx == 0)
        return;
    const struct Event *ev = &kfx_sim_state.event[evidx];
    const ImVec2 p1(p0.x + sz.x, p0.y + sz.y);

    char sid[16]; std::snprintf(sid, sizeof(sid), "ev%" PRId64, (int64_t)(slot));
    ImGui::SetCursorScreenPos(p0);
    ImGui::PushID(sid);
    const bool pressed = ImGui::InvisibleButton("e", sz,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool rclick  = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ImGui::PopID();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const bool sel    = (evidx == my_visible_event_idx);
    const bool unread = !(my_event_button_state[evidx] & EvBtnS_Read);
    const EvStyle st  = event_style(ev->kind);

    dl->AddRectFilled(p0, p1, IM_COL32(30, 22, 13, 240), 5.0, outer_round);
    relief::bevel(dl, p0, p1, 1.5, true); // slim raised edge -- reads as a token, not a sticker

    // Content pulled ~2px toward the sidebar (the marker mostly sits
    // off-panel), sized to nearly fill the cell.
    const ImVec2 c0(p0.x + 1.0, p0.y + 1.0);
    const ImVec2 csz(sz.x - 4.0, sz.y - 2.0);
    if (st.icon_spr != 0)
    {
        const int64_t spr = (st.colorize && !st.button_spr)
            ? get_player_colored_icon_idx(st.icon_spr, my_player_number) : st.icon_spr;
        int64_t w = 0, h = 0;
        void *tex = st.button_spr ? FeSpriteTexture(spr, &w, &h)
                                  : FeGuiPanelTexture(spr, &w, &h);
        if (tex != nullptr && w > 0 && h > 0)
        {
            double iw = csz.x, ih = csz.y;
            const double ar = (double)w / (double)h;
            if (iw / ih > ar) iw = ih * ar; else ih = iw / ar;
            const ImVec2 i0(c0.x + (csz.x - iw) * 0.5, c0.y + (csz.y - ih) * 0.5);
            dl->AddImage((ImTextureID)(intptr_t)tex, i0, ImVec2(i0.x + iw, i0.y + ih));
        }
    }
    else
    {
        ev_glyph(dl, c0, csz, st.glyph, st.col, st.grad_top);
    }

    ImU32 bcol = IM_COL32(90, 70, 42, 200);
    double bth = 1.0;
    if (sel) { bcol = IM_COL32(255, 219, 102, 255); bth = 2.0; }
    else if (unread)
    {
        const double pulse = 0.35 + 0.45 * (double)std::sin(ImGui::GetTime() * 7.0);
        bcol = ImGui::GetColorU32(ImVec4(1.0, 0.9, 0.5, pulse));
        bth = 2.0;
    }
    if (hovered) { bcol = IM_COL32(220, 62, 40, 255); bth = 2.0; }
    dl->AddRect(p0, p1, bcol, 5.0, outer_round, bth);

    if (pressed)      request_btn(&gui_open_event, 0, slot);
    else if (rclick)  request_btn(&gui_kill_event, 0, slot);
}

void draw_event_markers(void)
{
    // Anchor the column to slot 0's legacy rect, then stack the markers at a
    // fixed pitch. The legacy per-slot rects scale 30 virtual px up to
    // ~55-80 screen px -- far taller than the ~square markers we draw -- so
    // keying each marker to the vertical centre of its own oversized slot
    // left a big gap between them. Each slot still owns a fixed y (an empty
    // slot leaves a gap, as in the legacy layout); a run of populated slots
    // now reads as a contiguous stack.
    const struct GuiButton *b0 = find_btn(BID_MSG_EV01);
    if (b0 == nullptr)
        return;
    const double mw     = 40.0;
    // GUI_POSITION: the markers sit just outside the panel, on whichever
    // side is away from it -- past the right edge when the panel is on
    // the left (today's default), past the left edge when it's mirrored
    // onto the right. RoundCornersLeft/Right (below, at the fill + border)
    // flip with it so the token's rounded side always faces outward.
    const double mx = s_panel_on_right
        ? (double)s_menu_rect.x - mw - 3.0
        : (double)(s_menu_rect.x + s_menu_rect.w) + 3.0;
    const double mh     = 40.0;
    const double pitch  = mh + 1.0;                                // 1px hairline, no floaty gap
    const double base_y = (double)b0->scr_pos_y + (double)b0->height; // bottom of slot 0
    // The outer (away-from-panel) corner is the rounded one; the inner
    // corner sits square against the panel edge -- flips with the side.
    const ImDrawFlags outer_round = s_panel_on_right
        ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight;

    for (int64_t slot = 0; slot < 13; slot++)
    {
        const struct GuiButton *b = find_btn(BID_MSG_EV01 + slot);
        if (b == nullptr)
            continue;
        const EventIndex evidx = get_my_event_button_index(slot);
        if (evidx == 0)
            continue;
        double my = base_y - (double)(slot + 1) * pitch;
        if (flag_is_set(kfx_sim_state.event[evidx].flags, EvF_BtnFalling))
            my = interpolate_synced(my - pitch, my);
        draw_one_event_marker(slot, ImVec2(mx, my), ImVec2(mw, mh), outer_round);
    }
}

// GUI_POSITION Bottom (docs/refactor/ingame-gui/11-horizontal-layout.md):
// a row along region C's top edge instead of a stack past the panel's
// edge -- newest closest to region B (the grid), oldest trailing toward
// the screen edge. No falling-slide animation yet (open question §11.1/2:
// order + overflow past 13 slots aren't tuned either) -- markers just snap
// into place; token size shrinks first, so as many as fit at typical
// widths still show before any would need to be dropped.
void draw_event_markers_horizontal(const HudRect &r)
{
    const struct GuiButton *b0 = find_btn(BID_MSG_EV01);
    if (b0 == nullptr)
        return;
    const double mh = std::min(r.h() - 2.0, 40.0);
    const double mw = mh;
    const double pitch = mw + 1.0;
    const int64_t max_slots = pitch > 0.0 ? (int64_t)(r.w() / pitch) : 0;

    int64_t shown = 0;
    for (int64_t slot = 0; slot < 13 && shown < max_slots; slot++)
    {
        const struct GuiButton *b = find_btn(BID_MSG_EV01 + slot);
        if (b == nullptr)
            continue;
        if (get_my_event_button_index(slot) == 0)
            continue;
        const double mx = r.x0 + (double)shown * pitch;
        draw_one_event_marker(slot, ImVec2(mx, r.y0), ImVec2(mw, mh), ImDrawFlags_RoundCornersTop);
        shown++;
    }
}

// Minimal (docs/refactor/ingame-gui/13-minimal-layout.md §1.3): a vertical
// stack directly below the minimap, same edge as it, rather than an
// independent fixed screen edge -- the two clusters read as one group.
// Same "shrink the token first, then just crowd" overflow treatment as
// draw_event_markers_horizontal() -- no scrolling. `r` shares the
// minimap's own (much wider) column, so the marker -- narrower than that
// column -- must hug whichever of `r`'s two edges is the actual screen
// edge (`right`): the left edge when the minimap is upper-left, the right
// edge when upper-right, never always `r.x0` (live-tested: "when minimap
// is in upper-right, the event-markers aren't pinned to side" -- they were
// left-aligned within the column regardless of which side of the screen
// that column was actually on).
void draw_event_markers_minimal(const HudRect &r, bool right)
{
    const struct GuiButton *b0 = find_btn(BID_MSG_EV01);
    if (b0 == nullptr)
        return;
    const double mw = std::min(r.w() - 2.0, 40.0);
    const double mh = mw;
    const double pitch = mh + 1.0;
    const int64_t max_slots = pitch > 0.0 ? (int64_t)(r.h() / pitch) : 0;
    const double mx = right ? (r.x1 - mw) : r.x0;

    int64_t shown = 0;
    for (int64_t slot = 0; slot < 13 && shown < max_slots; slot++)
    {
        const struct GuiButton *b = find_btn(BID_MSG_EV01 + slot);
        if (b == nullptr)
            continue;
        if (get_my_event_button_index(slot) == 0)
            continue;
        const double my = r.y0 + (double)shown * pitch;
        draw_one_event_marker(slot, ImVec2(mx, my), ImVec2(mw, mh), ImDrawFlags_RoundCornersBottom);
        shown++;
    }
}

// ==== GUI_POSITION Bottom (docs/refactor/ingame-gui/11-horizontal-layout.md) ====
// A strip along the bottom of the screen, three regions from
// frontgui_hud_layout.h: gold+minimap+nav (region A, reusing the vertical
// layout's own head drawing verbatim against region A's own rect via the
// s_menu_rect-aliasing trick below), tab-header row + the 2-column grid
// (region B, frontgui_ingame_cells.cpp's grid_begin() already branches on
// GUI_POSITION for the column count), then event markers + the message
// queue (region C -- the queue itself moves in frontgui_ingame_text.cpp).

// Gold counter, but sized to a rect instead of derived from s_menu_rect's
// own (very different, for a whole vertical panel) proportions -- see
// draw_gold()'s 67/200 constant, which only makes sense against a panel
// tall enough that gold is a thin sliver at the very top of it.
void draw_gold_horizontal(const HudRect &r)
{
    const struct PlayerInfo *player = get_my_player();
    const struct Dungeon *dungeon = get_players_dungeon(player);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%lld", (long long)dungeon->total_money_owned);

    FeStylePushFont(FeFont_Body); // region A's gold strip is thin -- FeFont_Heading overflowed it
    const ImVec2 tsz = ImGui::CalcTextSize(buf);
    const ImVec2 c((r.x0 + r.x1) * 0.5, (r.y0 + r.y1) * 0.5);
    ImGui::GetWindowDrawList()->AddText(ImVec2(c.x - tsz.x * 0.5, c.y - tsz.y * 0.5),
        ImGui::GetColorU32(ImVec4(1.0, 0.86, 0.4, 1.0)), buf);
    FeStylePopFont();
}

// The 5 tab headers as a row across region B's top edge, instead of
// btn_rect()'s legacy-button rects (those are baked from GMnu_MAIN's own
// flush-left vertical creation -- meaningless here). tab_button() itself
// is pure geometry, so it's reused unchanged; only the rect it's handed
// differs. Spangle-flash kept (cheap, and it's the same visual language
// the vertical tab strip uses).
void draw_tabs_horizontal(const HudRect &r)
{
    struct TabSpec {
        int64_t id; MenuID target; int64_t spr; bool colorize; const char *glyph; ImU32 glyph_col;
    };
    const TabSpec tabs[] = {
        { BID_INFO_TAB,   GMnu_QUERY,    0, false, "?", IM_COL32(168, 224, 168, 255) },
        { BID_ROOM_TAB,   GMnu_ROOM,     (int64_t)GPS_plyrsym_symbol_room_red_std_a,   true,  nullptr, 0 },
        { BID_SPELL_TAB,  GMnu_SPELL,    (int64_t)GPS_room_research_std_s,             false, nullptr, 0 },
        { BID_MNFCT_TAB,  GMnu_TRAP,     (int64_t)GPS_room_workshop_std_s,             false, nullptr, 0 },
        { BID_CREATR_TAB, GMnu_CREATURE, (int64_t)GPS_plyrsym_symbol_player_red_std_a, true,  nullptr, 0 },
    };
    const int64_t flash_tab = (kfx_frontend_state.flash_button_index != 0)
        ? button_designation_to_tab_designation(kfx_frontend_state.flash_button_index) : 0;
    ImVec2 flash_p0(0, 0), flash_p1(0, 0);
    bool have_flash = false;

    const double slot_w = (r.x1 - r.x0) / 5.0;
    for (int64_t i = 0; i < 5; i++)
    {
        const TabSpec &t = tabs[i];
        const ImVec2 p0(r.x0 + (double)i * slot_w, r.y0);
        const ImVec2 p1(r.x0 + (double)(i + 1) * slot_w, r.y1);
        const bool sel = menu_is_active(t.target);
        char sid[16]; std::snprintf(sid, sizeof(sid), "htab%" PRId64, (int64_t)(t.id));
        if (tab_button(sid, t.spr, t.colorize, t.glyph, t.glyph_col, p0, p1, sel) == 1)
            request_btn(&gui_set_menu_mode, (int64_t)t.target, 0);
        if (t.id == flash_tab && !sel)
        {
            flash_p0 = p0; flash_p1 = p1; have_flash = true;
        }
    }
    if (have_flash)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const double t = (double)ImGui::GetTime();
        const double pulse = 0.45 + 0.45 * std::sin(t * 6.0);
        dl->AddRect(flash_p0, flash_p1,
                    ImGui::GetColorU32(ImVec4(1.0, 0.93, 0.4, pulse)), 2.0, 0, 3.0);
        const double rad = 2.0 + 1.5 * std::sin(t * 9.0);
        const ImU32 spark = ImGui::GetColorU32(ImVec4(1.0, 1.0, 0.75, 0.9));
        dl->AddCircleFilled(flash_p0, rad, spark);
        dl->AddCircleFilled(ImVec2(flash_p1.x, flash_p0.y), rad, spark);
        dl->AddCircleFilled(ImVec2(flash_p0.x, flash_p1.y), rad, spark);
        dl->AddCircleFilled(flash_p1, rad, spark);
    }
}

// Which HudBottomWidthMode the active tab wants (frontgui_hud_layout.h's
// own comment) -- read before hud_layout_frame() runs, so region B's
// width is right for this tab before ingame_tabcontent_draw() gets its
// rect. Mirrors ingame_tabcontent_draw()'s own possession/query check
// (frontgui_ingame_tabcontent.cpp) -- duplicated rather than shared,
// since that logic lives downstream of the layout decision this makes.
HudBottomWidthMode determine_bottom_width_mode(void)
{
    const struct PlayerInfo *me = get_my_player();
    const bool in_possession = me != nullptr
        && (me->view_type == PVT_CreatureContrl || me->view_type == PVT_CreaturePasngr);
    if (in_possession
     || menu_is_active(GMnu_CREATURE_QUERY1) || menu_is_active(GMnu_CREATURE_QUERY2)
     || menu_is_active(GMnu_CREATURE_QUERY3) || menu_is_active(GMnu_CREATURE_QUERY4))
        return HudBottomWidth_Narrow;
    if (menu_is_active(GMnu_CREATURE))
        return HudBottomWidth_Wide;
    return HudBottomWidth_Normal;
}

// One raised, mottled stone plate under the whole strip -- no per-region
// grooves yet (open for polish once the composition itself is confirmed).
void draw_background_horizontal(const HudRect &whole)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p0(whole.x0, whole.y0), p1(whole.x1, whole.y1);
    draw_face_or_override(dl, p0, p1, "background_horizontal");
    relief::edge_frame(dl, p0, p1);
}

void draw_panel_horizontal(const HudLayout &hl)
{
    const HudRect &mm_r = hl.region[HudRegion_Minimap];
    const HudRect whole = { hl.region[HudRegion_Gold].x0, hl.region[HudRegion_Gold].y0,
                            hl.region[HudRegion_Messages].x1, mm_r.y1 };

    // Framebuffer-swap raster -- must run before any ImGui window is open.
    // Alias s_menu_rect to region A's minimap rect so render_minimap()
    // (unchanged) scales/positions against it instead of a whole vertical
    // panel's width.
    const auto saved_menu_rect = s_menu_rect;
    s_menu_rect = { (int64_t)mm_r.x0, (int64_t)mm_r.y0, (int64_t)mm_r.w(), (int64_t)mm_r.h() };
    render_minimap();
    s_menu_rect = saved_menu_rect;

    ImGui::SetNextWindowPos(ImVec2(whole.x0, whole.y0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(whole.w(), whole.h()), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0);
    ImGui::Begin("##IngameSidebarHorizontal", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                 | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavInputs
                 | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    draw_background_horizontal(whole);

    s_menu_rect = { (int64_t)mm_r.x0, (int64_t)mm_r.y0, (int64_t)mm_r.w(), (int64_t)mm_r.h() };
    draw_minimap_and_compass();
    draw_nav_buttons();
    s_menu_rect = saved_menu_rect;

    draw_gold_horizontal(hl.region[HudRegion_Gold]);
    draw_tabs_horizontal(hl.region[HudRegion_TabStrip]);
    // Phase 5's active-tab body, region B's grid slice only (excludes the
    // tab-header row above it) -- see ingame_tabcontent_draw()'s own Bottom
    // branch (frontgui_ingame_tabcontent.cpp).
    const HudRect &tc = hl.region[HudRegion_TabContent];
    ingame_tabcontent_draw(tc.x0, tc.y0, tc.w(), tc.h());
    draw_event_markers_horizontal(hl.region[HudRegion_Events]);

    ImGui::End();
}

// ==== GUI_POSITION Minimal (docs/refactor/ingame-gui/13-minimal-layout.md) ====
// No persistent panel at all: a corner-pinned minimap+gold+event cluster
// (HudRegion_Minimap/Gold/Events, upper-left or upper-right per
// MINIMAP_CORNER) and a free-floating Room/Spell/Trap/Creature/Query
// button cluster in the diagonally opposite corner
// (HudRegion_TabStrip -- nominal, only its anchor point is read) that
// toggles a pop-up panel (HudRegion_TabContent; the pop-up window itself
// is owned by ingame_tabcontent_draw(), frontgui_ingame_tabcontent.cpp).

// Same icon set draw_tabs()/draw_tabs_horizontal() use, but icons only --
// no recessed channel or plinth fill, matching the layout's own
// no-chrome aesthetic. Returns true on left-click.
bool minimal_cluster_button(const char *sid, int64_t spr, bool colorize, const char *glyph,
                            ImU32 glyph_col, const ImVec2 &p0, double sz, bool selected)
{
    ImGui::SetCursorScreenPos(p0);
    ImGui::PushID(sid);
    const bool pressed = ImGui::InvisibleButton("b", ImVec2(sz, sz), ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + sz, p0.y + sz);
    if (glyph != nullptr)
    {
        FeStylePushFont(FeFont_Heading);
        ImFont *f = ImGui::GetFont();
        const double fs = sz * 0.60;
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0, glyph);
        dl->AddText(f, fs, ImVec2(p0.x + (sz - ts.x) * 0.5, p0.y + (sz - ts.y) * 0.5), glyph_col, glyph);
        FeStylePopFont();
    }
    else
    {
        const int64_t s = colorize ? get_player_colored_icon_idx(spr, my_player_number) : spr;
        int64_t sw = 0, sh = 0;
        void *tex = FeGuiPanelTexture(s, &sw, &sh);
        if (tex != nullptr && sw > 0 && sh > 0)
        {
            double iw = sz * 0.8, ih = sz * 0.8;
            const double ar = (double)sw / (double)sh;
            if (iw / ih > ar) iw = ih * ar; else ih = iw / ar;
            const ImVec2 ic(p0.x + sz * 0.5, p0.y + sz * 0.5);
            dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(ic.x - iw * 0.5, ic.y - ih * 0.5),
                         ImVec2(ic.x + iw * 0.5, ic.y + ih * 0.5));
        }
    }
    if (selected)      dl->AddRect(p0, p1, relief::accents().sel, 4.0, 0, 2.0);
    else if (hovered)  dl->AddRect(p0, p1, relief::accents().hover, 4.0, 0, 2.0);
    return pressed;
}

// `anchor` is HudRegion_TabStrip's nominal rect -- both its corners are
// the same point (build_minimal()'s own construction), read as the
// auto-resize window's pivot: bottom-right if the cluster is in the
// bottom-right (minimap upper-left), bottom-left if upper-right.
void draw_button_cluster_minimal(const HudRect &anchor, bool cluster_on_right)
{
    struct TabSpec {
        MenuID target; int64_t spr; bool colorize; const char *glyph; ImU32 glyph_col;
    };
    const TabSpec tabs[] = {
        { GMnu_QUERY,    0, false, "?", IM_COL32(168, 224, 168, 255) },
        { GMnu_ROOM,     (int64_t)GPS_plyrsym_symbol_room_red_std_a,   true,  nullptr, 0 },
        { GMnu_SPELL,    (int64_t)GPS_room_research_std_s,             false, nullptr, 0 },
        { GMnu_TRAP,     (int64_t)GPS_room_workshop_std_s,             false, nullptr, 0 },
        { GMnu_CREATURE, (int64_t)GPS_plyrsym_symbol_player_red_std_a, true,  nullptr, 0 },
    };

    const double sz = 40.0; // keep in sync with build_minimal()'s cluster_sz
    const double gap = 4.0;
    const int64_t   count = (int64_t)(sizeof(tabs) / sizeof(tabs[0]));
    const double cluster_w = (double)count * (sz + gap) - gap;

    // Explicit size, not ImGuiWindowFlags_AlwaysAutoResize -- that flag
    // sizes the window from the *previous* frame's content (a one-frame
    // lag), so its true on-screen size could still drift a pixel or two
    // from what build_minimal()'s cluster_sz assumes. An explicit size
    // computed from the exact same sz/gap/count is deterministic; padding
    // forced to zero so the content actually fills it edge to edge rather
    // than being inset and overflowing the last button off the window's
    // own clip rect. No residual gap with the pop-up above it (live-tested
    // twice now: "not possible to select a different one, once one is
    // open", then "the pop-up panels still overlap slightly with the
    // buttons" after the first fix).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0, 0.0));
    ImGui::SetNextWindowPos(ImVec2(anchor.x0, anchor.y0), ImGuiCond_Always,
                            ImVec2(cluster_on_right ? 1.0 : 0.0, 1.0));
    ImGui::SetNextWindowSize(ImVec2(cluster_w, sz), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0);
    ImGui::Begin("##IngameMinimalButtons", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                 | ImGuiWindowFlags_NoNavInputs);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    int64_t i = 0;
    for (const TabSpec &t : tabs)
    {
        const bool sel = menu_is_active(t.target);
        char sid[16]; std::snprintf(sid, sizeof(sid), "mcl%" PRId64, (int64_t)t.target);
        const ImVec2 bp(p0.x + (double)i * (sz + gap), p0.y);
        if (minimal_cluster_button(sid, t.spr, t.colorize, t.glyph, t.glyph_col, bp, sz, sel))
        {
            request_btn(&gui_set_menu_mode, (int64_t)t.target, 0);
            // Toggle: re-clicking the already-active+open button closes the
            // pop-up; any other click (new selection, or reopening the
            // current one) opens/keeps it open (docs/refactor/ingame-gui/
            // 13-minimal-layout.md §2.2).
            s_minimal_popup_open = !(sel && s_minimal_popup_open);
        }
        i++;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void draw_panel_minimal(const HudLayout &hl)
{
    const HudRect &mm_r = hl.region[HudRegion_Minimap];
    const HudRect &gold_r = hl.region[HudRegion_Gold];
    const HudRect &ev_r = hl.region[HudRegion_Events];
    // mm_r is the topmost region now (minimap moved above gold -- see
    // build_minimal()'s own comment), so the window's bounding box must
    // start at mm_r.y0, not gold_r.y0 -- using gold_r.y0 here left the
    // minimap's own draw calls entirely above this window's top edge,
    // clipped away by its scissor rect (live-tested: "the minimap has
    // disappeared entirely" -- gold/events, both below that edge, still
    // drew fine, which is what made this specific bug non-obvious).
    const HudRect whole = { mm_r.x0, mm_r.y0, mm_r.x1, ev_r.y1 };
    const bool corner_on_right = (keeperfx_ui_config.minimap_corner == 2); // HudMinimalCorner_UpperRight

    // Framebuffer-swap raster -- must run before any ImGui window is open.
    const auto saved_menu_rect = s_menu_rect;
    s_menu_rect = { (int64_t)mm_r.x0, (int64_t)mm_r.y0, (int64_t)mm_r.w(), (int64_t)mm_r.h() };
    render_minimap();
    s_menu_rect = saved_menu_rect;

    ImGui::SetNextWindowPos(ImVec2(whole.x0, whole.y0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(whole.w(), whole.h()), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0);
    ImGui::Begin("##IngameMinimapCluster", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                 | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavInputs
                 | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // No draw_nav_buttons() here -- the M/+/-/cpu triangular corner-pocket
    // buttons read as too busy against Minimal's own no-chrome aesthetic
    // (live-tested: "the bevel/icon triangle near the minimap should be
    // removed"). Left/Right/Bottom keep them; Minimal is deliberately
    // sparser.
    s_menu_rect = { (int64_t)mm_r.x0, (int64_t)mm_r.y0, (int64_t)mm_r.w(), (int64_t)mm_r.h() };
    draw_minimap_and_compass();
    s_menu_rect = saved_menu_rect;

    draw_gold_horizontal(gold_r);
    draw_event_markers_minimal(ev_r, corner_on_right);

    ImGui::End();

    // Diagonally opposite (bottom) corner -- own window, own pivot.
    draw_button_cluster_minimal(hl.region[HudRegion_TabStrip], !corner_on_right);

    const HudRect &tc = hl.region[HudRegion_TabContent];
    ingame_tabcontent_draw(tc.x0, tc.y0, tc.w(), tc.h());
}

// The always-on sidebar frame, VerticalRight/Left/Right shape (05-sidebar-
// frame-and-minimap.md / 10-maintainability-refactors.md's GUI_POSITION).
// read_menu_rect() must already have populated s_menu_rect.
void draw_panel_vertical(void)
{
    // Framebuffer-swap raster -- must run before any ImGui window is open.
    render_minimap();

    // Window == the panel strip (+ a margin on whichever side the event
    // markers overhang the 140-grid edge toward -- away from the panel:
    // right when it's on the left edge, left when it's mirrored onto the
    // right edge). Sized to the panel so io.WantCaptureMouse -- hence
    // busy_doing_gui via ingame_imgui_wants_mouse() -- is only set while
    // the pointer is actually over the sidebar, not the 3D view.
    const double overhang_x = s_panel_on_right ? (double)s_menu_rect.x - 44.0 : (double)s_menu_rect.x;
    ImGui::SetNextWindowPos(ImVec2(overhang_x, (double)s_menu_rect.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((double)s_menu_rect.w + 44.0, (double)s_menu_rect.h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0);
    ImGui::Begin("##IngameSidebar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                 | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavInputs
                 | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    draw_background();
    draw_minimap_and_compass();
    draw_gold();
    draw_tabs();
    // Phase 5: the active tab's body, in this same window (no z-order seam).
    ingame_tabcontent_draw((double)s_menu_rect.x, (double)s_menu_rect.y,
                           (double)s_menu_rect.w, (double)s_menu_rect.h);
    draw_nav_buttons();
    draw_event_markers();

    ImGui::End();
}

} // namespace

extern "C" void ingame_panel_minimap_screen_pos(int64_t *x, int64_t *y)
{
    if (x != nullptr) *x = s_mm_abs_x;
    if (y != nullptr) *y = s_mm_abs_y;
}

void ingame_panel_frame(void)
{
    apply_pending();

    if (ingame_gui_use_classic_hud())
        return;
    // Tab / Ctrl+Tab (toggle_gui -> GOF_ShowGui) hides the whole sidebar.
    // set_menu_visible_off leaves GMnu_MAIN's visual_state non-zero, so
    // read_menu_rect() alone won't catch this.
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) == 0)
        return;
    // The parchment / overhead map is a full-screen view -- no sidebar over it.
    const struct PlayerInfo *lp = get_my_player();
    if (lp != nullptr && lp->view_mode == PVM_ParchmentView)
        return;
    if (!read_menu_rect())
        return;
    if (s_menu_rect.w <= 0 || s_menu_rect.h <= 0)
        return;

    // GUI_POSITION Bottom (docs/refactor/ingame-gui/11-horizontal-layout.md):
    // an entirely different composition -- three regions from
    // frontgui_hud_layout.h instead of one vertical column. Left/Right stay
    // on draw_panel_vertical() unchanged (they're mirror images of the same
    // composition, not a different one).
    if (keeperfx_ui_config.hud_position == 3) // HudPos_Bottom
    {
        const ImGuiIO &io = ImGui::GetIO();
        hud_layout_frame(HudLayout_HorizontalBottom, determine_bottom_width_mode(),
                         HudMinimalCorner_UpperLeft, io.DisplaySize.x, io.DisplaySize.y);
        draw_panel_horizontal(hud_layout_current());
    }
    else if (keeperfx_ui_config.hud_position == 4) // HudPos_Minimal
    {
        const ImGuiIO &io = ImGui::GetIO();
        const HudMinimalCorner corner = (keeperfx_ui_config.minimap_corner == 2)
            ? HudMinimalCorner_UpperRight : HudMinimalCorner_UpperLeft;
        hud_layout_frame(HudLayout_Minimal, HudBottomWidth_Normal, corner, io.DisplaySize.x, io.DisplaySize.y);
        draw_panel_minimal(hud_layout_current());
    }
    else
    {
        draw_panel_vertical();
    }
}

bool ingame_minimal_popup_is_open(void) { return s_minimal_popup_open; }
