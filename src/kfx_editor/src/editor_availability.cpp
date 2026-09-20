/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_availability.cpp
 *     See editor_availability.h.
 * @par Comment:
 *     Cells are one of: unset (no line emitted -- the engine/script default
 *     applies), Off, Available, Researchable. Creatures/traps/doors have no
 *     researchable state (confirmed against lvl_script_value.c's setters:
 *     trap/door research goes through RESEARCH/RESEARCH_ORDER, and
 *     CREATURE_AVAILABLE's second number is a forced-in count, not
 *     research). Entries keep their raw (a, b) pair and are only rewritten
 *     when the user clicks that cell, so values the grid has no UI for
 *     (a creature force count, a trap stock amount) survive untouched.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_availability.h"
#include "script_setup.h"
#include "kfx_editor.h"
#include "editor_script.h"

#include "frontgui_widgets.h"

#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

bool s_show_availability = false;
ManagedSetupValues s_values;
int s_players = 1;

enum CellState { Cell_Unset = 0, Cell_Off, Cell_Available, Cell_Researchable };

bool kind_has_research(int kind)
{
    return (kind == AvailKind_Room) || (kind == AvailKind_Magic);
}

CellState state_of(int kind, const AvailabilityEntry *e)
{
    if (e == nullptr)
        return Cell_Unset;
    if (e->a == 0)
        return Cell_Off;
    if (kind_has_research(kind) && e->b == 0)
        return Cell_Researchable;
    return Cell_Available;
}

// Canonical (a, b) pair for a state, per the semantics documented in
// phase5/05-slice5-availability-grid.md.
void pair_for(int kind, CellState state, int &a, int &b)
{
    switch (state)
    {
    case Cell_Off:         a = 0; b = 0; break;
    case Cell_Researchable: a = 1; b = 0; break;
    default:               a = 1; b = kind_has_research(kind) ? 1 : 0; break;
    }
}

CellState next_state(int kind, CellState s)
{
    switch (s)
    {
    case Cell_Unset:       return Cell_Available;
    case Cell_Available:   return kind_has_research(kind) ? Cell_Researchable : Cell_Off;
    case Cell_Researchable: return Cell_Off;
    default:               return Cell_Unset;
    }
}

const char *label_of(CellState s)
{
    switch (s)
    {
    case Cell_Off:         return "Off";
    case Cell_Available:   return "Avail";
    case Cell_Researchable: return "Rsrch";
    default:               return "-";
    }
}

void set_cell(int kind, int player, int item, CellState state)
{
    AvailabilityEntry *e = script_setup_availability_find(s_values, kind, player, item);
    if (state == Cell_Unset)
    {
        if (e != nullptr)
            s_values.availability.erase(s_values.availability.begin() + (e - &s_values.availability[0]));
        return;
    }
    int a, b;
    pair_for(kind, state, a, b);
    if (e != nullptr)
    {
        e->a = a;
        e->b = b;
    }
    else
    {
        AvailabilityEntry ne = { kind, player, item, a, b };
        s_values.availability.push_back(ne);
    }
}

void draw_kind_table(int kind)
{
    const struct NamedCommand *desc = script_setup_availability_desc(kind);
    int columns = 2 + s_players; // name, ALL, P0..
    if (ImGui::BeginChild("##AvailScroll", ImVec2(0, -40)))
    {
        if (ImGui::BeginTable("##AvailTable", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("ALL");
            for (int p = 0; p < s_players; p++)
            {
                char h[8];
                snprintf(h, sizeof(h), "P%d", p);
                ImGui::TableSetupColumn(h);
            }
            ImGui::TableHeadersRow();
            std::vector<int> seen;
            for (int i = 0; desc != nullptr && desc[i].name != nullptr; i++)
            {
                int item = desc[i].num;
                if (item <= 0)
                    continue;
                bool dup = false;
                for (size_t k = 0; k < seen.size(); k++)
                    dup = dup || (seen[k] == item);
                if (dup)
                    continue; // alias name for an id already listed
                seen.push_back(item);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(desc[i].name);
                for (int c = 0; c < columns - 1; c++)
                {
                    int player = c - 1; // column 1 == ALL (-1)
                    ImGui::TableSetColumnIndex(c + 1);
                    ImGui::PushID(item * 16 + c);
                    CellState cur = state_of(kind, script_setup_availability_find(s_values, kind, player, item));
                    if (ImGui::Button(label_of(cur), ImVec2(56, 0)))
                        set_cell(kind, player, item, next_state(kind, cur));
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

} // namespace

void editor_dialogs_open_availability(void)
{
    s_players = editor_current_level_players();
    if (s_players < 1)
        s_players = 1;
    s_values = script_setup_parse(
        script_setup_extract_region(editor_current_level_script_text()), s_players,
        script_setup_level_version(editor_current_level_script_text()));
    s_show_availability = true;
}

void editor_availability_frame(void)
{
    if (!s_show_availability)
        return;
    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    bool open = s_show_availability;
    if (ImGui::Begin("Availability", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        editor_lua_override_banner();
        FeBodyText("Click a cell to cycle: - (unset) > Avail > Rsrch > Off. ALL applies to every player; a P column overrides it.");
        bool tabs = FeBeginTabBar("##AvailTabs");
        if (tabs)
        {
            static const char *const kTabNames[AvailKind_Count] = { "Creatures", "Rooms", "Spells", "Traps", "Doors" };
            for (int kind = 0; kind < AvailKind_Count; kind++)
            {
                if (FeTab(kTabNames[kind]))
                {
                    draw_kind_table(kind);
                    FeEndTab();
                }
            }
        }
        FeEndTabBar(tabs);

        if (FeButton("Apply", ImVec2(140, 0)))
        {
            // Re-parse the live block so the four Level Settings fields
            // (edited elsewhere) are preserved; only availability is ours.
            ManagedSetupValues current = script_setup_parse(
                script_setup_extract_region(editor_current_level_script_text()), s_players,
                script_setup_level_version(editor_current_level_script_text()));
            current.availability = s_values.availability;
            std::string body = script_setup_generate(current, s_players,
                script_setup_level_version(editor_current_level_script_text()));
            std::string script = script_setup_replace_region(editor_current_level_script_text(), body);
            editor_set_current_level_script_text(script.c_str());
            editor_mark_dirty();
        }
        ImGui::SameLine();
        if (FeButton("Close", ImVec2(140, 0)))
            open = false;
    }
    ImGui::End();
    s_show_availability = open;
}
