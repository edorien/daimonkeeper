/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_text.cpp
 *     The Text editor window. See content_text.h.
 */
#include "pre_inc.h"
#include "content_text.h"
#include "content_picker.h"
#include "content_strings.h"
#include "content_form.h"
#include "frontgui_widgets.h"

#include <imgui.h>
// See editor_script.cpp for why the pragma wraps this include.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <TextEditor.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

struct Row
{
    size_t id = 0;
    std::string line;    // the effective text on one line, for the table
    CfgLayer source = CfgLayer_Base;
    bool found = false;
    bool own = false;    // this layer has it
    std::string used_by; // "map00300 name, map00300 script"
};

struct TextState
{
    bool open = false;
    ContentPicker pick;
    StringsSession session;
    std::vector<std::string> languages;
    int64_t lang_idx = 0;
    StringsPaths paths;
    std::map<int64_t, std::vector<std::string>> usage;
    bool loaded = false;
    std::string loaded_signature;
    std::string status;
    std::vector<Row> rows;
    bool rows_stale = true;
    int64_t selected = -1;
    char filter[128] = "";
    bool only_own = false;
    bool only_used = false;
    bool only_missing = false;
    bool wrap = true;
    std::unique_ptr<TextEditor> editor;
    size_t editor_undo = 0;
    int64_t editor_id = -1;   // the id the edit box shows
    std::string force_lang;
    int force_id = -1;
};

TextState s_tx;

std::string base_root() { return content_root(); }

const ContentCampaign *campaign_or_null()
{
    return s_tx.pick.map_host ? nullptr : s_tx.pick.campaign();
}

StringsPaths current_paths()
{
    const std::string lang = s_tx.languages.empty() ? "eng" : s_tx.languages[(size_t)std::max<int64_t>(0, s_tx.lang_idx)];
    return content_strings_paths(base_root(), campaign_or_null(), s_tx.pick.target.level_dir, s_tx.pick.target.level_number, lang);
}

std::string one_line(const std::string &text)
{
    std::string out;
    for (char c : text)
        out += (c == '\n') ? std::string(" / ") : std::string(1, c);
    if (out.size() > 160)
        out = out.substr(0, 157) + "...";
    return out;
}

void ensure_editor()
{
    if (s_tx.editor)
        return;
    s_tx.editor.reset(new TextEditor());
    s_tx.editor->SetPalette(TextEditor::GetDarkPalette()); // see editor_script.cpp: set explicitly
}

void show_in_editor(int64_t id)
{
    ensure_editor();
    s_tx.editor_id = id;
    s_tx.editor->SetText(id >= 0 ? (s_tx.session.writable() ? s_tx.session.own_text((size_t)id) : s_tx.session.view((size_t)id).text) : std::string());
    s_tx.editor->SetReadOnlyEnabled(!s_tx.session.writable());
    s_tx.editor->SetWordWrapEnabled(s_tx.wrap);
    s_tx.editor_undo = s_tx.editor->GetUndoIndex();
}

void rebuild_rows()
{
    s_tx.rows.clear();
    s_tx.rows_stale = false;
    if (!s_tx.session.is_open())
        return;
    const size_t count = std::min(s_tx.session.id_count(), kStringsMax + 1);
    std::string needle = s_tx.filter;
    for (char &c : needle)
        c = (char)std::tolower((unsigned char)c);
    for (size_t id = 0; id < count; id++)
    {
        const StringsSession::View v = s_tx.session.view(id);
        const auto used = s_tx.usage.find((int64_t)id);
        const bool is_used = used != s_tx.usage.end();
        // What is listed: ids with text, ids the campaign uses, and the one being edited.
        if (!v.found && !is_used && (int64_t)id != s_tx.selected)
            continue;
        if (s_tx.only_own && !v.overridden_here)
            continue;
        if (s_tx.only_used && !is_used)
            continue;
        if (s_tx.only_missing && !(is_used && !v.found))
            continue;
        Row r;
        r.id = id;
        r.line = one_line(v.text);
        r.source = v.source;
        r.found = v.found;
        r.own = v.overridden_here;
        if (is_used)
            for (const std::string &u : used->second)
                r.used_by += (r.used_by.empty() ? "" : ", ") + u;
        if (!needle.empty())
        {
            std::string hay = std::to_string(id) + " " + r.line + " " + r.used_by;
            for (char &c : hay)
                c = (char)std::tolower((unsigned char)c);
            if (hay.find(needle) == std::string::npos)
                continue;
        }
        s_tx.rows.push_back(std::move(r));
    }
}

void load()
{
    s_tx.status.clear();
    s_tx.languages = content_strings_languages(base_root(), campaign_or_null());
    if (s_tx.languages.empty())
        s_tx.languages.push_back("eng");
    if (!s_tx.force_lang.empty())
    {
        for (size_t i = 0; i < s_tx.languages.size(); i++)
            if (s_tx.languages[i] == s_tx.force_lang)
                s_tx.lang_idx = (int64_t)i;
        s_tx.force_lang.clear();
    }
    if (s_tx.lang_idx >= (int64_t)s_tx.languages.size())
        s_tx.lang_idx = 0;
    s_tx.paths = current_paths();
    s_tx.pick.layer_filter = [](CfgLayer l) {
        const StringsPaths p = current_paths();
        return l == CfgLayer_Campaign ? !p.campaign.empty() : !p.level.empty();
    };
    s_tx.session.open(s_tx.paths, s_tx.languages[(size_t)s_tx.lang_idx], (CfgLayer)s_tx.pick.layer_sel);
    // Where each id is used: the campaign's level names and the scripts of its levels (or the one map being edited).
    std::vector<int64_t> levels;
    std::string levels_dir;
    if (const ContentCampaign *c = campaign_or_null())
    {
        levels = c->levels;
        levels_dir = c->levels_dir;
    }
    else if (s_tx.pick.target.level_number >= 0)
    {
        levels.push_back(s_tx.pick.target.level_number);
        levels_dir = s_tx.pick.target.level_dir;
    }
    s_tx.usage = content_strings_usage(campaign_or_null(), levels_dir, levels);
    s_tx.loaded = true;
    s_tx.loaded_signature = s_tx.pick.signature() + "|" + s_tx.languages[(size_t)s_tx.lang_idx];
    s_tx.rows_stale = true;
    if (s_tx.force_id >= 0)
    {
        s_tx.selected = s_tx.force_id;
        s_tx.force_id = -1;
    }
    show_in_editor(s_tx.selected);
    if (!s_tx.session.writable())
        s_tx.status = s_tx.session.why_read_only();
}

void draw_list(float height)
{
    if (s_tx.rows_stale)
        rebuild_rows();
    if (!ImGui::BeginTable("##strings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings, ImVec2(0, height)))
        return;
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, form_col(4));
    ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, form_col(6));
    ImGui::TableSetupColumn("Used by", ImGuiTableColumnFlags_WidthFixed, form_col(16));
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin((int)s_tx.rows.size());
    while (clipper.Step())
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        {
            const Row &r = s_tx.rows[(size_t)i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID((int)r.id);
            if (ImGui::Selectable(std::to_string(r.id).c_str(), (int64_t)r.id == s_tx.selected, ImGuiSelectableFlags_SpanAllColumns)
                && (int64_t)r.id != s_tx.selected)
            {
                s_tx.selected = (int64_t)r.id;
                show_in_editor(s_tx.selected);
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            if (r.found)
                ImGui::TextUnformatted(r.line.c_str());
            else
                ImGui::TextDisabled("(empty)");
            ImGui::TableNextColumn();
            if (r.found)
                ImGui::TextColored(form_layer_colour(r.source), "%s", form_layer_name(r.source));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.used_by.c_str());
        }
    ImGui::EndTable();
}

void draw_edit_pane(float height)
{
    if (s_tx.selected < 0)
    {
        FeBodyText("Pick a string in the list, or press Add string.");
        ImGui::Dummy(ImVec2(0, height));
        return;
    }
    const size_t id = (size_t)s_tx.selected;
    const StringsSession::View v = s_tx.session.view(id);
    char head[200];
    snprintf(head, sizeof(head), "String %zu", id);
    FeSubheading(head);
    if (s_tx.session.writable())
    {
        if (s_tx.session.own_text(id).empty() && v.found)
        {
            ImGui::TextDisabled("This layer has no text for it: it shows the %s string.", form_layer_name(v.source));
            ImGui::TextUnformatted(v.text.c_str());
        }
        else if (v.pending)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "edited%s%s", v.has_beneath ? "  (replaces: " : "", v.has_beneath ? (one_line(v.beneath) + ")").c_str() : "");
        else
            ImGui::TextColored(form_layer_colour(v.source), "%s", v.found ? form_layer_name(v.source) : "empty");
    }
    const float edit_h = std::max(60.0f, height - ImGui::GetFrameHeightWithSpacing() * 4.5f);
    ensure_editor();
    s_tx.editor->Render("##textedit", ImVec2(0, edit_h));
    if (s_tx.session.writable() && s_tx.editor->GetUndoIndex() != s_tx.editor_undo)
    {
        s_tx.editor_undo = s_tx.editor->GetUndoIndex();
        s_tx.session.set_text(id, s_tx.editor->GetText());
        s_tx.rows_stale = true;
    }
    if (FeCheckbox("Word wrap (view only)", &s_tx.wrap))
        s_tx.editor->SetWordWrapEnabled(s_tx.wrap);
    ImGui::SameLine();
    ImGui::BeginDisabled(!s_tx.session.writable() || s_tx.session.own_text(id).empty());
    if (FeButton("Reset string", ImVec2(form_col(9), 0)))
    {
        s_tx.session.reset(id);
        show_in_editor(s_tx.selected);
        s_tx.rows_stale = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    FeCaption("Enter starts a new line in the text itself; wrapping is only how it is shown here.");
}

void draw_window()
{
    content_ui_begin_window("##ContentText", s_tx.pick.map_host);
    FeHeading("Text Editor");
    FeSeparator();

    const bool dirty = s_tx.session.dirty();
    ImGui::BeginDisabled(dirty);
    const bool target_changed = s_tx.pick.draw_target();
    ImGui::SameLine();
    const bool layer_changed = s_tx.pick.draw_layer();
    ImGui::SameLine();
    FeCaption("Language");
    ImGui::SameLine();
    std::vector<const char *> lang_ptrs;
    for (const std::string &l : s_tx.languages)
        lang_ptrs.push_back(l.c_str());
    int64_t li = s_tx.lang_idx;
    bool lang_changed = false;
    ImGui::SetNextItemWidth(form_col(7));
    if (!lang_ptrs.empty() && FeCombo("##textlang", &li, lang_ptrs.data(), (int64_t)lang_ptrs.size()) && li != s_tx.lang_idx)
    {
        s_tx.lang_idx = li;
        lang_changed = true;
    }
    ImGui::EndDisabled();
    if (dirty)
        FeCaption("Apply or Revert the changes before switching target, layer or language.");
    else if (!s_tx.pick.note.empty())
        FeCaption(s_tx.pick.note.c_str());
    if (target_changed || layer_changed || lang_changed || !s_tx.loaded
        || s_tx.pick.signature() + "|" + (s_tx.languages.empty() ? "" : s_tx.languages[(size_t)s_tx.lang_idx]) != s_tx.loaded_signature)
        if (!dirty)
        {
            if (target_changed)
                s_tx.selected = -1;
            load();
        }
    FeSeparator();

    // Filters.
    FeCaption("Filter");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(form_col(16));
    if (FeTextInput("##strfilter", s_tx.filter, sizeof(s_tx.filter)))
        s_tx.rows_stale = true;
    ImGui::SameLine();
    if (FeCheckbox("This layer's strings", &s_tx.only_own))
        s_tx.rows_stale = true;
    ImGui::SameLine();
    if (FeCheckbox("In use", &s_tx.only_used))
        s_tx.rows_stale = true;
    ImGui::SameLine();
    if (FeCheckbox("Used but empty", &s_tx.only_missing))
        s_tx.rows_stale = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!s_tx.session.writable());
    if (FeButton("Add string", ImVec2(form_col(9), 0)))
    {
        const size_t id = s_tx.session.next_free_id();
        if (id < kStringsMax)
        {
            s_tx.selected = (int64_t)id;
            show_in_editor(s_tx.selected);
            s_tx.rows_stale = true;
        }
        else
            s_tx.status = "All the ids the game reads are taken.";
    }
    ImGui::EndDisabled();

    const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing()
        + ImGui::GetStyle().ItemSpacing.y * 4 + ImGui::GetStyle().WindowPadding.y * 2;
    const float body_h = std::max(200.0f, ImGui::GetContentRegionAvail().y - bottom);
    if (s_tx.loaded)
    {
        draw_list(body_h * 0.5f);
        FeSeparator();
        draw_edit_pane(body_h * 0.5f - ImGui::GetStyle().ItemSpacing.y * 3);
    }
    else
        ImGui::Dummy(ImVec2(0, body_h));

    FeSeparator();
    const bool writable = s_tx.session.writable();
    const std::vector<StringsSession::Problem> problems = s_tx.session.diagnostics();
    ImGui::BeginDisabled(!(writable && s_tx.session.dirty()));
    if (FeButton("Apply", ImVec2(110, 0)))
    {
        std::string err;
        if (s_tx.session.apply(&err))
        {
            s_tx.status = "Applied.";
            show_in_editor(s_tx.selected);
            s_tx.rows_stale = true;
        }
        else
            s_tx.status = "Not written: " + err;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!s_tx.session.dirty());
    if (FeButton("Revert", ImVec2(110, 0)))
    {
        s_tx.session.discard();
        show_in_editor(s_tx.selected);
        s_tx.rows_stale = true;
        s_tx.status = "Changes discarded.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (FeButton("Close", ImVec2(110, 0)))
    {
        if (s_tx.session.dirty())
            s_tx.status = "Apply or Revert the changes first.";
        else
            s_tx.open = false;
    }
    ImGui::SameLine();
    if (!problems.empty())
        FeCaption(problems[0].message.c_str());
    else if (!s_tx.status.empty())
        FeCaption(s_tx.status.c_str());
    else if (s_tx.session.dirty())
    {
        char info[100];
        snprintf(info, sizeof(info), "%zu string(s) changed, not applied", s_tx.session.pending_count());
        FeCaption(info);
    }
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
    ImGui::End();
}

} // namespace

void content_text_open(bool map_host)
{
    s_tx.open = true;
    s_tx.pick.open(map_host, map_host ? CfgLayer_Level : CfgLayer_Campaign);
    s_tx.session.discard();
    s_tx.loaded = false;
    s_tx.selected = -1;
    s_tx.status.clear();
    s_tx.languages.clear();
    s_tx.lang_idx = 0;
    load();
}

void content_text_frame(void)
{
    if (s_tx.open)
        draw_window();
}

bool content_text_is_open(void)
{
    return s_tx.open;
}

void content_text_show(const char *lang, int id)
{
    s_tx.force_lang = lang != nullptr ? lang : "";
    s_tx.force_id = id;
    if (!s_tx.session.dirty())
        s_tx.loaded = false; // reload with the language and id applied
}
