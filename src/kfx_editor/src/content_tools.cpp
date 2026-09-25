/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_tools.cpp
 *     The content tools host and the raw config editor window (plan 03 F3). See content_tools.h.
 * @par Comment:
 *     All logic is in content_raw.cpp / content_target.cpp (unit tested); this file is the ImGui
 *     layout and the open/close/unsaved-changes flow.
 */
#include "pre_inc.h"
#include "content_tools.h"
#include "content_raw.h"
#include "content_target.h"
#include "content_picker.h"
#include "content_rules.h"
#include "content_trapdoor.h"
#include "content_spells.h"
#include "content_creature.h"
#include "content_text.h"
#include "content_rooms.h"
#include "content_campaign.h"
#include "kfx_editor.h"
#include "frontgui_widgets.h"
#include "frontend.h"

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
#include <memory>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

enum PendingAction { Pending_None = 0, Pending_Open, Pending_Close };

struct HostState
{
    bool open = false;
    ContentPicker pick;
    int64_t file_idx = 0;
    // What is loaded
    std::vector<RawFileEntry> files;
    RawConfigSession session;
    bool loaded = false;
    int64_t loaded_layer = -1;
    std::string loaded_file;
    std::string loaded_target_key;
    // Editor widget and derived views
    std::unique_ptr<TextEditor> editor;
    size_t synced_undo = 0; // undo index at load / apply / revert
    size_t text_undo = 0;   // undo index the session's text was last copied at
    size_t seen_undo = 0;
    double last_change_time = 0;
    bool views_stale = true;
    std::vector<CfgDiagnostic> diags;
    std::vector<RawEffectiveRow> rows;
    bool only_layer = true;
    std::string status;
    bool delete_armed = false;
    PendingAction pending = Pending_None;
    bool want_modal = false;
};

HostState s_host;

ImVec4 layer_colour(CfgLayer l)
{
    switch (l)
    {
    case CfgLayer_Base: return ImVec4(0.62f, 0.62f, 0.66f, 1.0f);
    case CfgLayer_Campaign: return ImVec4(0.95f, 0.75f, 0.30f, 1.0f);
    default: return ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
    }
}

// --- .cfg colouring: ";" comments, "[blocks]", key names known to the schema.
TextEditor::Iterator cfg_tokenizer(TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color &color)
{
    if (start == end)
        return start;
    const ImWchar c = *start;
    if (c == ';')
    {
        color = TextEditor::Color::comment;
        return end;
    }
    if (c == '[')
    {
        TextEditor::Iterator it = start;
        ++it;
        while (it != end && *it != ']')
            ++it;
        if (it != end)
            ++it;
        color = TextEditor::Color::keyword;
        return it;
    }
    return start;
}

bool cfg_punctuation(ImWchar c)
{
    return c == '=';
}

void apply_syntax(const std::vector<std::string> &keys)
{
    static TextEditor::Language language;
    static bool initialised = false;
    if (!initialised)
    {
        language.name = "KeeperFX configuration";
        language.caseSensitive = false;
        language.customTokenizer = cfg_tokenizer;
        language.isPunctuation = cfg_punctuation;
        language.getIdentifier = TextEditor::Language::C()->getIdentifier;
        language.getNumber = TextEditor::Language::C()->getNumber;
        initialised = true;
    }
    language.identifiers.clear();
    for (const std::string &k : keys)
    {
        std::string l = k;
        for (char &ch : l)
            ch = (char)std::tolower((unsigned char)ch);
        language.identifiers.insert(l);
    }
    s_host.editor->SetLanguage(&language);
}

void ensure_editor()
{
    if (s_host.editor)
        return;
    s_host.editor.reset(new TextEditor());
    // See editor_script.cpp: the palette must be set explicitly, not left to static initialisation order.
    s_host.editor->SetPalette(TextEditor::GetDarkPalette());
}

void resolve_target()
{
    s_host.pick.resolve();
    s_host.files = content_raw_list_files(s_host.pick.target);
    if (s_host.file_idx >= (int64_t)s_host.files.size())
        s_host.file_idx = 0;
}

std::string target_key()
{
    return s_host.pick.signature();
}

void mark_views_stale()
{
    s_host.views_stale = true;
    s_host.last_change_time = ImGui::GetTime();
}

void load_selected()
{
    ensure_editor();
    s_host.delete_armed = false;
    s_host.status.clear();
    if (s_host.file_idx < 0 || (size_t)s_host.file_idx >= s_host.files.size())
    {
        s_host.loaded = false;
        return;
    }
    const RawFileEntry &f = s_host.files[(size_t)s_host.file_idx];
    CfgLayer layer = (CfgLayer)s_host.pick.layer_sel;
    if (!s_host.pick.layer_available(layer))
        layer = CfgLayer_Base;
    s_host.pick.layer_sel = layer;
    s_host.loaded = s_host.session.open(s_host.pick.target, f, layer);
    s_host.loaded_layer = layer;
    s_host.loaded_file = f.name;
    s_host.loaded_target_key = target_key();
    s_host.editor->SetText(s_host.session.text());
    s_host.editor->SetReadOnlyEnabled(!s_host.session.writable());
    apply_syntax(s_host.session.known_keys());
    s_host.synced_undo = s_host.seen_undo = s_host.text_undo = s_host.editor->GetUndoIndex();
    s_host.views_stale = true;
    s_host.last_change_time = 0;
    if (!s_host.loaded)
        s_host.status = "This target has no directory for that layer.";
    else if (!s_host.session.exists() && s_host.session.writable())
        s_host.status = "No file yet on this layer; Apply creates it.";
}

// Copies the editor's text into the session, only when it changed since the last copy.
void sync_text()
{
    const size_t undo = s_host.editor->GetUndoIndex();
    if (undo != s_host.text_undo)
    {
        s_host.session.text() = s_host.editor->GetText();
        s_host.text_undo = undo;
    }
}

void refresh_views()
{
    sync_text();
    s_host.diags = s_host.session.validate();
    s_host.rows = s_host.session.effective_rows(s_host.only_layer);
    s_host.views_stale = false;
}

bool selection_differs()
{
    if (!s_host.loaded)
        return true;
    if (s_host.file_idx < 0 || (size_t)s_host.file_idx >= s_host.files.size())
        return false;
    return s_host.files[(size_t)s_host.file_idx].name != s_host.loaded_file || s_host.pick.layer_sel != s_host.loaded_layer
        || target_key() != s_host.loaded_target_key;
}

bool session_dirty()
{
    if (!s_host.loaded || !s_host.editor)
        return false;
    sync_text();
    return s_host.session.dirty();
}

void request_open_selection()
{
    if (session_dirty())
    {
        s_host.pending = Pending_Open;
        s_host.want_modal = true;
    }
    else
        load_selected();
}

void request_close()
{
    if (session_dirty())
    {
        s_host.pending = Pending_Close;
        s_host.want_modal = true;
    }
    else
        s_host.open = false;
}

void draw_pending_modal()
{
    if (s_host.pending == Pending_None)
        return;
    if (s_host.want_modal)
    {
        FeOpenModal("##ContentDiscard");
        s_host.want_modal = false;
    }
    const bool open = FeBeginModal("##ContentDiscard");
    if (open)
    {
        FeHeading("Unsaved changes");
        FeBodyText("The text in the editor has changes that were not applied.");
        if (FeButton("Discard changes", ImVec2(180, 0)))
        {
            const PendingAction a = s_host.pending;
            s_host.pending = Pending_None;
            ImGui::CloseCurrentPopup();
            if (a == Pending_Open)
                load_selected();
            else
                s_host.open = false;
        }
        ImGui::SameLine();
        if (FeButton("Keep editing", ImVec2(180, 0)))
        {
            s_host.pending = Pending_None;
            ImGui::CloseCurrentPopup();
            // Put the picks back to what is loaded.
            resolve_target();
        }
    }
    FeEndModal(open);
}

void draw_pickers()
{
    if (s_host.pick.draw_target())
        resolve_target();
    s_host.pick.draw_layer();

    // File, on the layer row.
    ImGui::SameLine();
    FeCaption("File");
    ImGui::SameLine();
    std::vector<std::string> names;
    for (const RawFileEntry &f : s_host.files)
        names.push_back(f.name);
    if (names.empty())
        names.push_back("(no files)");
    std::vector<const char *> ptrs;
    for (const std::string &n : names)
        ptrs.push_back(n.c_str());
    int64_t fi = std::max<int64_t>(0, s_host.file_idx);
    ImGui::SetNextItemWidth(300);
    if (FeCombo("##filepick", &fi, ptrs.data(), (int64_t)ptrs.size()))
        s_host.file_idx = fi;
    ImGui::SameLine();
    const bool differs = selection_differs();
    ImGui::BeginDisabled(!differs);
    if (FeButton("Open", ImVec2(100, 0)))
        request_open_selection();
    ImGui::EndDisabled();
    if (!s_host.pick.note.empty())
        FeCaption(s_host.pick.note.c_str());
    if (s_host.loaded && !s_host.session.path().empty())
        FeCaption(s_host.session.path().c_str());
}

char s_filter_buf[128] = "";

bool contains_nocase(const std::string &hay, const char *needle)
{
    if (needle[0] == '\0')
        return true;
    std::string h = hay, n = needle;
    for (char &c : h)
        c = (char)std::tolower((unsigned char)c);
    for (char &c : n)
        c = (char)std::tolower((unsigned char)c);
    return h.find(n) != std::string::npos;
}

void draw_effective_pane(float height)
{
    if (FeCheckbox("Only keys in this layer", &s_host.only_layer))
        s_host.views_stale = true;
    FeCaption("Filter");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    FeTextInput("##effFilter", s_filter_buf, sizeof(s_filter_buf));
    if (ImGui::BeginTable("##EffRows", 4,
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
            ImVec2(0, std::max(60.0f, height - ImGui::GetFrameHeightWithSpacing() * 2 - ImGui::GetTextLineHeightWithSpacing()))))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Block", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableHeadersRow();
        for (const RawEffectiveRow &r : s_host.rows)
        {
            if (!contains_nocase(r.section, s_filter_buf) && !contains_nocase(r.key, s_filter_buf)
                && !contains_nocase(r.value, s_filter_buf))
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.section.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.key.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.value.c_str());
            if (r.has_beneath && ImGui::IsItemHovered())
                ImGui::SetTooltip("Replaces: %s", r.beneath.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(layer_colour(r.source), "%s", r.source == CfgLayer_Base ? "Base" : r.source == CfgLayer_Campaign ? "Campaign" : "Level");
        }
        ImGui::EndTable();
    }
}

void draw_problems_and_buttons()
{
    int64_t warnings = 0;
    for (const CfgDiagnostic &d : s_host.diags)
        if (d.severity >= CfgSev_Warning)
            warnings++;
    char head[160];
    snprintf(head, sizeof(head), "Problems: %" PRId64 " warning(s), %" PRId64 " note(s)", warnings, (int64_t)s_host.diags.size() - warnings);
    FeCaption(head);
    const bool list_open = FeBeginListBox("##Problems", ImVec2(-FLT_MIN, 84.0f));
    if (list_open)
    {
        for (size_t i = 0; i < s_host.diags.size(); i++)
        {
            const CfgDiagnostic &d = s_host.diags[i];
            const char *tag = d.severity >= CfgSev_Error ? "error" : d.severity == CfgSev_Warning ? "warning" : "note";
            char row[400];
            snprintf(row, sizeof(row), "line %" PRId64 "  %s: %s##p%zu", (int64_t)d.line, tag, d.message.c_str(), i);
            if (FeListRow(row, false) && d.line > 0)
            {
                s_host.editor->SelectLine((size_t)d.line - 1);
                s_host.editor->ScrollToLine((size_t)d.line - 1, TextEditor::Scroll::alignMiddle);
            }
        }
    }
    FeEndListBox(list_open);

    const bool can_write = s_host.loaded && s_host.session.writable();
    const bool dirty = session_dirty();
    ImGui::BeginDisabled(!(can_write && dirty));
    if (FeButton("Apply", ImVec2(110, 0)))
    {
        std::string err;
        if (s_host.session.apply(&err))
        {
            s_host.editor->SetText(s_host.session.text()); // the file is what is in the editor now
            s_host.synced_undo = s_host.seen_undo = s_host.text_undo = s_host.editor->GetUndoIndex();
            s_host.status = warnings > 0 ? "Applied with " + std::to_string(warnings) + " warning(s)." : "Applied.";
            s_host.views_stale = true;
        }
        else
            s_host.status = "Not written: " + err;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (FeButton("Revert", ImVec2(110, 0)))
    {
        s_host.session.revert();
        s_host.editor->SetText(s_host.session.text());
        s_host.synced_undo = s_host.seen_undo = s_host.text_undo = s_host.editor->GetUndoIndex();
        s_host.views_stale = true;
        s_host.status = "Reverted to the file on disk.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!(can_write && s_host.session.exists()));
    if (FeButton(s_host.delete_armed ? "Really delete?" : "Delete file", ImVec2(150, 0)))
    {
        if (!s_host.delete_armed)
            s_host.delete_armed = true;
        else
        {
            std::string err;
            s_host.status = s_host.session.delete_file(&err) ? "File deleted; the text stays in the editor." : "Not deleted: " + err;
            s_host.delete_armed = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (FeButton("Close", ImVec2(110, 0)))
        request_close();
    ImGui::SameLine();
    if (!s_host.status.empty())
        FeCaption(s_host.status.c_str());
    else if (s_host.loaded && !s_host.session.writable())
        FeCaption("Read only: base files are never edited.");
}

void draw_config_files_window()
{
    // Over the map (the map editor's host) the translucent style would let the world show through the text.
    content_ui_begin_window("##ContentConfigFiles", s_host.pick.map_host);

    FeHeading("Config Files");
    FeSeparator();
    draw_pickers();
    FeSeparator();

    // Text edits since the last look: refresh the derived views after a short pause in typing.
    if (s_host.loaded)
    {
        const size_t undo = s_host.editor->GetUndoIndex();
        if (undo != s_host.seen_undo)
        {
            s_host.seen_undo = undo;
            mark_views_stale();
            s_host.status.clear();
            s_host.delete_armed = false;
        }
        if (s_host.views_stale && ImGui::GetTime() - s_host.last_change_time > 0.35)
            refresh_views();
    }

    // Everything below the text and the table is fixed height (problems header, list, buttons/status), so the
    // body gets what is left and the button row can never scroll out of the window.
    const float problems_h = 84.0f;
    const float bottom = ImGui::GetTextLineHeightWithSpacing() + problems_h + ImGui::GetFrameHeightWithSpacing()
        + ImGui::GetStyle().ItemSpacing.y * 5 + ImGui::GetStyle().WindowPadding.y * 2;
    const float body_h = std::max(120.0f, ImGui::GetContentRegionAvail().y - bottom);
    if (s_host.loaded && ImGui::BeginTable("##cols", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("Effective", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableNextRow(0, body_h);
        ImGui::TableNextColumn();
        s_host.editor->Render("##ContentCfgText", ImVec2(0, body_h));
        ImGui::TableNextColumn();
        draw_effective_pane(body_h);
        ImGui::EndTable();
    }
    else if (!s_host.loaded)
    {
        FeBodyText("Pick a file and press Open.");
        ImGui::Dummy(ImVec2(0, body_h));
    }
    FeSeparator();
    draw_problems_and_buttons();
    draw_pending_modal();
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
    ImGui::End();
}

void select_file_by_name(const std::string &name)
{
    for (size_t i = 0; i < s_host.files.size(); i++)
        if (s_host.files[i].name == name)
        {
            s_host.file_idx = (int64_t)i;
            load_selected();
            return;
        }
}

void open_common(bool map_host)
{
    s_host.open = true;
    s_host.pending = Pending_None;
    // The narrowest layer that exists: a level in the map editor, else the campaign, else base.
    s_host.pick.open(map_host, map_host ? CfgLayer_Level : CfgLayer_Campaign);
    s_host.files = content_raw_list_files(s_host.pick.target);
    s_host.loaded = false;
    s_host.diags.clear();
    s_host.rows.clear();
    s_host.status.clear();
    // Start on the file most people want first.
    s_host.file_idx = 0;
    for (size_t i = 0; i < s_host.files.size(); i++)
        if (s_host.files[i].name == "rules.cfg")
            s_host.file_idx = (int64_t)i;
    load_selected();
}

} // namespace

/******************************************************************************/
TbBool content_tools_is_available(int tool)
{
    return tool == ContentTool_ConfigFiles || tool == ContentTool_Rules || tool == ContentTool_TrapDoor
        || tool == ContentTool_SpellAbility || tool == ContentTool_Creature
        || tool == ContentTool_Text || tool == ContentTool_Room || tool == ContentTool_Campaign;
}

void content_tools_open(int tool)
{
    if (tool == ContentTool_Rules)
        content_rules_open(false);
    else if (tool == ContentTool_TrapDoor)
        content_trapdoor_open(false);
    else if (tool == ContentTool_SpellAbility)
        content_spells_open(false);
    else if (tool == ContentTool_Creature)
        content_creature_open(false);
    else if (tool == ContentTool_Text)
        content_text_open(false);
    else if (tool == ContentTool_Room)
        content_rooms_open(false);
    else if (tool == ContentTool_Campaign)
        content_campaign_open(false);
    else if (tool == ContentTool_ConfigFiles)
        open_common(false);
}

void content_tools_open_file(int tool, const char *file)
{
    content_tools_open(tool);
    if (tool == ContentTool_ConfigFiles && file != nullptr)
        select_file_by_name(file);
}

void content_tools_open_for_map(int tool)
{
    if (tool == ContentTool_Rules)
        content_rules_open(true);
    else if (tool == ContentTool_TrapDoor)
        content_trapdoor_open(true);
    else if (tool == ContentTool_SpellAbility)
        content_spells_open(true);
    else if (tool == ContentTool_Creature)
        content_creature_open(true);
    else if (tool == ContentTool_Text)
        content_text_open(true);
    else if (tool == ContentTool_Room)
        content_rooms_open(true);
    else if (tool == ContentTool_Campaign)
        content_campaign_open(true);
    else if (tool == ContentTool_ConfigFiles)
        open_common(true);
}

void content_tools_frame(void)
{
    if (s_host.open)
        draw_config_files_window();
    content_rules_frame();
    content_trapdoor_frame();
    content_spells_frame();
    content_creature_frame();
    content_text_frame();
    content_rooms_frame();
    content_campaign_frame();
}

TbBool content_tools_is_open(void)
{
    return s_host.open || content_rules_is_open() || content_trapdoor_is_open() || content_spells_is_open() || content_creature_is_open() || content_text_is_open() || content_rooms_is_open() || content_campaign_is_open();
}
