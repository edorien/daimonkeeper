/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_rules.cpp
 *     The Rules editor window. See content_rules.h.
 * @par Comment:
 *     A form generated from the schema (rules.cfg's blocks and keys), with the help text lifted from the
 *     base file's own comments and the key spelling of the base file. Each row shows the value, where it
 *     comes from (Base / Campaign / Level / default), whether it is changed here, and a Reset. Nothing is
 *     written until Apply; equal-to-inherited edits vanish (plan 03 §7 rule 1).
 */
#include "pre_inc.h"
#include "content_rules.h"
#include "content_picker.h"
#include "content_struct.h"
#include "cfgc_help.h"
#include "content_names.h"
#include "content_form.h"
#include "frontgui_widgets.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

struct RulesState
{
    bool open = false;
    ContentPicker pick;
    StructuredSession session;
    bool loaded = false;
    std::string loaded_signature;
    std::string status;
    FormContext form; // the shared form row (help and key spelling come from the base rules.cfg)
    bool base_read = false;
    std::string force_tab; // tab to bring to the front on the next frame (block name, lower case)
};

RulesState s_rules;

// The groups in the order rules.cfg has its blocks; then the list blocks.
const char *const kValueGroups[] = {"game", "creatures", "rooms", "magic", "computer", "workers", "health"};
const char *const kListGroups[] = {"research", "sacrifices"};

std::string title_case(const std::string &s)
{
    std::string out = s;
    if (!out.empty())
        out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

// Thin aliases onto the shared form helpers.
std::string lower(std::string s) { return form_lower(std::move(s)); }
ImVec4 layer_colour(CfgLayer l) { return form_layer_colour(l); }
const char *layer_name(CfgLayer l) { return form_layer_name(l); }
bool parse_int(const std::string &s, int64_t &out) { return form_parse_int(s, out); }
float col(float em) { return form_col(em); }

void load()
{
    s_rules.status.clear();
    s_rules.loaded = s_rules.session.open(s_rules.pick.target, "rules", "rules.cfg", (CfgLayer)s_rules.pick.layer_sel);
    s_rules.loaded_signature = s_rules.pick.signature();
    s_rules.form.session = &s_rules.session;
    if (!s_rules.base_read)
    {
        s_rules.form.read_reference(s_rules.pick.target.path_for("rules.cfg", CfgLayer_Base));
        s_rules.base_read = true;
    }
    if (!s_rules.loaded)
        s_rules.status = "This target has no rules.cfg location for that layer.";
}

void draw_value_group(const std::string &section)
{
    const CfgSectionSpec *sec = s_rules.session.schema()->find_section(section);
    if (sec == nullptr)
        return;
    for (const CfgFieldSpec &f : sec->fields)
    {
        if (f.state == CfgState_Ignored && !s_rules.form.show_ignored)
            continue;
        if (!f.alias_of.empty())
            continue;
        s_rules.form.draw_row(section, f);
    }
}

std::vector<std::string> split_words(const std::string &text) { return form_split_words(text); }
std::string join_words(const std::vector<std::string> &v) { return form_join_words(v); }

// What the result of a recipe is chosen from, by key.
std::vector<std::string> result_options(const CfgFieldSpec &spec)
{
    if (spec.parts.empty())
        return std::vector<std::string>();
    const CfgValueSpec &p = spec.parts[0];
    if (!p.enum_names.empty())
        return p.enum_names;
    std::vector<std::string> out;
    for (const std::string &n : content_registry_list(s_rules.session.names(), p.enum_registry))
        if (n != "NOSPELL")
            out.push_back(n);
    return out;
}

const char *sacrifice_description(const std::string &key)
{
    const std::string k = lower(key);
    if (k == "mkcreature") return "Creates a creature on the sacrificer's side";
    if (k == "mkgoodhero") return "Creates a creature on the heroes' side";
    if (k == "posspellall") return "Casts a beneficial spell on all the sacrificer's creatures";
    if (k == "negspellall") return "Casts a harmful spell on all the sacrificer's creatures";
    if (k == "posuniqfunc") return "A special beneficial effect";
    if (k == "neguniqfunc") return "A special harmful effect";
    return "";
}

// One recipe per line, "<result> <victim> ...": drop-downs for the result and up to six victims.
void draw_sacrifice_group(const std::string &section)
{
    const CfgSectionSpec *sec = s_rules.session.schema()->find_section(section);
    if (sec == nullptr)
        return;
    const bool writable = s_rules.session.writable();
    FeCaption("Sacrifice recipes: the result, then up to six creatures that are sacrificed. The block replaces the layer beneath as a whole.");
    const std::vector<std::string> creatures = content_registry_list(s_rules.session.names(), "creature");
    bool any_here = false;
    for (const CfgFieldSpec &f : sec->fields)
        any_here = any_here || s_rules.session.list(section, f.key).overridden_here;

    for (const CfgFieldSpec &f : sec->fields)
    {
        if (f.parts.empty() || !f.repeat_last)
            continue; // CustomReward / CustomPunish: edit them in Config Files
        const ListView lv = s_rules.session.list(section, f.key);
        const std::vector<std::string> results = result_options(f);
        std::vector<std::string> lines = lv.lines;
        bool changed = false;
        ImGui::PushID(f.key.c_str());
        FeSubheading(s_rules.form.display_key(f).c_str());
        ImGui::SameLine();
        if (lv.pending)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "edited");
        else if (!lv.is_set)
            ImGui::TextDisabled("none");
        else
            ImGui::TextColored(layer_colour(lv.source), "%s", layer_name(lv.source));
        FeCaption(sacrifice_description(f.key));

        ImGui::BeginDisabled(!writable);
        for (size_t i = 0; i < lines.size(); i++)
        {
            ImGui::PushID((int)i);
            std::vector<std::string> words = split_words(lines[i]);
            if (words.empty())
                words.push_back("");
            bool row_changed = false;
            FeCaption("Result");
            ImGui::SameLine();
            row_changed |= form_pick_name("##result", words[0], results, col(17));
            ImGui::SameLine();
            bool removed = false;
            if (FeButton("Remove", ImVec2(col(5), 0)))
            {
                lines.erase(lines.begin() + (std::ptrdiff_t)i);
                changed = true;
                removed = true;
            }
            if (!removed)
            {
                FeCaption("Victims");
                for (size_t v = 1; v < words.size(); v++)
                {
                    ImGui::SameLine();
                    ImGui::PushID((int)v);
                    row_changed |= form_pick_name("##victim", words[v], creatures, col(12));
                    ImGui::PopID();
                }
                if (words.size() < 7)
                {
                    ImGui::SameLine();
                    if (FeButton("+", ImVec2(col(2), 0)))
                    {
                        words.push_back(creatures.empty() ? std::string("") : creatures[0]);
                        row_changed = true;
                    }
                }
                if (words.size() > 2)
                {
                    ImGui::SameLine();
                    if (FeButton("-", ImVec2(col(2), 0)))
                    {
                        words.pop_back();
                        row_changed = true;
                    }
                }
                if (row_changed)
                {
                    lines[i] = join_words(words);
                    changed = true;
                }
            }
            ImGui::PopID();
            if (removed)
                break;
        }
        if (FeButton("Add recipe", ImVec2(col(8), 0)))
        {
            lines.push_back(join_words({results.empty() ? std::string("") : results[0], creatures.empty() ? std::string("") : creatures[0]}));
            changed = true;
        }
        ImGui::EndDisabled();
        if (changed)
            s_rules.session.set_list(section, f.key, lines);
        ImGui::PopID();
        FeSeparator();
    }
    if (writable && any_here)
    {
        if (FeButton("Reset all sacrifices", ImVec2(col(14), 0)))
            for (const CfgFieldSpec &f : sec->fields)
                s_rules.session.reset_list(section, f.key);
    }
}

// --- Research ---------------------------------------------------------------------------------------------
// One line per item, "KIND NAME POINTS". An item can be in the list once, and the order of the list is the
// order the game offers them, so the editor works on items rather than text: the item is a fixed label (only
// what is not in the list yet can be added), the cost is a number, and the row moves.

struct ResearchItem
{
    std::string kind;  // MAGIC, ROOM, CREATURE (as written)
    std::string name;
    int64_t cost = 0;
    bool valid = false; // the line has all three parts and a numeric cost
    std::string raw;    // the line as it is in the file
};

ResearchItem parse_research(const std::string &line)
{
    ResearchItem it;
    it.raw = line;
    const std::vector<std::string> w = split_words(line);
    if (w.size() >= 3 && parse_int(w[2], it.cost))
    {
        it.kind = w[0];
        it.name = w[1];
        it.valid = true;
    }
    return it;
}

std::string research_line(const std::string &kind, const std::string &name, int64_t cost)
{
    return kind + " " + name + " " + std::to_string((long long)cost);
}

std::string research_key(const std::string &kind, const std::string &name)
{
    return lower(kind) + "/" + lower(name);
}

// Draws a drop-down that offers `options` and returns the one chosen (empty if none this frame).
std::string pick_to_add(const char *id, const char *placeholder, const std::vector<std::string> &options, float width)
{
    if (options.empty())
        return std::string();
    std::vector<const char *> ptrs;
    ptrs.push_back(placeholder);
    for (const std::string &o : options)
        ptrs.push_back(o.c_str());
    int64_t sel = 0;
    ImGui::SetNextItemWidth(width);
    if (FeCombo(id, &sel, ptrs.data(), (int64_t)ptrs.size()) && sel > 0)
        return options[(size_t)sel - 1];
    return std::string();
}

void draw_research_group(const std::string &section)
{
    const bool writable = s_rules.session.writable();
    const ListView lv = s_rules.session.list(section, "Research");
    std::vector<std::string> lines = lv.lines;
    std::vector<ResearchItem> items;
    for (const std::string &l : lines)
        items.push_back(parse_research(l));
    bool changed = false;

    FeCaption("The order here is the order the game offers them; each room or spell can be researched once. "
              "The number is the research points it costs.");
    // Where it comes from, and a summary.
    if (lv.pending)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "edited");
    else if (!lv.is_set)
        ImGui::TextDisabled("empty");
    else
        ImGui::TextColored(layer_colour(lv.source), "%s", layer_name(lv.source));
    int64_t total = 0;
    for (const ResearchItem &it : items)
        total += it.valid ? it.cost : 0;
    ImGui::SameLine();
    ImGui::TextDisabled("%zu items, %lld points in total", items.size(), (long long)total);

    // What is already in, to spot duplicates and to offer only what is not.
    std::set<std::string> used;
    std::vector<bool> is_dup(items.size(), false);
    for (size_t i = 0; i < items.size(); i++)
    {
        if (!items[i].valid)
            continue;
        const std::string k = research_key(items[i].kind, items[i].name);
        if (!used.insert(k).second)
            is_dup[i] = true;
    }

    ImGui::BeginDisabled(!writable);
    if (ImGui::BeginTable("##research", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, col(2));
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, col(3));
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, col(24));
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, col(12));
        ImGui::TableSetupColumn("Running total", ImGuiTableColumnFlags_WidthFixed, col(10));
        ImGui::TableSetupColumn("Move", ImGuiTableColumnFlags_WidthFixed, col(9));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, col(7));
        ImGui::TableHeadersRow();
        int64_t running = 0;
        int64_t remove_at = -1;
        int64_t move_from = -1, move_to = -1;
        for (size_t i = 0; i < items.size(); i++)
        {
            const ResearchItem &it = items[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Selectable("::", false, 0, ImVec2(col(1.6f), 0)); // drag handle
            if (ImGui::BeginDragDropSource())
            {
                const int src = (int)i;
                ImGui::SetDragDropPayload("RESEARCH_ROW", &src, sizeof(src));
                ImGui::TextUnformatted(it.valid ? it.name.c_str() : it.raw.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("RESEARCH_ROW"))
                {
                    move_from = *(const int *)pl->Data;
                    move_to = (int64_t)i;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i + 1);
            ImGui::TableNextColumn();
            if (!it.valid)
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "? %s", it.raw.c_str());
            else if (is_dup[i])
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "%s %s (repeated)", it.kind.c_str(), it.name.c_str());
            else
                ImGui::Text("%s  %s", it.kind.c_str(), it.name.c_str());
            ImGui::TableNextColumn();
            if (it.valid)
            {
                int64_t cost = it.cost;
                ImGui::SetNextItemWidth(col(11));
                if (FeInputInt("##cost", &cost, 100, 1000, 0, UINT32_MAX) && cost != it.cost)
                {
                    lines[i] = research_line(it.kind, it.name, cost);
                    changed = true;
                }
                running += it.cost;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%lld", (long long)running);
            ImGui::TableNextColumn();
            if (FeButton("Up", ImVec2(col(3.5f), 0)) && i > 0)
            {
                move_from = (int64_t)i;
                move_to = (int64_t)i - 1;
            }
            ImGui::SameLine();
            if (FeButton("Down", ImVec2(col(4), 0)) && i + 1 < items.size())
            {
                move_from = (int64_t)i;
                move_to = (int64_t)i + 1;
            }
            ImGui::TableNextColumn();
            if (FeButton("Remove", ImVec2(col(6), 0)))
                remove_at = (int64_t)i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (move_from >= 0 && move_to >= 0 && move_from != move_to)
        {
            const std::string moved = lines[(size_t)move_from];
            lines.erase(lines.begin() + move_from);
            lines.insert(lines.begin() + move_to, moved);
            changed = true;
        }
        if (remove_at >= 0)
        {
            lines.erase(lines.begin() + remove_at);
            changed = true;
        }
    }

    // Adding: only what is not in the list can be picked, so nothing can be added twice.
    auto available = [&](const char *registry, const char *kind, const char *none_name) {
        std::vector<std::string> out;
        for (const std::string &n : content_registry_list(s_rules.session.names(), registry))
            if (n != none_name && used.count(research_key(kind, n)) == 0)
                out.push_back(n);
        return out;
    };
    const int64_t default_cost = items.empty() || !items.back().valid ? 1000 : items.back().cost;
    FeCaption("Add");
    ImGui::SameLine();
    const std::string room = pick_to_add("##addroom", "room...", available("room", "ROOM", "NOROOM"), col(16));
    ImGui::SameLine();
    const std::string spell = pick_to_add("##addspell", "spell...", available("power", "MAGIC", "NOPOWER"), col(20));
    if (!room.empty())
    {
        lines.push_back(research_line("ROOM", room, default_cost));
        changed = true;
    }
    if (!spell.empty())
    {
        lines.push_back(research_line("MAGIC", spell, default_cost));
        changed = true;
    }

    // Whole-list tools.
    static int64_t s_scale = 100;
    if (FeButton("Sort by cost", ImVec2(col(9), 0)))
    {
        std::vector<ResearchItem> sorted;
        for (const std::string &l : lines)
            sorted.push_back(parse_research(l));
        std::stable_sort(sorted.begin(), sorted.end(), [](const ResearchItem &a, const ResearchItem &b) {
            return (a.valid ? a.cost : INT64_MAX) < (b.valid ? b.cost : INT64_MAX);
        });
        lines.clear();
        for (const ResearchItem &it : sorted)
            lines.push_back(it.raw);
        changed = true;
    }
    ImGui::SameLine();
    FeCaption("Scale all costs to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(col(9));
    FeInputInt("##scale", &s_scale, 5, 25, 1, 1000);
    ImGui::SameLine();
    FeCaption("%");
    ImGui::SameLine();
    if (FeButton("Apply scale", ImVec2(col(8), 0)))
    {
        for (std::string &l : lines)
        {
            const ResearchItem it = parse_research(l);
            if (it.valid)
                l = research_line(it.kind, it.name, it.cost * s_scale / 100);
        }
        changed = true;
    }
    ImGui::EndDisabled();
    if (changed)
        s_rules.session.set_list(section, "Research", lines);

    // What the list does not contain: those can only be had some other way (script, availability).
    size_t missing_rooms = 0, missing_spells = 0;
    missing_rooms = available("room", "ROOM", "NOROOM").size();
    missing_spells = available("power", "MAGIC", "NOPOWER").size();
    char note[200];
    snprintf(note, sizeof(note), "Not in the list (never researchable): %zu rooms, %zu spells.", missing_rooms, missing_spells);
    FeCaption(note);
    if (writable && lv.overridden_here && FeButton("Reset research", ImVec2(col(14), 0)))
        s_rules.session.reset_list(section, "Research");
}

void draw_list_group(const std::string &section)
{
    if (section == "sacrifices")
        draw_sacrifice_group(section);
    else
        draw_research_group(section);
}

void draw_window()
{
    content_ui_begin_window("##ContentRules", s_rules.pick.map_host);
    FeHeading("Rules Editor");
    FeSeparator();

    // The pickers are locked while there are unapplied edits: switching target would drop them.
    ImGui::BeginDisabled(s_rules.session.dirty());
    bool target_changed = s_rules.pick.draw_target();
    ImGui::SameLine();
    bool layer_changed = s_rules.pick.draw_layer();
    ImGui::EndDisabled();
    if (s_rules.session.dirty())
        FeCaption("Apply or Revert the changes before switching target or layer.");
    else if (!s_rules.pick.note.empty())
        FeCaption(s_rules.pick.note.c_str());
    if (target_changed || layer_changed || !s_rules.loaded || s_rules.pick.signature() != s_rules.loaded_signature)
        if (!s_rules.session.dirty())
            load();
    FeSeparator();

    // Fixed-height bottom row, so it can never scroll out of the window.
    const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing()
        + ImGui::GetStyle().ItemSpacing.y * 4 + ImGui::GetStyle().WindowPadding.y * 2;
    const float body_h = std::max(120.0f, ImGui::GetContentRegionAvail().y - bottom);

    std::vector<CfgDiagnostic> diags;
    if (s_rules.loaded)
    {
        diags = s_rules.session.diagnostics();
        s_rules.form.refresh_problems();
    }

    if (s_rules.loaded)
    {
        const bool tabs = FeBeginTabBar("##RulesTabs");
        if (tabs)
        {
            for (const char *g : kValueGroups)
                if (FeTabEx(title_case(g).c_str(), s_rules.force_tab == g))
                {
                    const bool area = FeBeginScrollArea(("##area_" + std::string(g)).c_str(), ImVec2(0, body_h));
                    if (area)
                        draw_value_group(g);
                    FeEndScrollArea();
                    FeEndTab();
                }
            for (const char *g : kListGroups)
                if (FeTabEx(title_case(g).c_str(), s_rules.force_tab == g))
                {
                    const bool area = FeBeginScrollArea(("##area_" + std::string(g)).c_str(), ImVec2(0, body_h));
                    if (area)
                        draw_list_group(g);
                    FeEndScrollArea();
                    FeEndTab();
                }
        }
        FeEndTabBar(tabs);
        s_rules.force_tab.clear();
    }
    else
    {
        FeBodyText("Pick a campaign or map pack to edit its rules.");
        ImGui::Dummy(ImVec2(0, body_h));
    }

    FeSeparator();
    const bool writable = s_rules.loaded && s_rules.session.writable();
    const bool dirty = s_rules.session.dirty();
    ImGui::BeginDisabled(!(writable && dirty));
    if (FeButton("Apply", ImVec2(110, 0)))
    {
        std::string err;
        size_t warnings = 0;
        if (s_rules.session.apply(&err, &warnings))
            s_rules.status = warnings > 0 ? "Applied with " + std::to_string(warnings) + " warning(s)." : "Applied.";
        else
            s_rules.status = "Not written: " + err;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (FeButton("Revert", ImVec2(110, 0)))
    {
        s_rules.session.discard();
        s_rules.status = "Changes discarded.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (FeButton("Close", ImVec2(110, 0)))
    {
        if (dirty)
            s_rules.status = "Apply or Revert the changes first.";
        else
            s_rules.open = false;
    }
    ImGui::SameLine();
    FeCheckbox("Show keys with no effect", &s_rules.form.show_ignored);
    ImGui::SameLine();
    char info[200];
    size_t warn = 0;
    for (const CfgDiagnostic &d : diags)
        if (d.severity >= CfgSev_Warning)
            warn++;
    if (!s_rules.status.empty())
        FeCaption(s_rules.status.c_str());
    else if (s_rules.loaded && !writable)
        FeCaption("Read only: base files are never edited. Pick the Campaign or Level layer to make changes.");
    else if (dirty)
    {
        snprintf(info, sizeof(info), "%zu change(s) not applied%s", s_rules.session.pending_count(),
            warn > 0 ? "; some values are outside the range the game accepts (red)" : "");
        FeCaption(info);
    }
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
    ImGui::End();
}

} // namespace

void content_rules_open(bool map_host)
{
    s_rules.open = true;
    s_rules.pick.open(map_host, map_host ? CfgLayer_Level : CfgLayer_Campaign);
    s_rules.base_read = false;
    s_rules.loaded = false;
    s_rules.status.clear();
    s_rules.session.discard();
    load();
}

void content_rules_frame(void)
{
    if (s_rules.open)
        draw_window();
}

void content_rules_show_tab(const char *block)
{
    s_rules.force_tab = block != nullptr ? block : "";
}

bool content_rules_is_open(void)
{
    return s_rules.open;
}
