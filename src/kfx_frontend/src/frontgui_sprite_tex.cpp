#include "pre_inc.h"
#include "frontgui_sprite_tex.h"

#include "frontgui_widgets.h"
#include "frontgui_style.h"
#include "frontgui_offscreen.h"           // FeOffscreenTarget -- sprite -> texture capture

#include "bflib_guibtns.h"                 // do_sound_menu_click
#include "bflib_sprite.h"                  // struct TbSprite, load_spritesheet
#include "bflib_dernc.h"                   // LbFileLoadAt
#include "vidmode.h"                       // gui_panel_sprites
#include "bflib_vidraw.h"                  // LbSpriteDrawImmediate
#include "bflib_video.h"                   // TbPixel, LbScreen*GraphicsWindow, TbGraphicsWindow
#include "renderer/RendererManager.h"      // dynamic textures, draw-flag state
#include "custom_sprites.h"                // get_button_sprite, get_panel_sprite
#include "kfx_sim_state.h"                 // engine_palette (game palette for the sprite decode)
#include "frontgui_ingame_icon_overrides.h" // FeIconOverrideForStaticIndex -- docs/refactor/ingame-gui/12-png-icon-overrides.md

#include <imgui_internal.h>                // GImGui->NavCursorVisible -- same use as frontgui_widgets.cpp
#include "post_inc.h"

#include <map>
#include <vector>

namespace {

struct CachedSprite {
    void *texture = nullptr;   // RendererCreateDynamicTexture handle, or nullptr while unbuilt
    int64_t   width   = 0;
    int64_t   height  = 0;
    bool  built   = false;     // a successful render happened -- stop retrying
};

std::map<int64_t, CachedSprite> s_button_cache;
std::map<int64_t, CachedSprite> s_panel_cache;

// The menu runs in "minimal resolution" mode (vidmode.c LoadVResMinimal), which loads only the button
// sprites and frontend fonts: gui_panel_sprites (gui2-*.dat, the room/spell/trap/creature/player icons)
// and engine_palette (data/palette.dat) exist only in-game. So a panel sprite requested from a menu
// screen used to resolve to the engine's magenta checkerboard placeholder (bad_icon) -- and, cached by
// index, would have kept it. For those callers this keeps a private copy of the sheet and palette, loaded
// on first use and dropped as soon as the real in-game sheet is present.
struct TbSpriteSheet *s_menu_panel_sheet = nullptr;
unsigned char s_menu_palette[PALETTE_SIZE];
bool s_menu_panel_attempted = false;
bool s_menu_panel_ready = false;
std::map<int64_t, CachedSprite> s_menu_panel_cache;

bool menu_panel_sheet_ready()
{
    if (s_menu_panel_attempted)
        return s_menu_panel_ready;
    s_menu_panel_attempted = true;
    s_menu_panel_sheet = load_spritesheet("data/gui2-64.dat", "data/gui2-64.tab");
    const int64_t got = LbFileLoadAt("data/palette.dat", s_menu_palette);
    s_menu_panel_ready = (s_menu_panel_sheet != nullptr) && (got >= (int64_t)PALETTE_SIZE);
    if (!s_menu_panel_ready)
        WARNLOG("Menu panel sprites unavailable (sheet %s, palette %" PRId64 " bytes)", s_menu_panel_sheet ? "loaded" : "missing", (int64_t)(got));
    return s_menu_panel_ready;
}

void release_menu_panel_sheet()
{
    if (s_menu_panel_sheet != nullptr)
        free_spritesheet(&s_menu_panel_sheet);
    s_menu_panel_attempted = false; // may be needed again after returning to the menu
    s_menu_panel_ready = false;
}

const struct TbSprite *menu_panel_sprite(int64_t idx)
{
    if (idx < 0 || !menu_panel_sheet_ready() || idx >= num_sprites(s_menu_panel_sheet))
        return nullptr;
    return get_sprite(s_menu_panel_sheet, idx);
}

// Renders one classic button sprite into an RGBA buffer and uploads it to
// a fresh dynamic texture. Same sequence as build_cursor_pixels()
// (frontgui_style.cpp): save the ambient draw-flag/colour state (held off
// lbDisplay, left however the last engine draw set it), force a plain
// draw, redirect the framebuffer target at a local buffer, blit, restore.
// Palette is deliberately NOT overridden here (unlike the cursor, which
// runs in menu context and must force frontend_palette): these sprites are
// only ever shown while gameplay is running, decoded against the same
// active engine palette the classic gui_area_no_anim_button() draw uses.
bool render_sprite_with_palette(const struct TbSprite *spr, CachedSprite &out, unsigned char *palette)
{
    const int64_t w = spr->SWidth;
    const int64_t h = spr->SHeight;
    if (w <= 0 || h <= 0)
        return false;

    std::vector<TbPixel> pixels((size_t)w * (size_t)h, TbPixel{0, 0, 0, 0});

    // LbSpriteDrawImmediate decodes the sprite's paletted bytes through
    // RendererGetActivePalette() (LbDrawBufferSolid). At ImGui present
    // time that ambient palette isn't guaranteed to be the one a given
    // sheet was authored against -- found live, "the zoom/close icons are
    // all white": the GUI *panel* sprites came out as bright silhouettes
    // (the button sprites happen to decode fine ambiently). For the panel
    // path, force the game engine palette. engine_palette is null until a
    // level's palette loads -- bail and retry rather than cache a wrong decode.
    // (The caller decides the palette; nullptr = the ambient one.)
    const int64_t prev_flags = RendererGetDrawFlags();
    const unsigned char prev_colour = RendererGetDrawColour();
    RendererSetDrawFlags(0);
    {
        FeOffscreenTarget cap(pixels.data(), w, h, palette);
        LbSpriteDrawImmediate(0, 0, spr);
    }
    RendererSetDrawFlags(prev_flags);
    RendererSetDrawColour(prev_colour);

    void *tex = RendererCreateDynamicTexture(w, h);
    if (tex == nullptr)
        return false;
    RendererUpdateDynamicTexture(tex, pixels.data(), w, h);

    out.texture = tex;
    out.width   = w;
    out.height  = h;
    out.built   = true;
    return true;
}

bool render_sprite(const struct TbSprite *spr, CachedSprite &out, bool force_engine_palette)
{
    if (force_engine_palette && engine_palette == nullptr)
        return false; // null until a level's palette loads -- bail and retry rather than cache a wrong decode
    return render_sprite_with_palette(spr, out, force_engine_palette ? engine_palette : nullptr);
}

// Hover/press/nav-focus highlight, gated on NavCursorVisible so the
// auto-focused first item is not lit at rest -- identical rule to
// frontgui_widgets.cpp's fe_button_highlighted().
bool item_highlighted()
{
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        return true;
    return ImGui::IsItemFocused() && GImGui->NavCursorVisible;
}

// Shared geometry for FeSpriteButton() / FeSpriteButtonWidth(). Assumes the
// body font is already pushed. `spr_w/spr_h` are the sprite's pixel size.
struct SpriteBtnGeom {
    double icon_w, icon_h;
    double gap;          // icon->label spacing (0 when no label)
    ImVec2 text_sz;
    ImVec2 box;
};

SpriteBtnGeom sprite_button_geom(int64_t spr_w, int64_t spr_h, const char *label, double icon_h_req)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    SpriteBtnGeom g;
    g.icon_h = icon_h_req > 0.0 ? icon_h_req : ImGui::GetFontSize();
    g.icon_w = g.icon_h * (double)spr_w / (double)spr_h;

    const bool have_label = label != nullptr && label[0] != '\0';
    g.text_sz = have_label ? ImGui::CalcTextSize(label) : ImVec2(0, 0);
    g.gap = have_label ? style.ItemInnerSpacing.x * 2.0 : 0.0;

    g.box = ImVec2(
        g.icon_w + g.gap + g.text_sz.x + style.FramePadding.x * 2.0,
        (g.icon_h > g.text_sz.y ? g.icon_h : g.text_sz.y) + style.FramePadding.y * 2.0);
    return g;
}

void *lookup(std::map<int64_t, CachedSprite> &cache, int64_t idx,
            const struct TbSprite *(*resolve)(int64_t), bool force_engine_palette,
            int64_t *out_w, int64_t *out_h)
{
    CachedSprite &c = cache[idx];
    if (!c.built)
    {
        const struct TbSprite *spr = resolve(idx);
        if (spr != nullptr && spr->SWidth > 0 && spr->SHeight > 0)
        {
            c.width  = spr->SWidth;
            c.height = spr->SHeight;
            c.built  = render_sprite(spr, c, force_engine_palette); // false -> retried next call
        }
    }
    if (out_w != nullptr) *out_w = c.width;
    if (out_h != nullptr) *out_h = c.height;
    return c.texture;
}

} // namespace

void *FeSpriteTexture(int64_t sprite_idx, int64_t *out_w, int64_t *out_h)
{
    // Static icon-pack override (docs/refactor/ingame-gui/12-png-icon-overrides.md
    // §3.1) checked first -- a table hit means every caller of this
    // sprite_idx gets the pack's PNG with no call-site change of its own.
    void *ov = FeIconOverrideForStaticIndex(sprite_idx, /*is_button_sheet*/ true, out_w, out_h);
    if (ov != nullptr)
        return ov;
    return lookup(s_button_cache, sprite_idx, &get_button_sprite, false, out_w, out_h);
}

void *FeGuiPanelTexture(int64_t sprite_idx, int64_t *out_w, int64_t *out_h)
{
    void *ov = FeIconOverrideForStaticIndex(sprite_idx, /*is_button_sheet*/ false, out_w, out_h);
    if (ov != nullptr)
        return ov;
    if (gui_panel_sprites == nullptr)
    {
        // Menu context: no in-game sheet.
        if (out_w != nullptr) *out_w = 0;
        if (out_h != nullptr) *out_h = 0;
        // Custom icons (index >= GUI_PANEL_SPRITES_COUNT: mod/campaign sprite zips such as replacement
        // creature portraits) live in custom_sprites, which IS loaded in the menu -- get_panel_sprite()
        // resolves them correctly there, exactly as before the menu sheet existed. Only base-sheet
        // indices need the private sheet.
        const bool custom = is_custom_icon(sprite_idx) != 0;
        std::map<int64_t, CachedSprite> &cache = custom ? s_panel_cache : s_menu_panel_cache;
        CachedSprite &c = cache[sprite_idx];
        if (!c.built)
        {
            const struct TbSprite *spr = custom ? get_panel_sprite(sprite_idx) : menu_panel_sprite(sprite_idx);
            if (spr != nullptr && spr->SWidth > 0 && spr->SHeight > 0)
            {
                c.width = spr->SWidth;
                c.height = spr->SHeight;
                // The game palette when a level's has been loaded, else the private copy of the same file.
                unsigned char *palette = (engine_palette != nullptr) ? engine_palette
                    : (menu_panel_sheet_ready() ? s_menu_palette : nullptr);
                c.built = (palette != nullptr) && render_sprite_with_palette(spr, c, palette);
            }
        }
        if (c.built)
        {
            if (out_w != nullptr) *out_w = c.width;
            if (out_h != nullptr) *out_h = c.height;
        }
        return c.texture;
    }
    if (s_menu_panel_sheet != nullptr)
        release_menu_panel_sheet(); // the real sheet is loaded now (a level started)
    // Panel sprites need the game engine palette forced (see render_sprite).
    return lookup(s_panel_cache, sprite_idx, &get_panel_sprite, true, out_w, out_h);
}

void FeGuiPanelReleaseMenuSheet()
{
    release_menu_panel_sheet();
}

bool FeGuiPanelSpriteAvailable(int64_t sprite_idx)
{
    if (sprite_idx <= 0)
        return false;
    if (gui_panel_sprites != nullptr)
        return is_panel_sprite_drawable(sprite_idx);
    return is_custom_icon(sprite_idx) || (menu_panel_sprite(sprite_idx) != nullptr);
}

namespace {

// Shared body for FeSpriteButton() / FeGuiPanelButton(): the texture is
// already resolved. `label` (if any) draws beside the icon; `fallback` is
// the caption shown as a plain text button until the texture is ready
// (defaults to `label`, then `str_id`).
bool sprite_button_body(const char *str_id, void *tex, int64_t spr_w, int64_t spr_h,
                        const char *label, const char *fallback, double icon_h)
{
    if (tex == nullptr || spr_w <= 0 || spr_h <= 0)
    {
        const char *cap = (fallback != nullptr && fallback[0] != '\0') ? fallback
                        : (label != nullptr && label[0] != '\0') ? label : str_id;
        return FeButton(cap);
    }

    const ImGuiStyle &style = ImGui::GetStyle();

    FeStylePushFont(FeFont_Body);
    const SpriteBtnGeom g = sprite_button_geom(spr_w, spr_h, label, icon_h);
    const bool have_label = g.gap > 0.0 || (label != nullptr && label[0] != '\0');

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(str_id, g.box, ImGuiButtonFlags_EnableNav);
    const bool hot = item_highlighted();

    ImDrawList *dl = ImGui::GetWindowDrawList();

    if (hot)
    {
        // Soft warm wash on hover -- no hard border (the blood-red ring
        // read as harsh around the message-box / quit-modal icons).
        dl->AddRectFilled(p0, ImVec2(p0.x + g.box.x, p0.y + g.box.y),
                          IM_COL32(255, 235, 190, 40), 2.0);
    }

    // Global-alpha aware: BeginDisabled() lowers style.Alpha, so the icon
    // dims with the rest of a disabled row.
    const ImU32 tint = ImGui::GetColorU32(ImVec4(1, 1, 1, 1));
    const ImVec2 icon_p0(p0.x + style.FramePadding.x, p0.y + (g.box.y - g.icon_h) * 0.5);
    dl->AddImage((ImTextureID)(intptr_t)tex, icon_p0,
                 ImVec2(icon_p0.x + g.icon_w, icon_p0.y + g.icon_h),
                 ImVec2(0, 0), ImVec2(1, 1), tint);

    if (have_label)
    {
        const ImU32 col = hot ? ImGui::GetColorU32(ImVec4(1.0, 0.92, 0.72, 1.0))
                              : ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(ImVec2(icon_p0.x + g.icon_w + g.gap, p0.y + (g.box.y - g.text_sz.y) * 0.5),
                    col, label);
    }
    FeStylePopFont();

    if (pressed)
        do_sound_menu_click();
    return pressed;
}

} // namespace

bool FeSpriteButton(const char *str_id, int64_t sprite_idx, const char *label, double icon_h)
{
    int64_t w = 0, h = 0;
    void *tex = FeSpriteTexture(sprite_idx, &w, &h);
    return sprite_button_body(str_id, tex, w, h, label, nullptr, icon_h);
}

bool FeGuiPanelButton(const char *str_id, int64_t sprite_idx, const char *label, double icon_h)
{
    int64_t w = 0, h = 0;
    void *tex = FeGuiPanelTexture(sprite_idx, &w, &h);
    return sprite_button_body(str_id, tex, w, h, label, nullptr, icon_h);
}

bool FeGuiPanelIconButton(const char *str_id, int64_t sprite_idx, const char *fallback_label, double icon_h)
{
    int64_t w = 0, h = 0;
    void *tex = FeGuiPanelTexture(sprite_idx, &w, &h);
    return sprite_button_body(str_id, tex, w, h, nullptr, fallback_label, icon_h);
}

double FeSpriteButtonWidth(int64_t sprite_idx, const char *label, double icon_h)
{
    int64_t spr_w = 0, spr_h = 0;
    FeSpriteTexture(sprite_idx, &spr_w, &spr_h);
    if (spr_w <= 0 || spr_h <= 0)
        return 0.0;
    FeStylePushFont(FeFont_Body);
    const double w = sprite_button_geom(spr_w, spr_h, label, icon_h).box.x;
    FeStylePopFont();
    return w;
}
