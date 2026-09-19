/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_command_browser.cpp
 *     Script > Commands window.
 * @par Purpose:
 *     See editor_command_browser.h.
 * @par Comment:
 *     The command list is read from the engine's own command table
 *     (command_desc[]) so it can never drift from what the script parser
 *     accepts; grouping, "classic" tagging and summaries come from the pure
 *     tables in editor_script_commands.cpp (which follow the original 1993
 *     Editor manual, chapter 5.2). The Values tab mirrors the manual's
 *     variable groups (5.2.1: players, variables, comparisons, action
 *     points, creature / room / door and trap / spell names), read from
 *     the live name tables so custom creatures, rooms and spells appear.
 *     It is a plain, non-modal window on purpose: it stays open beside the
 *     script editor so several things can be inserted in a row.
 */
#include "pre_inc.h"
#include "editor_command_browser.h"
#include "editor_script.h"
#include "editor_script_commands.h"
#include "editor_script_names.h"
#include "kfx_editor.h"
#include "frontgui_widgets.h"
#include "lvl_script_lib.h"
#include "lvl_script_commands.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

struct CommandEntry
{
    std::string name;
    std::string signature;
    std::string tmpl;
    std::string summary;
    int group;
    bool classic;
};

struct ValueGroup
{
    std::string title;
    std::vector<std::string> names;
};

bool s_show = false;
bool s_classic_only = false;
char s_filter[64];
std::vector<CommandEntry> s_commands;
std::vector<ValueGroup> s_values;
int s_group_sel = -1;   // -1 = all groups
int s_command_sel = -1; // index into s_commands
int s_value_group_sel = 0;
std::string s_status;

bool contains_nocase(const std::string &hay, const char *needle)
{
    if (needle[0] == '\0')
        return true;
    size_t n = std::strlen(needle);
    for (size_t i = 0; i + n <= hay.size(); i++)
    {
        size_t j = 0;
        while (j < n && std::tolower((unsigned char)hay[i + j]) == std::tolower((unsigned char)needle[j]))
            j++;
        if (j == n)
            return true;
    }
    return false;
}

void build_catalog()
{
    s_commands.clear();
    for (int i = 0; command_desc[i].textptr != NULL; i++)
    {
        CommandEntry e;
        e.name = command_desc[i].textptr;
        e.signature = editor_script_command_signature(e.name, command_desc[i].args);
        e.tmpl = editor_script_command_template(e.name, command_desc[i].args);
        e.summary = editor_script_command_summary(e.name);
        e.group = editor_script_command_group(e.name);
        e.classic = editor_script_command_is_classic(e.name);
        s_commands.push_back(e);
    }
    // Classic commands first within a group, then alphabetical.
    std::stable_sort(s_commands.begin(), s_commands.end(), [](const CommandEntry &a, const CommandEntry &b) {
        if (a.group != b.group)
            return a.group < b.group;
        if (a.classic != b.classic)
            return a.classic;
        return a.name < b.name;
    });

    s_values.clear();
    for (const ScriptNameGroup &g : editor_script_collect_name_groups())
    {
        ValueGroup v;
        v.title = g.title;
        v.names = g.names;
        s_values.push_back(v);
    }
}

bool command_visible(const CommandEntry &e)
{
    if (s_group_sel >= 0 && e.group != s_group_sel)
        return false;
    if (s_classic_only && !e.classic)
        return false;
    return contains_nocase(e.name, s_filter) || contains_nocase(e.summary, s_filter);
}

void insert_command(const CommandEntry &e)
{
    bool into_editor = editor_script_insert_command_at_cursor(e.tmpl.c_str());
    s_status = into_editor
        ? "Inserted at the cursor in the script editor -- press Apply there to keep it."
        : "Script editor is closed: appended to the end of the script.";
}

void draw_commands_tab()
{
    int group_counts[ScrGroup_Count] = {};
    int visible_total = 0;
    for (const CommandEntry &e : s_commands)
    {
        if (s_classic_only && !e.classic)
            continue;
        group_counts[e.group]++;
        visible_total++;
    }

    if (ImGui::BeginChild("##CmdGroups", ImVec2(210, -130), ImGuiChildFlags_Borders))
    {
        char label[96];
        snprintf(label, sizeof(label), "All (%d)", visible_total);
        if (ImGui::Selectable(label, s_group_sel == -1))
            s_group_sel = -1;
        for (int g = 0; g < ScrGroup_Count; g++)
        {
            if (group_counts[g] == 0)
                continue;
            snprintf(label, sizeof(label), "%s (%d)", editor_script_group_title(g), group_counts[g]);
            if (ImGui::Selectable(label, s_group_sel == g))
                s_group_sel = g;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (ImGui::BeginChild("##CmdList", ImVec2(0, -130), ImGuiChildFlags_Borders))
    {
        int last_group = -2;
        for (size_t i = 0; i < s_commands.size(); i++)
        {
            const CommandEntry &e = s_commands[i];
            if (!command_visible(e))
                continue;
            if (s_group_sel == -1 && e.group != last_group)
            {
                ImGui::SeparatorText(editor_script_group_title(e.group));
                last_group = e.group;
            }
            std::string row = e.signature + (e.classic ? "" : "   [KFX]") + "##cmd" + std::to_string(i);
            if (ImGui::Selectable(row.c_str(), s_command_sel == (int)i, ImGuiSelectableFlags_AllowDoubleClick))
            {
                s_command_sel = (int)i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    insert_command(e);
            }
        }
    }
    ImGui::EndChild();

    // Detail pane for the selected command.
    if ((s_command_sel >= 0) && (s_command_sel < (int)s_commands.size()))
    {
        const CommandEntry &e = s_commands[s_command_sel];
        FeSubheading(e.name.c_str());
        ImGui::TextWrapped("%s", e.summary.empty()
            ? (e.classic ? "" : "KeeperFX command -- no description bundled; see the KeeperFX script documentation.")
            : e.summary.c_str());
        FeCaption("Inserts:");
        ImGui::SameLine();
        ImGui::TextUnformatted(e.tmpl.c_str());
        if (FeButton("Insert at cursor", ImVec2(180, 0)))
            insert_command(e);
    }
    else
    {
        FeBodyText("Select a command (double-click inserts it).");
    }
}

void draw_values_tab()
{
    bool can_insert = editor_script_is_open();
    if (!can_insert)
        FeCaption("Open Script > Edit Script to insert values at its cursor.");

    if (ImGui::BeginChild("##ValGroups", ImVec2(210, 0), ImGuiChildFlags_Borders))
    {
        for (size_t g = 0; g < s_values.size(); g++)
        {
            char label[96];
            snprintf(label, sizeof(label), "%s (%zu)", s_values[g].title.c_str(), s_values[g].names.size());
            if (ImGui::Selectable(label, s_value_group_sel == (int)g))
                s_value_group_sel = (int)g;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (ImGui::BeginChild("##ValList", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
        if ((s_value_group_sel >= 0) && (s_value_group_sel < (int)s_values.size()))
        {
            const ValueGroup &g = s_values[s_value_group_sel];
            for (size_t i = 0; i < g.names.size(); i++)
            {
                if (!contains_nocase(g.names[i], s_filter))
                    continue;
                std::string row = g.names[i] + "##val" + std::to_string(i);
                ImGui::BeginDisabled(!can_insert);
                if (ImGui::Selectable(row.c_str(), false))
                {
                    editor_script_insert_token_at_cursor(g.names[i].c_str());
                    s_status = "Inserted " + g.names[i] + " -- press Apply in the script editor to keep it.";
                }
                ImGui::EndDisabled();
            }
        }
    }
    ImGui::EndChild();
}

} // namespace

extern "C" void editor_dialogs_open_command_browser(void)
{
    build_catalog();
    s_command_sel = -1;
    s_status.clear();
    s_show = true;
}

extern "C" int editor_command_browser_command_count(void)
{
    build_catalog();
    return (int)s_commands.size();
}

extern "C" int editor_command_browser_unclassified(char *out, int out_size)
{
    build_catalog();
    std::string names;
    int count = 0;
    for (const CommandEntry &e : s_commands)
    {
        if (e.group != ScrGroup_Other)
            continue;
        count++;
        if (!names.empty())
            names += ",";
        names += e.name;
    }
    if (out_size > 0)
    {
        std::strncpy(out, names.c_str(), (size_t)out_size - 1);
        out[out_size - 1] = '\0';
    }
    return count;
}

extern "C" void editor_command_browser_frame(void)
{
    if (!s_show)
        return;

    ImGui::SetNextWindowSize(ImVec2(760, 540), ImGuiCond_FirstUseEver);
    bool open = s_show;
    if (ImGui::Begin("Script Commands", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::SetNextItemWidth(240);
        ImGui::InputTextWithHint("##CmdFilter", "Filter...", s_filter, sizeof(s_filter));
        ImGui::SameLine();
        FeCheckbox("Classic commands only", &s_classic_only);

        bool tabs = FeBeginTabBar("##CmdTabs");
        if (tabs)
        {
            if (FeTab("Commands"))
            {
                draw_commands_tab();
                FeEndTab();
            }
            if (FeTab("Values"))
            {
                draw_values_tab();
                FeEndTab();
            }
        }
        FeEndTabBar(tabs);
        if (!s_status.empty())
            FeCaption(s_status.c_str());
    }
    ImGui::End();
    s_show = open;
}
