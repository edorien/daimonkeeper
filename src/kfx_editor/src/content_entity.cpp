/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_entity.cpp
 *     See content_entity.h.
 */
#include "pre_inc.h"
#include "content_entity.h"
#include "frontgui_widgets.h"

#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include "post_inc.h"

/******************************************************************************/
std::string EntityEditor::title_of(Bundle &b, const std::string &id) const
{
    const FieldView n = b.session.value_of(id, "Name");
    return id + (n.text.empty() ? "" : "   " + n.text);
}

EntityEditor::Bundle &EntityEditor::bundle_for(const EntityMode &m)
{
    return bundles_[m.file_name.empty() ? cfg_.file_name : m.file_name];
}

bool EntityEditor::any_dirty() const
{
    for (const auto &b : bundles_)
        if (b.second.session.dirty())
            return true;
    return false;
}

const ConfigStack *EntityEditor::aux_for(const EntityLink &l) const
{
    const auto it = aux_.find(l.file_name);
    return it != aux_.end() ? it->second.get() : nullptr;
}

std::string EntityEditor::link_name(Bundle &b, const std::string &id, const EntityLink &l)
{
    const std::string text = b.session.value_of(id, l.key).text;
    if (l.key_word < 0)
        return text;
    const std::vector<std::string> w = form_split_words(text);
    return (size_t)l.key_word < w.size() ? w[(size_t)l.key_word] : std::string();
}

// The block of the linked file whose Name is `name`.
std::string EntityEditor::linked_id(const EntityLink &l, const std::string &name) const
{
    const ConfigStack *s = aux_for(l);
    if (s == nullptr || name.empty())
        return std::string();
    for (const std::string &id : s->numbered_section_ids(l.target_basename))
    {
        CfgEffective e;
        if (s->effective(id, "Name", e) && e.values.size() == 1 && form_lower(e.values[0]) == form_lower(name))
            return id;
    }
    return std::string();
}

std::string EntityEditor::linked_value(const EntityLink &l, const std::string &target_id, const std::string &key) const
{
    const ConfigStack *s = aux_for(l);
    CfgEffective e;
    if (s != nullptr && !target_id.empty() && s->effective(target_id, key, e) && !e.values.empty())
        return e.values[0];
    return std::string();
}

void EntityEditor::load()
{
    status_.clear();
    static const ConfigSchema schema = build_engine_schema();
    // One bundle per file the modes use: the editor's own file, plus any other a mode names.
    struct Want { std::string kind, file; };
    std::vector<Want> wanted;
    wanted.push_back({cfg_.kind, cfg_.file_name});
    for (const EntityMode &m : cfg_.modes)
        if (!m.file_name.empty())
        {
            bool have = false;
            for (const Want &w : wanted)
                have = have || w.file == m.file_name;
            if (!have)
                wanted.push_back({m.kind, m.file_name});
        }
    loaded_ = false;
    for (const Want &w : wanted)
    {
        Bundle &b = bundles_[w.file];
        b.form.session = &b.session;
        b.form.read_only = cfg_.read_only;
        b.form.plot_keys = cfg_.plot_keys;
        b.loaded = b.session.open(pick_.target, w.kind, w.file, (CfgLayer)pick_.layer_sel);
        if (!b.reference_read)
        {
            b.form.read_reference(pick_.target.path_for(w.file, CfgLayer_Base));
            b.reference_read = true;
        }
        if (w.file == cfg_.file_name)
            loaded_ = b.loaded;
    }
    loaded_signature_ = pick_.signature();
    // The files the links point into, read once per load, for the read-only summaries.
    aux_.clear();
    for (const EntityLink &l : cfg_.links)
        if (aux_.find(l.file_name) == aux_.end())
            aux_[l.file_name].reset(new ConfigStack(ConfigStack::load(pick_.target, l.kind, l.file_name, schema.find(l.kind))));
    if (!loaded_)
        status_ = "This target has no " + cfg_.file_name + " location for that layer.";
}

void EntityEditor::draw_links(const EntityMode &m, Bundle &b, const std::string &group, const std::string &id)
{
    for (const EntityLink &l : cfg_.links)
    {
        if (l.mode_basename != m.basename || l.group != group)
            continue;
        const std::string name = link_name(b, id, l);
        const std::string target = linked_id(l, name);
        if (l.optional && target.empty())
            continue;
        FeSeparator();
        FeSubheading(l.title.c_str());
        if (name.empty() || target.empty())
        {
            FeCaption(name.empty() ? ("No " + l.key + " set.").c_str() : (l.key + " " + name + " is not a " + l.target_basename + " of " + l.file_name + ".").c_str());
            continue;
        }
        b.form.draw_info_row(l.key, name + "  (" + target + ")", l.file_name + ", read only");
        for (const std::string &k : l.show)
        {
            const std::string v = linked_value(l, target, k.c_str());
            if (!v.empty())
                b.form.draw_info_row(k, v, "");
        }
        if (!l.note.empty())
            FeCaption(l.note.c_str());
    }
}

void EntityEditor::draw_group(const EntityMode &m, Bundle &b, const std::string &group, const std::string &id)
{
    const CfgSectionSpec *sec = b.session.schema()->find_section(m.basename);
    if (sec == nullptr)
        return;
    const auto note = m.group_notes.find(group);
    if (note != m.group_notes.end())
        FeCaption(note->second.c_str());
    for (const CfgFieldSpec &f : sec->fields)
    {
        if (form_lower(f.key) == "name" || !f.alias_of.empty())
            continue;
        if (f.state == CfgState_Ignored && !b.form.show_ignored)
            continue;
        if (m.group_of(f.key) != group)
            continue;
        b.form.draw_row(id, f);
    }
    // A value that two entities of this kind must not share (a workshop panel position).
    if (group == m.unique_group)
        for (const std::string &key : m.unique_keys)
        {
            const std::string mine = b.session.value_of(id, key).text;
            std::string clash;
            if (!mine.empty() && mine != "0")
                for (const std::string &other : b.session.section_ids(m.basename))
                    if (other != id && b.session.value_of(other, key).text == mine)
                        clash += (clash.empty() ? "" : ", ") + title_of(b, other);
            if (!clash.empty())
                FeCaption(("Warning: " + key + " " + mine + " is also used by " + clash + ".").c_str());
        }
    draw_links(m, b, group, id);
}

void EntityEditor::draw_compare(const EntityMode &m, Bundle &b, const std::vector<std::string> &ids)
{
    FeCaption("All items side by side. Colour shows where each value comes from (grey base, amber campaign, green level).");
    const int total = 1 + (int)m.compare_keys.size() + (int)m.compare_linked.size();
    if (!ImGui::BeginTable("##compare", total, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
        return;
    ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, form_col(18));
    for (const std::string &k : m.compare_keys)
        ImGui::TableSetupColumn(k.c_str(), ImGuiTableColumnFlags_WidthFixed, form_col(8));
    for (const EntityLinkedColumn &c : m.compare_linked)
        ImGui::TableSetupColumn(c.heading.c_str(), ImGuiTableColumnFlags_WidthFixed, form_col(9));
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableHeadersRow();
    for (const std::string &id : ids)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(title_of(b, id).c_str());
        for (const std::string &spec : m.compare_keys)
        {
            ImGui::TableNextColumn();
            // "Key[0]" / "Key[last]": one word of a multi-value key.
            std::string key = spec;
            int64_t word = -1;
            bool last = false;
            const size_t br = spec.find('[');
            if (br != std::string::npos)
            {
                key = spec.substr(0, br);
                last = spec.compare(br, 6, "[last]") == 0;
                if (!last)
                    word = std::atoll(spec.c_str() + br + 1);
            }
            const FieldView v = b.session.value_of(id, key);
            std::string text = v.text;
            if (br != std::string::npos)
            {
                const std::vector<std::string> w = form_split_words(v.text);
                text = w.empty() ? std::string() : last ? w.back() : ((size_t)word < w.size() ? w[(size_t)word] : std::string());
            }
            if (v.is_set)
                ImGui::TextColored(form_layer_colour(v.source), "%s", text.c_str());
            else
                ImGui::TextDisabled("%s", text.c_str());
        }
        for (const EntityLinkedColumn &c : m.compare_linked)
        {
            ImGui::TableNextColumn();
            const EntityLink &l = cfg_.links[c.link];
            const std::string v = linked_value(l, linked_id(l, link_name(b, id, l)), c.key);
            if (v.empty())
                ImGui::TextDisabled("-");
            else
                ImGui::TextUnformatted(v.c_str());
        }
    }
    ImGui::EndTable();
}

void EntityEditor::draw_mode(size_t mode, float body_h)
{
    const EntityMode &m = cfg_.modes[mode];
    Bundle &b = bundle_for(m);
    if (!b.loaded)
    {
        FeBodyText(("This target has no " + (m.file_name.empty() ? cfg_.file_name : m.file_name) + " location for that layer.").c_str());
        return;
    }
    const std::vector<std::string> ids = m.single ? std::vector<std::string>{m.basename} : b.session.section_ids(m.basename);
    int64_t &sel = selected_[mode];
    if (sel >= (int64_t)ids.size())
        sel = 0;
    if (!ImGui::BeginTable(("##split" + m.basename).c_str(), 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
        return;
    ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthFixed, form_col(17)); // drag the divider to widen
    ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow(0, body_h);
    ImGui::TableNextColumn();
    const bool list = FeBeginListBox(("##list" + m.basename).c_str(), ImVec2(-FLT_MIN, body_h));
    if (list)
    {
        for (size_t i = 0; i < ids.size(); i++)
        {
            std::string label = title_of(b, ids[i]);
            if (b.session.section_touched(ids[i]))
                label += "   *";
            if (FeListRow((label + "##i" + std::to_string(i)).c_str(), (int64_t)i == sel))
                sel = (int64_t)i;
        }
    }
    FeEndListBox(list);
    ImGui::TableNextColumn();
    if (!ids.empty())
    {
        const std::string &id = ids[(size_t)sel];
        FeSubheading(title_of(b, id).c_str());
        const bool tabs = FeBeginTabBar(("##tabs" + m.basename).c_str());
        if (tabs)
        {
            for (const std::string &g : m.groups)
                if (FeTabEx(g.c_str(), force_group_ == g))
                {
                    const bool area = FeBeginScrollArea(("##area_" + m.basename + g).c_str(), ImVec2(0, body_h - ImGui::GetFrameHeightWithSpacing() * 3));
                    if (area)
                        draw_group(m, b, g, id);
                    FeEndScrollArea();
                    FeEndTab();
                }
            if (!m.compare_keys.empty() && FeTabEx("Compare all", force_group_ == "Compare all"))
            {
                draw_compare(m, b, ids);
                FeEndTab();
            }
        }
        FeEndTabBar(tabs);
    }
    ImGui::EndTable();
}

void EntityEditor::draw_window()
{
    content_ui_begin_window(cfg_.imgui_id.c_str(), pick_.map_host);
    FeHeading(cfg_.title.c_str());
    FeSeparator();

    ImGui::BeginDisabled(any_dirty());
    const bool target_changed = pick_.draw_target();
    ImGui::SameLine();
    const bool layer_changed = pick_.draw_layer();
    ImGui::EndDisabled();
    if (any_dirty())
        FeCaption("Apply or Revert the changes before switching target or layer.");
    else if (!pick_.note.empty())
        FeCaption(pick_.note.c_str());
    if (target_changed || layer_changed || !loaded_ || pick_.signature() != loaded_signature_)
        if (!any_dirty())
            load();
    FeSeparator();

    const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing()
        + ImGui::GetStyle().ItemSpacing.y * 4 + ImGui::GetStyle().WindowPadding.y * 2;
    const float body_h = std::max(140.0f, ImGui::GetContentRegionAvail().y - bottom);

    std::vector<CfgDiagnostic> diags;
    if (loaded_)
    {
        for (auto &b : bundles_)
            if (b.second.loaded)
            {
                for (CfgDiagnostic &d : b.second.session.diagnostics())
                    diags.push_back(std::move(d));
                b.second.form.refresh_problems();
            }
        if (selected_.size() < cfg_.modes.size())
            selected_.resize(cfg_.modes.size(), 0);
        const bool modes = FeBeginTabBar(("##modes" + cfg_.imgui_id).c_str());
        if (modes)
        {
            for (size_t i = 0; i < cfg_.modes.size(); i++)
                if (FeTabEx(cfg_.modes[i].label.c_str(), force_mode_ == (int64_t)i))
                {
                    draw_mode(i, body_h - ImGui::GetFrameHeightWithSpacing());
                    FeEndTab();
                }
        }
        FeEndTabBar(modes);
        force_mode_ = -1;
        force_group_.clear();
    }
    else
    {
        FeBodyText(cfg_.empty_text.c_str());
        ImGui::Dummy(ImVec2(0, body_h));
    }

    FeSeparator();
    bool writable = loaded_;
    for (const auto &b : bundles_)
        if (b.second.session.dirty() && !b.second.session.writable())
            writable = false;
    if (loaded_)
        writable = writable && bundles_[cfg_.file_name].session.writable();
    const bool dirty = any_dirty();
    size_t pending = 0;
    for (const auto &b : bundles_)
        pending += b.second.session.pending_count();
    ImGui::BeginDisabled(!(writable && dirty));
    if (FeButton("Apply", ImVec2(110, 0)))
    {
        // Each file is written atomically; several files (spells and abilities) one after the other.
        std::string err;
        size_t warnings = 0;
        bool ok = true;
        for (auto &b : bundles_)
            if (ok && b.second.session.dirty())
            {
                size_t w = 0;
                ok = b.second.session.apply(&err, &w);
                warnings += w;
            }
        if (ok)
        {
            status_ = warnings > 0 ? "Applied with " + std::to_string(warnings) + " warning(s)." : "Applied.";
            load(); // linked summaries and names may have changed with the layer
        }
        else
            status_ = "Not written: " + err;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (FeButton("Revert", ImVec2(110, 0)))
    {
        for (auto &b : bundles_)
            b.second.session.discard();
        status_ = "Changes discarded.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (FeButton("Close", ImVec2(110, 0)))
    {
        if (dirty)
            status_ = "Apply or Revert the changes first.";
        else
            open_ = false;
    }
    ImGui::SameLine();
    {
        static bool s_show_ignored_shared = false;
        if (FeCheckbox("Show keys with no effect", &s_show_ignored_shared))
            for (auto &b : bundles_)
                b.second.form.show_ignored = s_show_ignored_shared;
    }
    ImGui::SameLine();
    size_t warn = 0;
    for (const CfgDiagnostic &d : diags)
        if (d.severity >= CfgSev_Warning)
            warn++;
    char info[200];
    if (!status_.empty())
        FeCaption(status_.c_str());
    else if (loaded_ && !writable)
        FeCaption("Read only: base files are never edited. Pick the Campaign or Level layer to make changes.");
    else if (dirty)
    {
        snprintf(info, sizeof(info), "%zu change(s) not applied%s", pending,
            warn > 0 ? "; some values are outside the range the game accepts (red)" : "");
        FeCaption(info);
    }
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
    ImGui::End();
}

void EntityEditor::open(bool map_host)
{
    open_ = true;
    pick_.open(map_host, map_host ? CfgLayer_Level : CfgLayer_Campaign);
    for (auto &b : bundles_)
    {
        b.second.reference_read = false;
        b.second.session.discard();
    }
    loaded_ = false;
    status_.clear();
    load();
}

void EntityEditor::frame()
{
    if (open_)
        draw_window();
}

void EntityEditor::show(size_t mode, const std::string &group)
{
    force_mode_ = (int64_t)mode;
    force_group_ = group;
}
