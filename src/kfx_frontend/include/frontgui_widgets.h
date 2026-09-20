#ifndef FRONTGUI_WIDGETS_H
#define FRONTGUI_WIDGETS_H

// The KeeperFX ImGui widget wrapper layer
// (docs/refactor/renderer/04-imgui-gui-foundation.md §5.2). Hard rule from
// the plan: "Every panel, list, scroll region, button, slider and text
// style gets a KeeperFX wrapper, and screen code calls only wrappers" --
// no migrated screen (from Phase C onward) should call ImGui:: directly
// for anything covered here. imgui.h is included here and in screen code,
// nowhere else in kfx_frontend.
//
// Two Begin/End pairing conventions, matching the two conventions ImGui
// itself uses (imgui.h documents the split at BeginChild/EndChild's
// comment): FeBeginPanel/FeBeginScrollArea wrap BeginChild, so their End
// must always be called regardless of the Begin return value. Every other
// Begin here wraps ImGui's "only call End if Begin returned true" family
// (BeginListBox, BeginTabBar, BeginTabItem, BeginPopupModal) -- to keep
// the call contract uniform rather than caller-remembered, those FeEndXxx
// take the bool FeBeginXxx returned, and internally decide whether the
// real ImGui::EndXxx() is safe to call. Screen code always writes the
// pair unconditionally:
//
//   bool open = FeBeginListBox("##keys", size);
//   if (open) { for (...) FeListRow(...); }
//   FeEndListBox(open);
//
// Style/font push and pop is always owned by the wrapper (never left to
// the caller), and always balanced regardless of which branch above ran.

#include <imgui.h>

// --- Panels: bordered content panel, ornament corners, title bar. The
// successor to frontend_draw_scroll_box (§1.2/§5.1) -- procedural for now
// (translucent fill + bronze border + title strip), the 9-slice chrome
// import is follow-up work, not blocking this layer's shape. Always call
// FeEndPanel() after FeBeginPanel(), regardless of its return value
// (BeginChild/EndChild contract).
//
// `scrollable` defaults to false, matching every existing caller's
// expectation of a static, non-scrolling piece of chrome: with it false,
// a mouse wheel over the panel is deliberately not captured here (found
// live: content that overflows a fixed-height panel just clips instead of
// growing it). Pass true for a panel whose content can genuinely run long
// (e.g. a level/campaign description) -- it then gets its own scrollbar
// and captures its own mouse wheel instead of overflowing invisibly or
// (worse) leaking the wheel event to whatever window contains it.
bool FeBeginPanel(const char *title, const ImVec2 &size = ImVec2(0, 0), bool scrollable = false);
void FeEndPanel();

// --- Scroll areas: scrolling text/content with the styled scrollbar.
// Same always-call-End contract as FeBeginPanel (BeginChild-backed).
bool FeBeginScrollArea(const char *label, const ImVec2 &size = ImVec2(0, 0));
void FeEndScrollArea();

// --- List boxes: scrollable selection lists (the FrontendSelectList
// screens, the key-remap rows). FeEndListBox(open) takes the bool
// FeBeginListBox returned -- see the file header note above.
bool FeBeginListBox(const char *label, const ImVec2 &size = ImVec2(0, 0));
void FeEndListBox(bool was_open);
bool FeListRow(const char *label, bool selected);

// Nudges the cursor so the next item, given its own width, is horizontally
// centred in whatever space remains in the current line (GetContentRegionAvail()).
// A no-op (cursor untouched) if item_width is already >= the available space.
// Found live: a fixed pixel width tuned for the default UI_FONT_SCALE (e.g.
// the main menu's own 260px buttons) stops matching the window's own
// AlwaysAutoResize width once a *different* piece of content on the same
// window -- FeHeading()'s own text -- grows or shrinks with font scale
// while the fixed-width item doesn't, drifting the two out of alignment
// ("menu items don't stay centered" when changing UI_FONT_SCALE). Call this
// right before drawing any item whose width doesn't already track the
// window's own current size.
void FeCenterNextItem(float item_width);

// --- Buttons: the large/small menu button families (gui_frontbtns.c).
bool FeButton(const char *label, const ImVec2 &size = ImVec2(0, 0));
bool FeIconButton(const char *icon_label, float size = 0.0f);
// Full-width, left-aligned nav entry (main-menu-style vertical stacks).
bool FeNavButton(const char *label, bool selected = false);

// --- Settings controls; the schema renderer (§6.3) is built entirely
// from these.
bool FeSlider(const char *label, float *v, float v_min, float v_max, const char *fmt = "%.0f");
bool FeCheckbox(const char *label, bool *v);
bool FeCombo(const char *label, int *current_item, const char *const items[], int items_count);
bool FeTextInput(const char *label, char *buf, size_t buf_size);
// Integer entry with -/+ buttons (`step`, `step_fast` with Ctrl). The value is clamped to
// [v_min, v_max] after any edit. Returns true when the value changed.
bool FeInputInt(const char *label, int *v, int step, int step_fast, int v_min, int v_max);
// A collapsible section header (the Skirmish setup tab's General/Availability/...). Returns true
// while the section is open -- draw its content only then; there is no End call.
bool FeCollapsingHeader(const char *label, bool default_open = false);
// capturing: caller-owned "waiting for the next keypress" state; the row
// shows "Press a key..." instead of key_label while true. Returns true on
// the frame its button is clicked (caller flips its own capturing flag).
bool FeKeybindRow(const char *action_label, const char *key_label, bool capturing);

// --- Type scale: font-role choices live in frontgui_style.{h,cpp}, only
// referenced here. text/body/caption arguments are never treated as
// printf format strings (arbitrary get_string() content flows through
// these) -- always TextUnformatted internally, never Text()/TextWrapped().
void FeHeading(const char *text);
void FeSubheading(const char *text);
void FeBodyText(const char *text);
void FeCaption(const char *text);
void FeSeparator();

// Shows `text` as a hover tooltip for whichever widget was drawn
// immediately before this call, if `text` is non-null/non-empty and that
// widget is currently hovered. A no-op otherwise (including a short delay
// before the tooltip appears, matching normal ImGui hover behaviour) --
// safe to call unconditionally after every widget, not just ones that
// happen to have help text. Phase G §6.3's per-option help string
// (label_stridx's sibling, config_settingschema.h) is the first user.
void FeHelpTooltip(const char *text);

// --- Tabs: the settings screen's Game/Graphics/Sound/Input tabs (§10).
// FeEndTabBar(open) mirrors FeEndListBox's contract; FeTab/FeEndTab mirror
// BeginTabItem/EndTabItem's own "only End if Begin returned true" pairing
// (call FeEndTab() only when FeTab() returned true).
bool FeBeginTabBar(const char *label);
void FeEndTabBar(bool was_open);
bool FeTab(const char *label);
// Same, but `select` forces this tab to become the selected one this frame
// (ImGuiTabItemFlags_SetSelected) -- for code that changes what is active
// (e.g. the editor's eyedropper) and wants the tab UI to follow.
bool FeTabEx(const char *label, bool select);
void FeEndTab();

// --- Modals: error box, add-session box, "press a key".
void FeOpenModal(const char *name);
bool FeBeginModal(const char *name);
// Same, but the user can resize it (drag the corner) -- for dialogs whose
// content can outgrow an auto-sized box at larger font sizes. `size` is the
// first-open size; the popup remembers what the user chose while it lives.
bool FeBeginModalResizable(const char *name, const ImVec2 &size, const ImVec2 &min_size);
void FeEndModal(bool was_open);

// --- Menu bar: a top-anchored pull-down bar (kfx_editor's File/Edit/View/
// Script bar). Same "only call End if Begin returned true" contract as the
// tab family above -- FeEndMenuBar(was_open)/FeEndMenu(was_open) mirror
// FeEndTabBar/FeEndTab.
bool FeBeginMenuBar();
void FeEndMenuBar(bool was_open);
bool FeBeginMenu(const char *label, bool enabled = true);
void FeEndMenu(bool was_open);
// Returns true the frame it's clicked. `shortcut` is display-only text
// (e.g. "Ctrl+S") shown right-aligned -- callers still own their own
// keybinding handling, this doesn't register one.
bool FeMenuItem(const char *label, const char *shortcut = nullptr, bool enabled = true);

#endif // FRONTGUI_WIDGETS_H
