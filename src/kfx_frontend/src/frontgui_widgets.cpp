#include "pre_inc.h"
#include "frontgui_widgets.h"
#include "frontgui_style.h"
#include "bflib_guibtns.h" // do_sound_menu_click -- menu hover/click sound feedback lives in the wrapper, not per-screen (§5.2)
#include "fe_noise.h"       // fe::fbm -- procedural marble list background
#include <imgui_internal.h> // GImGui->NavCursorVisible -- no public getter; the wrapper is the one place ImGui internals are allowed
#include <cmath>            // std::sin
#include "post_inc.h"

namespace {
    // Word-wrapped/uncoloured raw text, never routed through a printf-style
    // Text() call -- get_string() output can contain a literal '%' that
    // ImGui::Text()/TextWrapped() would otherwise try to parse as a format
    // specifier.
    void draw_wrapped_text(const char *text)
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
    }

    // The blood-red a text button's label flips to while hovered or
    // keyboard-focused -- the legacy frontend's highlighted-caption colour,
    // standing in for a filled button rectangle. Routed through GetColorU32
    // at draw time so BeginDisabled()'s global alpha dims it like any text.
    const ImVec4 kTextButtonHighlight(0.86f, 0.24f, 0.16f, 1.00f);

    // True when a text button should show its highlight colour: mouse hover
    // or press, or keyboard/gamepad focus *while the nav cursor is actually
    // visible*. ImGui auto-focuses the first item when a window appears, so
    // gating on IsItemFocused() alone lit the first menu button red at rest
    // (found live). NavCursorVisible is ImGui's own "show the focus
    // rectangle" state -- true after a nav keypress, hidden after a click or
    // mouse move -- and has no public getter, hence imgui_internal.h.
    bool fe_button_highlighted()
    {
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            return true;
        return ImGui::IsItemFocused() && GImGui->NavCursorVisible;
    }

    // Shared body for "a button that is only its label": no fill, no frame.
    // The label draws in the normal text colour and reddens on hover/focus.
    // `size` is honoured verbatim when either axis is > 0 (the main menu's
    // fixed 260px column, icon squares) and the label is centred in that box
    // -- a label wider than a fixed box overflows it evenly rather than
    // clipping, staying visually centred; a 0 axis sizes to the label plus
    // frame padding. Returns true on click; plays the menu click sound.
    bool fe_text_button(const char *label, const ImVec2 &size, FeFontRole font)
    {
        FeStylePushFont(font);
        const ImGuiStyle &style = ImGui::GetStyle();
        const ImVec2 text_sz = ImGui::CalcTextSize(label);
        const ImVec2 box(
            size.x > 0.0f ? size.x : text_sz.x + style.FramePadding.x * 2.0f,
            size.y > 0.0f ? size.y : text_sz.y + style.FramePadding.y * 2.0f);

        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        // EnableNav: InvisibleButton opts out of keyboard/tab navigation by
        // default (imgui.h) -- these menus are keyboard-navigable, opt back in.
        const bool pressed = ImGui::InvisibleButton(label, box, ImGuiButtonFlags_EnableNav);
        const bool hot = fe_button_highlighted();

        const ImU32 col = hot ? ImGui::GetColorU32(kTextButtonHighlight)
                              : ImGui::GetColorU32(ImGuiCol_Text);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(p0.x + (box.x - text_sz.x) * 0.5f, p0.y + (box.y - text_sz.y) * 0.5f),
            col, label);
        FeStylePopFont();

        if (pressed)
            do_sound_menu_click();
        return pressed;
    }

    // Two-tone inset bevel on an arbitrary screen-space rect: a light warm
    // line down the top+left inner edge, a dark line down the bottom+right --
    // the "embossed" raised look of the legacy message frame, done
    // procedurally rather than from the fixed-size frame sprites (which don't
    // scale with resolution). Drawn on the current window's draw list.
    void fe_inset_bevel(const ImVec2 &p_min, const ImVec2 &p_max)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImU32 hi = IM_COL32(220, 198, 156, 45);
        const ImU32 lo = IM_COL32(0, 0, 0, 90);
        const ImVec2 a(p_min.x + 1.0f, p_min.y + 1.0f);
        const ImVec2 b(p_max.x - 1.0f, p_max.y - 1.0f);
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, a.y), hi); // top
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(a.x, b.y), hi); // left
        dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y), lo); // bottom
        dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, b.y), lo); // right
    }

    void fe_window_inset_bevel()
    {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        fe_inset_bevel(wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
    }


    // Legacy selection lists sit on a dark, mottled parchment-shadow texture
    // rather than a flat fill. A firm dark base plus procedural marble
    // veining -- the same domain-warped sine the in-game panel uses
    // (frontgui_ingame_relief.cpp), this list's own colours. Deterministic,
    // so it's stable per frame and just re-lays on a resize. Drawn right
    // after BeginListBox, so it sits behind the rows.
    void fe_stipple_bg()
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 s = ImGui::GetWindowSize();
        const float x0 = p.x + 2.0f, y0 = p.y + 2.0f;
        const float x1 = p.x + s.x - 2.0f, y1 = p.y + s.y - 2.0f;
        if (x1 <= x0 || y1 <= y0)
            return;

        // Firm, mostly-opaque dark base -- the classic list panel is clearly
        // darker than the surrounding chrome, not a translucent wash.
        dl->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(14, 10, 7, 232));

        const float cell = 5.0f;
        const float ns = 0.013f, amp = 40.0f, freq = 0.030f;
        for (float y = y0; y < y1; y += cell)
            for (float x = x0; x < x1; x += cell)
            {
                const float px = x - p.x, py = y - p.y;
                const float warp = fe::fbm(px * ns, py * ns) - 0.4f;
                const float m = std::sin(freq * (px + py * 0.4f + amp * warp)); // -1..1
                const float t = 0.5f + 0.5f * m;
                const float lightv = t * t * t;
                const float darkv  = (1.0f - t) * (1.0f - t) * (1.0f - t);
                if (lightv < 0.09f && darkv < 0.09f)
                    continue;
                const ImU32 col = (lightv >= darkv)
                    ? IM_COL32(156, 126, 88, (int)(lightv * 46.0f)) // light vein
                    : IM_COL32(0, 0, 0, (int)(darkv * 54.0f));      // dark vein
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell + 0.5f, y + cell + 0.5f), col);
            }
    }
}

// ----------------------------------------------------------------------
// Panels
// ----------------------------------------------------------------------

bool FeBeginPanel(const char *title, const ImVec2 &size, bool scrollable)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.5f);
    // A 0 in either axis means "size to content" here, not BeginChild's own
    // default meaning of "fill remaining space in the parent" -- without
    // ImGuiChildFlags_AutoResizeX/Y a 0 height silently stretches the panel
    // to fill whatever space is left in its parent (found live: a settings
    // panel with only a few sliders in it stretching to cover most of the
    // window, leaving a large empty gap below its actual content).
    ImGuiChildFlags child_flags = ImGuiChildFlags_Borders;
    if (size.x == 0.0f) child_flags |= ImGuiChildFlags_AutoResizeX;
    if (size.y == 0.0f) child_flags |= ImGuiChildFlags_AutoResizeY;
    ImGuiWindowFlags win_flags = scrollable ? ImGuiWindowFlags_None
        : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    bool open = ImGui::BeginChild(title, size, child_flags, win_flags);
    if (open)
        fe_window_inset_bevel();
    if (open && title != nullptr && title[0] != '\0')
    {
        FeStylePushFont(FeFont_Subheading);
        ImGui::TextUnformatted(title);
        FeStylePopFont();

        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + ImGui::GetContentRegionAvail().x, p0.y);
        dl->AddLine(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), 2.0f);
        ImGui::Spacing();
        ImGui::Spacing();
    }
    return open;
}

void FeEndPanel()
{
    ImGui::EndChild(); // BeginChild/EndChild: always call End, per imgui.h's own note
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ----------------------------------------------------------------------
// Scroll areas
// ----------------------------------------------------------------------

bool FeBeginScrollArea(const char *label, const ImVec2 &size)
{
    FeStylePushFont(FeFont_Body);
    return ImGui::BeginChild(label, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
}

void FeEndScrollArea()
{
    ImGui::EndChild();
    FeStylePopFont();
}

// ----------------------------------------------------------------------
// List boxes
// ----------------------------------------------------------------------

bool FeBeginListBox(const char *label, const ImVec2 &size)
{
    FeStylePushFont(FeFont_Body);
    bool open = ImGui::BeginListBox(label, size);
    if (open)
    {
        fe_stipple_bg();
        fe_window_inset_bevel();
    }
    else
        FeStylePopFont(); // EndListBox() won't run this frame -- pop now instead
    return open;
}

void FeEndListBox(bool was_open)
{
    if (!was_open)
        return;
    ImGui::EndListBox();
    FeStylePopFont();
}

bool FeListRow(const char *label, bool selected)
{
    bool clicked = ImGui::Selectable(label, selected);
    if (clicked)
        do_sound_menu_click();
    return clicked;
}

void FeCenterNextItem(float item_width)
{
    float avail = ImGui::GetContentRegionAvail().x;
    if (item_width >= avail)
        return;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - item_width) * 0.5f);
}

// ----------------------------------------------------------------------
// Buttons
// ----------------------------------------------------------------------

bool FeButton(const char *label, const ImVec2 &size)
{
    return fe_text_button(label, size, FeFont_Body);
}

bool FeIconButton(const char *icon_label, float size)
{
    FeStylePushFont(FeFont_Subheading);
    const float sz = size > 0.0f ? size : ImGui::GetFontSize() * 1.6f;
    FeStylePopFont();
    return fe_text_button(icon_label, ImVec2(sz, sz), FeFont_Subheading);
}

bool FeNavButton(const char *label, bool selected)
{
    FeStylePushFont(FeFont_Subheading);
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 text_sz = ImGui::CalcTextSize(label);
    const float pad_x = style.FramePadding.x * 1.5f;
    const float pad_y = style.FramePadding.y * 1.5f;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail < 1.0f) avail = 1.0f;
    const ImVec2 box(avail, text_sz.y + pad_y * 2.0f);

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, box, ImGuiButtonFlags_EnableNav);
    const bool hot = selected || fe_button_highlighted();
    const ImU32 col = hot ? ImGui::GetColorU32(kTextButtonHighlight)
                          : ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + pad_x, p0.y + pad_y), col, label);
    FeStylePopFont();

    if (pressed)
        do_sound_menu_click();
    return pressed;
}

// ----------------------------------------------------------------------
// Settings controls
// ----------------------------------------------------------------------

bool FeSlider(const char *label, float *v, float v_min, float v_max, const char *fmt)
{
    FeStylePushFont(FeFont_Body);
    bool changed = ImGui::SliderFloat(label, v, v_min, v_max, fmt);
    FeStylePopFont();
    return changed;
}

bool FeCheckbox(const char *label, bool *v)
{
    FeStylePushFont(FeFont_Body);
    bool changed = ImGui::Checkbox(label, v);
    FeStylePopFont();
    if (changed)
        do_sound_menu_click();
    return changed;
}

bool FeCombo(const char *label, int *current_item, const char *const items[], int items_count)
{
    FeStylePushFont(FeFont_Body);
    bool changed = ImGui::Combo(label, current_item, items, items_count);
    FeStylePopFont();
    if (changed)
        do_sound_menu_click();
    return changed;
}

bool FeTextInput(const char *label, char *buf, size_t buf_size)
{
    FeStylePushFont(FeFont_Body);
    bool changed = ImGui::InputText(label, buf, buf_size);
    FeStylePopFont();
    return changed;
}

bool FeKeybindRow(const char *action_label, const char *key_label, bool capturing)
{
    FeStylePushFont(FeFont_Body);
    ImGui::TextUnformatted(action_label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x > 220.0f ? 220.0f : 0.0f);

    FeStylePopFont();

    ImGui::PushID(action_label);
    bool clicked;
    if (capturing)
    {
        // Force the highlight colour on regardless of hover: fe_text_button's
        // idle branch reads ImGuiCol_Text, so pushing it there lights the
        // "Press a key..." label the same red a hovered row shows.
        ImGui::PushStyleColor(ImGuiCol_Text, kTextButtonHighlight);
        clicked = fe_text_button("Press a key...", ImVec2(0, 0), FeFont_Body);
        ImGui::PopStyleColor();
    }
    else
    {
        clicked = fe_text_button(key_label, ImVec2(0, 0), FeFont_Body);
    }
    ImGui::PopID();

    return clicked;
}

// ----------------------------------------------------------------------
// Type scale
// ----------------------------------------------------------------------

void FeHeading(const char *text)
{
    FeStylePushFont(FeFont_Heading);
    ImGui::TextUnformatted(text);
    FeStylePopFont();
}

void FeSubheading(const char *text)
{
    FeStylePushFont(FeFont_Subheading);
    ImGui::TextUnformatted(text);
    FeStylePopFont();
}

void FeBodyText(const char *text)
{
    FeStylePushFont(FeFont_Body);
    draw_wrapped_text(text);
    FeStylePopFont();
}

void FeCaption(const char *text)
{
    FeStylePushFont(FeFont_Caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    FeStylePopFont();
}

void FeSeparator()
{
    ImGui::Separator(); // ImGuiCol_Separator is already the bronze accent (frontgui_style.cpp)
}

void FeHelpTooltip(const char *text)
{
    if ((text == nullptr) || (text[0] == '\0'))
        return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        return;
    FeStylePushFont(FeFont_Caption);
    ImGui::SetTooltip("%s", text);
    FeStylePopFont();
}

// ----------------------------------------------------------------------
// Tabs
// ----------------------------------------------------------------------

bool FeBeginTabBar(const char *label)
{
    FeStylePushFont(FeFont_Subheading);
    bool open = ImGui::BeginTabBar(label, ImGuiTabBarFlags_None);
    if (!open)
        FeStylePopFont();
    return open;
}

void FeEndTabBar(bool was_open)
{
    if (!was_open)
        return;
    ImGui::EndTabBar();
    FeStylePopFont();
}

bool FeTab(const char *label)
{
    bool open = ImGui::BeginTabItem(label);
    if (open && ImGui::IsItemActivated())
        do_sound_menu_click();
    return open;
}

void FeEndTab()
{
    ImGui::EndTabItem();
}

// ----------------------------------------------------------------------
// Menu bar
// ----------------------------------------------------------------------

// docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- BeginMainMenuBar
// (not a plain child window with ImGuiWindowFlags_MenuBar) since the editor's
// bar is meant to sit full-width at the very top of the screen with no
// enclosing FeBeginPanel/Begin of its own; this is the one wrapper in this
// file that doesn't need a parent Begin() at all.
bool FeBeginMenuBar()
{
    FeStylePushFont(FeFont_Body);
    bool open = ImGui::BeginMainMenuBar();
    if (!open)
        FeStylePopFont();
    return open;
}

void FeEndMenuBar(bool was_open)
{
    if (!was_open)
        return;
    ImGui::EndMainMenuBar();
    FeStylePopFont();
}

bool FeBeginMenu(const char *label, bool enabled)
{
    return ImGui::BeginMenu(label, enabled);
}

void FeEndMenu(bool was_open)
{
    if (!was_open)
        return;
    ImGui::EndMenu();
}

bool FeMenuItem(const char *label, const char *shortcut, bool enabled)
{
    bool clicked = ImGui::MenuItem(label, shortcut, false, enabled);
    if (clicked)
        do_sound_menu_click();
    return clicked;
}

// ----------------------------------------------------------------------
// Modals
// ----------------------------------------------------------------------

void FeOpenModal(const char *name)
{
    ImGui::OpenPopup(name);
}

bool FeBeginModal(const char *name)
{
    FeStylePushFont(FeFont_Body);
    bool open = ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    if (!open)
        FeStylePopFont();
    return open;
}

void FeEndModal(bool was_open)
{
    if (!was_open)
        return;
    fe_window_inset_bevel(); // still inside the popup window here
    ImGui::EndPopup();
    FeStylePopFont();
}
