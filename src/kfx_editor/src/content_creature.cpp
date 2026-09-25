/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_creature.cpp
 *     The Creature editor window. See content_creature.h.
 */
#include "pre_inc.h"
#include "content_creature.h"
#include "content_picker.h"
#include "content_struct.h"
#include "content_form.h"
#include "content_names.h"
#include "frontgui_widgets.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
int64_t creature_level_value(int64_t base, int64_t percent, int64_t level_index)
{
    return base + (percent * base * level_index) / 100;
}

const std::vector<std::string> &creature_sections(void)
{
    static const std::vector<std::string> s = {"attributes", "attraction", "annoyance", "jobs", "experience", "appearance",
        "senses", "sprites", "sounds"};
    return s;
}

/******************************************************************************/
namespace {

struct CreatureState
{
    bool open = false;
    ContentPicker pick;
    StructuredSession global; // creature.cfg: the creature list and the experience percentages
    FormContext global_form;
    StructuredSession model;  // the selected creature's file
    FormContext model_form;
    bool loaded = false;
    bool reference_read = false;
    std::string loaded_signature;
    std::string status;
    std::vector<std::string> creatures; // names, in creature.cfg order
    std::set<std::string> edited;       // creatures whose file this layer has
    int64_t selected = 0;
    std::string opened_creature;        // the creature `model` was opened for
    std::string force_tab;
    // Every creature's stack, for the comparison table (read once per load).
    std::map<std::string, std::unique_ptr<ConfigStack>> stacks;
    std::set<std::string> read_only_sections = {"sprites", "sounds"};
};

CreatureState s_cr;

std::string file_of(const std::string &creature)
{
    return form_lower(creature) + ".cfg";
}

std::string title_case(const std::string &s)
{
    std::string out = s;
    if (!out.empty())
        out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

bool any_dirty()
{
    return s_cr.model.dirty() || s_cr.global.dirty();
}

void open_model()
{
    if (s_cr.creatures.empty() || s_cr.selected < 0 || (size_t)s_cr.selected >= s_cr.creatures.size())
        return;
    const std::string name = s_cr.creatures[(size_t)s_cr.selected];
    s_cr.model.open(s_cr.pick.target, "creaturemodel", file_of(name), (CfgLayer)s_cr.pick.layer_sel, true);
    s_cr.model_form.session = &s_cr.model;
    s_cr.opened_creature = name;
    // Sprite and sound keys are indices into tables: shown, not edited (plan 04 §3).
    s_cr.model_form.read_only.clear();
    for (const std::string &sec : s_cr.read_only_sections)
    {
        const CfgSectionSpec *spec = s_cr.model.schema() != nullptr ? s_cr.model.schema()->find_section(sec) : nullptr;
        if (spec != nullptr)
            for (const CfgFieldSpec &f : spec->fields)
                s_cr.model_form.read_only.insert(form_lower(f.key));
    }
}

void load()
{
    s_cr.status.clear();
    static const ConfigSchema schema = build_engine_schema();
    s_cr.global_form.session = &s_cr.global;
    s_cr.loaded = s_cr.global.open(s_cr.pick.target, "creature", "creature.cfg", (CfgLayer)s_cr.pick.layer_sel);
    s_cr.loaded_signature = s_cr.pick.signature();
    if (!s_cr.reference_read)
    {
        s_cr.global_form.read_reference(s_cr.pick.target.path_for("creature.cfg", CfgLayer_Base));
        s_cr.model_form.read_reference(s_cr.pick.target.path_for("attributes.cfg", CfgLayer_Base, true)); // replaced below
        s_cr.reference_read = true;
    }
    s_cr.creatures = form_split_words(s_cr.global.value_of("common", "Creatures").text);
    if (s_cr.selected >= (int64_t)s_cr.creatures.size())
        s_cr.selected = 0;
    s_cr.edited.clear();
    s_cr.stacks.clear();
    for (const std::string &c : s_cr.creatures)
    {
        const std::string p = s_cr.pick.target.path_for(file_of(c), (CfgLayer)s_cr.pick.layer_sel, true);
        std::error_code ec;
        if ((CfgLayer)s_cr.pick.layer_sel != CfgLayer_Base && !p.empty() && std::filesystem::exists(p, ec))
            s_cr.edited.insert(c);
        s_cr.stacks[c].reset(new ConfigStack(ConfigStack::load(s_cr.pick.target, "creaturemodel", file_of(c), schema.find("creaturemodel"), true)));
    }
    if (s_cr.loaded)
    {
        // Help text and key spelling come from a base model file (they share their comments).
        if (!s_cr.creatures.empty())
            s_cr.model_form.read_reference(s_cr.pick.target.path_for(file_of(s_cr.creatures[0]), CfgLayer_Base, true));
        open_model();
    }
    else
        s_cr.status = "This target has no creature.cfg location for that layer.";
}

// The stat percentages the engine scales by (creature.cfg [experience]).
int64_t percent_of(const char *key)
{
    const FieldView v = s_cr.global.value_of("experience", key);
    int64_t n = 35; // the engine's default (CREATURE_PROPERTY_INCREASE_ON_EXP)
    form_parse_int(v.text, n);
    return n;
}

// --- Composite editors: two keys whose values line up position by position -------------------------------------
// EntranceRoom (three rooms) with RoomSlabsRequired (three counts), and Powers (ten abilities) with
// PowersLevelRequired (ten levels). Each is drawn as rows instead of two lines of numbers.

std::vector<std::string> words_padded(const std::string &section, const char *key, size_t n, const char *fill)
{
    std::vector<std::string> w = form_split_words(s_cr.model.value_of(section, key).text);
    w.resize(n, fill);
    return w;
}

void draw_source_badge(const char *section, const char *key, bool &reset)
{
    const FieldView v = s_cr.model.value_of(section, key);
    ImGui::SameLine(form_col(44));
    if (v.pending)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "edited");
    else if (!v.is_set)
        ImGui::TextDisabled("default");
    else
        ImGui::TextColored(form_layer_colour(v.source), "%s", form_layer_name(v.source));
    reset = false;
    if (s_cr.model.writable() && v.overridden_here)
    {
        ImGui::SameLine();
        if (FeButton("Reset", ImVec2(form_col(5), 0)))
            reset = true;
    }
}

bool is_null_word(const std::string &w)
{
    return w.empty() || form_lower(w) == "null";
}

void draw_entrance()
{
    static const size_t kSlots = 3;
    FeSubheading("Entrance requirements");
    FeCaption("The rooms this creature needs in the dungeon before it will arrive, and how many slabs of each.");
    std::vector<std::string> rooms = words_padded("attraction", "EntranceRoom", kSlots, "NULL");
    std::vector<std::string> slabs = words_padded("attraction", "RoomSlabsRequired", kSlots, "0");
    const std::vector<std::string> options = content_registry_list(s_cr.model.names(), "room");
    bool changed = false;
    ImGui::BeginDisabled(!s_cr.model.writable());
    size_t active = 0;
    int64_t remove = -1;
    for (size_t i = 0; i < kSlots; i++)
    {
        if (is_null_word(rooms[i]))
            continue;
        active++;
        ImGui::PushID((int)i);
        changed |= form_pick_name("##room", rooms[i], options, form_col(18));
        ImGui::SameLine();
        FeCaption("slabs");
        ImGui::SameLine();
        int64_t count = 0;
        form_parse_int(slabs[i], count);
        ImGui::SetNextItemWidth(form_col(11));
        if (FeInputInt("##slabs", &count, 1, 5, 0, 1000))
        {
            slabs[i] = std::to_string((long long)count);
            changed = true;
        }
        ImGui::SameLine();
        if (FeButton("Remove", ImVec2(form_col(6), 0)))
            remove = (int64_t)i;
        ImGui::PopID();
    }
    if (remove >= 0)
    {
        rooms.erase(rooms.begin() + remove);
        slabs.erase(slabs.begin() + remove);
        rooms.push_back("NULL");
        slabs.push_back("0");
        changed = true;
    }
    if (active < kSlots && FeButton("Add room", ImVec2(form_col(9), 0)))
    {
        for (size_t i = 0; i < kSlots; i++)
            if (is_null_word(rooms[i]))
            {
                rooms[i] = options.empty() ? "LAIR" : options[0];
                slabs[i] = "10";
                break;
            }
        changed = true;
    }
    ImGui::EndDisabled();
    if (changed)
    {
        s_cr.model.set("attraction", "EntranceRoom", form_join_words(rooms));
        s_cr.model.set("attraction", "RoomSlabsRequired", form_join_words(slabs));
    }
    bool reset = false;
    draw_source_badge("attraction", "EntranceRoom", reset);
    if (reset)
    {
        s_cr.model.reset("attraction", "EntranceRoom");
        s_cr.model.reset("attraction", "RoomSlabsRequired");
    }
    FeSeparator();
}

void draw_powers()
{
    static const size_t kSlots = 10;
    FeSubheading("Abilities by level");
    FeCaption("The abilities this creature gains, in the order of its ten slots, and the experience level each needs. NULL leaves a slot empty.");
    std::vector<std::string> powers = words_padded("experience", "Powers", kSlots, "NULL");
    std::vector<std::string> levels = words_padded("experience", "PowersLevelRequired", kSlots, "0");
    std::vector<std::string> options = content_registry_list(s_cr.model.names(), "instance");
    options.insert(options.begin(), "NULL");
    bool changed = false;
    ImGui::BeginDisabled(!s_cr.model.writable());
    for (size_t i = 0; i < kSlots; i++)
    {
        ImGui::PushID((int)i);
        ImGui::TextDisabled("%zu", i + 1);
        ImGui::SameLine(form_col(3));
        changed |= form_pick_name("##power", powers[i], options, form_col(24));
        ImGui::SameLine();
        FeCaption("level");
        ImGui::SameLine();
        int64_t level = 0;
        form_parse_int(levels[i], level);
        ImGui::SetNextItemWidth(form_col(11));
        if (FeInputInt("##level", &level, 1, 1, 0, 10))
        {
            levels[i] = std::to_string((long long)level);
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (changed)
    {
        s_cr.model.set("experience", "Powers", form_join_words(powers));
        s_cr.model.set("experience", "PowersLevelRequired", form_join_words(levels));
    }
    bool reset = false;
    draw_source_badge("experience", "Powers", reset);
    if (reset)
    {
        s_cr.model.reset("experience", "Powers");
        s_cr.model.reset("experience", "PowersLevelRequired");
    }
    FeSeparator();
}

void draw_section(const std::string &section)
{
    const CfgSectionSpec *sec = s_cr.model.schema()->find_section(section);
    if (sec == nullptr)
        return;
    // The line-up pairs are drawn as rows above the other keys.
    if (section == "attraction")
        draw_entrance();
    if (section == "experience")
        draw_powers();
    for (const CfgFieldSpec &f : sec->fields)
    {
        if (f.state == CfgState_Ignored && !s_cr.model_form.show_ignored)
            continue;
        const std::string k = form_lower(f.key);
        if ((section == "attraction" && (k == "entranceroom" || k == "roomslabsrequired"))
            || (section == "experience" && (k == "powers" || k == "powerslevelrequired")))
            continue;
        s_cr.model_form.draw_row(section, f);
    }
}

void draw_preview()
{
    FeCaption("The stats of this creature at each experience level, from its values here and the percentages of creature.cfg [experience].");
    struct Row { const char *label; const char *key; const char *pct; };
    static const Row rows[] = {{"Health", "Health", "HealthIncreaseOnExp"}, {"Strength", "Strength", "StrengthIncreaseOnExp"},
        {"Armour", "Armour", "ArmourIncreaseOnExp"}, {"Defence", "Defence", "DefenseIncreaseOnExp"},
        {"Dexterity", "Dexterity", "DexterityIncreaseOnExp"}, {"Pay", "Pay", "PayIncreaseOnExp"},
        {"Training cost", "TrainingCost", "TrainingCostIncreaseOnExp"}};
    // Stats down the side, experience levels across: there are more stats than levels' worth of width to spare.
    if (!ImGui::BeginTable("##preview", 11, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_ScrollX | ImGuiTableFlags_NoSavedSettings))
        return;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, form_col(8));
    // Narrow columns (the numbers are at most five digits); drag a header edge to resize, scroll sideways if needed.
    for (int64_t level = 1; level <= 10; level++)
        ImGui::TableSetupColumn(("L" + std::to_string((long long)level)).c_str(), ImGuiTableColumnFlags_WidthFixed, form_col(2.9f));
    ImGui::TableHeadersRow();
    for (const Row &r : rows)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.label);
        const char *section = form_lower(r.key) == "trainingcost" ? "jobs" : "attributes";
        int64_t base = 0;
        form_parse_int(s_cr.model.value_of(section, r.key).text, base);
        const int64_t pct = percent_of(r.pct);
        for (int64_t level = 0; level < 10; level++)
        {
            ImGui::TableNextColumn();
            ImGui::Text("%lld", (long long)creature_level_value(base, pct, level));
        }
    }
    ImGui::EndTable();
}

void draw_compare()
{
    FeCaption("Every creature side by side (as saved). Colour shows where each value comes from.");
    static const char *const keys[] = {"Health", "Strength", "Armour", "Defence", "Dexterity", "Luck", "Pay", "HungerRate", "GoldHold", "SlapsToKill"};
    const int ncols = 1 + (int)(sizeof(keys) / sizeof(keys[0]));
    if (!ImGui::BeginTable("##crcompare", ncols, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
        return;
    ImGui::TableSetupColumn("Creature", ImGuiTableColumnFlags_WidthFixed, form_col(15));
    for (const char *k : keys)
        ImGui::TableSetupColumn(k, ImGuiTableColumnFlags_WidthFixed, form_col(7));
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableHeadersRow();
    for (const std::string &c : s_cr.creatures)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(c.c_str());
        const auto it = s_cr.stacks.find(c);
        for (const char *k : keys)
        {
            ImGui::TableNextColumn();
            CfgEffective e;
            if (it != s_cr.stacks.end() && it->second->effective("attributes", k, e) && !e.values.empty())
                ImGui::TextColored(form_layer_colour(e.source), "%s", e.values[0].c_str());
            else
                ImGui::TextDisabled("-");
        }
    }
    ImGui::EndTable();
}

void draw_global()
{
    FeCaption("How much a creature grows with each experience level, in percent of its base value (creature.cfg [experience]). Applies to every creature.");
    const CfgSectionSpec *sec = s_cr.global.schema()->find_section("experience");
    if (sec == nullptr)
        return;
    for (const CfgFieldSpec &f : sec->fields)
        s_cr.global_form.draw_row("experience", f);
}

void draw_detail(float body_h)
{
    if (s_cr.creatures.empty())
        return;
    const std::string &name = s_cr.creatures[(size_t)s_cr.selected];
    FeSubheading(name.c_str());
    if (!s_cr.model.is_open())
    {
        FeBodyText("This target has no creature folder for that layer.");
        return;
    }
    const bool tabs = FeBeginTabBar("##crtabs");
    if (tabs)
    {
        for (const std::string &sec : creature_sections())
            if (FeTabEx(title_case(sec).c_str(), s_cr.force_tab == title_case(sec)))
            {
                const bool area = FeBeginScrollArea(("##crarea_" + sec).c_str(), ImVec2(0, body_h - ImGui::GetFrameHeightWithSpacing() * 3));
                if (area)
                    draw_section(sec);
                FeEndScrollArea();
                FeEndTab();
            }
        if (FeTabEx("Preview", s_cr.force_tab == "Preview"))
        {
            draw_preview();
            FeEndTab();
        }
        if (FeTabEx("Compare all", s_cr.force_tab == "Compare all"))
        {
            draw_compare();
            FeEndTab();
        }
        if (FeTabEx("Global", s_cr.force_tab == "Global"))
        {
            draw_global();
            FeEndTab();
        }
    }
    FeEndTabBar(tabs);
}

void draw_window()
{
    content_ui_begin_window("##ContentCreature", s_cr.pick.map_host);
    FeHeading("Creature Editor");
    FeSeparator();

    ImGui::BeginDisabled(any_dirty());
    const bool target_changed = s_cr.pick.draw_target();
    ImGui::SameLine();
    const bool layer_changed = s_cr.pick.draw_layer();
    ImGui::EndDisabled();
    if (any_dirty())
        FeCaption("Apply or Revert the changes before switching target, layer or creature.");
    else if (!s_cr.pick.note.empty())
        FeCaption(s_cr.pick.note.c_str());
    if (target_changed || layer_changed || !s_cr.loaded || s_cr.pick.signature() != s_cr.loaded_signature)
        if (!any_dirty())
            load();
    FeSeparator();

    const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing()
        + ImGui::GetStyle().ItemSpacing.y * 4 + ImGui::GetStyle().WindowPadding.y * 2;
    const float body_h = std::max(140.0f, ImGui::GetContentRegionAvail().y - bottom);

    std::vector<CfgDiagnostic> diags;
    if (s_cr.loaded)
    {
        for (CfgDiagnostic &d : s_cr.model.diagnostics())
            diags.push_back(std::move(d));
        for (CfgDiagnostic &d : s_cr.global.diagnostics())
            diags.push_back(std::move(d));
        s_cr.model_form.refresh_problems();
        s_cr.global_form.refresh_problems();
        if (ImGui::BeginTable("##crsplit", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthFixed, form_col(15)); // drag the divider to widen
            ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow(0, body_h);
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(any_dirty());
            const bool list = FeBeginListBox("##crlist", ImVec2(-FLT_MIN, body_h));
            if (list)
                for (size_t i = 0; i < s_cr.creatures.size(); i++)
                {
                    std::string label = s_cr.creatures[i];
                    if (s_cr.edited.count(s_cr.creatures[i]))
                        label += "   *";
                    if (FeListRow((label + "##c" + std::to_string(i)).c_str(), (int64_t)i == s_cr.selected) && (int64_t)i != s_cr.selected)
                    {
                        s_cr.selected = (int64_t)i;
                        open_model();
                    }
                }
            FeEndListBox(list);
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            draw_detail(body_h);
            s_cr.force_tab.clear();
            ImGui::EndTable();
        }
    }
    else
    {
        FeBodyText("Pick a campaign or map pack to edit its creatures.");
        ImGui::Dummy(ImVec2(0, body_h));
    }

    FeSeparator();
    const bool writable = s_cr.loaded && s_cr.model.writable();
    const bool dirty = any_dirty();
    ImGui::BeginDisabled(!(writable && dirty));
    if (FeButton("Apply", ImVec2(110, 0)))
    {
        std::string err;
        size_t warnings = 0, w = 0;
        bool ok = true;
        if (s_cr.model.dirty())
        {
            ok = s_cr.model.apply(&err, &w);
            warnings += w;
        }
        if (ok && s_cr.global.dirty())
        {
            ok = s_cr.global.apply(&err, &w);
            warnings += w;
        }
        if (ok)
        {
            s_cr.status = warnings > 0 ? "Applied with " + std::to_string(warnings) + " warning(s)." : "Applied.";
            load();
        }
        else
            s_cr.status = "Not written: " + err;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (FeButton("Revert", ImVec2(110, 0)))
    {
        s_cr.model.discard();
        s_cr.global.discard();
        s_cr.status = "Changes discarded.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (FeButton("Close", ImVec2(110, 0)))
    {
        if (dirty)
            s_cr.status = "Apply or Revert the changes first.";
        else
            s_cr.open = false;
    }
    ImGui::SameLine();
    if (FeCheckbox("Show keys with no effect", &s_cr.model_form.show_ignored))
        s_cr.global_form.show_ignored = s_cr.model_form.show_ignored;
    ImGui::SameLine();
    size_t warn = 0;
    for (const CfgDiagnostic &d : diags)
        if (d.severity >= CfgSev_Warning)
            warn++;
    char info[200];
    if (!s_cr.status.empty())
        FeCaption(s_cr.status.c_str());
    else if (s_cr.loaded && !writable)
        FeCaption("Read only: base files are never edited. Pick the Campaign or Level layer to make changes.");
    else if (dirty)
    {
        snprintf(info, sizeof(info), "%zu change(s) not applied%s", s_cr.model.pending_count() + s_cr.global.pending_count(),
            warn > 0 ? "; some values are outside the range the game accepts (red)" : "");
        FeCaption(info);
    }
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
    ImGui::End();
}

} // namespace

void content_creature_open(bool map_host)
{
    s_cr.open = true;
    s_cr.pick.open(map_host, map_host ? CfgLayer_Level : CfgLayer_Campaign);
    s_cr.reference_read = false;
    s_cr.loaded = false;
    s_cr.status.clear();
    s_cr.model.discard();
    s_cr.global.discard();
    load();
}

void content_creature_frame(void)
{
    if (s_cr.open)
        draw_window();
}

bool content_creature_is_open(void)
{
    return s_cr.open;
}

void content_creature_show(const char *tab, int creature_index)
{
    s_cr.force_tab = tab != nullptr ? tab : "";
    if (creature_index >= 0 && (size_t)creature_index < s_cr.creatures.size() && !any_dirty() && (int64_t)creature_index != s_cr.selected)
    {
        s_cr.selected = creature_index;
        open_model();
    }
}
