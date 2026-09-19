/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_names.cpp
 *     Value-name tables for the script tools.
 * @par Purpose:
 *     See editor_script_names.h.
 */
#include "pre_inc.h"
#include "editor_script_names.h"
#include "lvl_script_lib.h"
#include "lvl_script_commands.h"
#include "lvl_script_conditions.h"
#include "config_creature.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "config_magic.h"
#include "actionpt.h"
#include <algorithm>
#include "post_inc.h"

namespace {

void add_names(ScriptNameGroup &g, const struct NamedCommand *table, int max_entries)
{
    for (int i = 0; (i < max_entries) && (table[i].name != NULL); i++)
        if (table[i].name[0] != '\0')
            g.names.push_back(table[i].name);
}

} // namespace

std::vector<ScriptNameGroup> editor_script_collect_name_groups()
{
    std::vector<ScriptNameGroup> groups;
    {
        ScriptNameGroup g;
        g.title = "Players";
        add_names(g, player_desc, 32);
        groups.push_back(g);
    }
    {
        ScriptNameGroup g;
        g.title = "Variables";
        add_names(g, variable_desc, 256);
        groups.push_back(g);
    }
    {
        ScriptNameGroup g;
        g.title = "Flags & timers";
        add_names(g, flag_desc, 16);
        add_names(g, timer_desc, 16);
        groups.push_back(g);
    }
    {
        ScriptNameGroup g;
        g.title = "Comparisons";
        add_names(g, comparison_desc, 16);
        groups.push_back(g);
    }
    {
        ScriptNameGroup heroes, evil;
        heroes.title = "Hero creatures";
        evil.title = "Evil creatures";
        for (int i = 0; (i < CREATURE_TYPES_MAX) && (creature_desc[i].name != NULL); i++)
        {
            if (creature_desc[i].num <= 0)
                continue;
            struct CreatureModelConfig *crconf = creature_stats_get((ThingModel)creature_desc[i].num);
            bool is_evil = (crconf != NULL) && ((crconf->model_flags & CMF_IsEvil) != 0);
            (is_evil ? evil : heroes).names.push_back(creature_desc[i].name);
        }
        groups.push_back(heroes);
        groups.push_back(evil);
    }
    {
        ScriptNameGroup g;
        g.title = "Rooms";
        add_names(g, room_desc, TERRAIN_ITEMS_MAX);
        groups.push_back(g);
    }
    {
        ScriptNameGroup g;
        g.title = "Doors";
        add_names(g, door_desc, TRAPDOOR_TYPES_MAX);
        groups.push_back(g);
    }
    {
        ScriptNameGroup g;
        g.title = "Traps";
        add_names(g, trap_desc, TRAPDOOR_TYPES_MAX);
        groups.push_back(g);
    }
    {
        ScriptNameGroup g;
        g.title = "Spells & powers";
        add_names(g, power_desc, 256);
        groups.push_back(g);
    }
    {
        // Numbers of the action points placed on this level right now.
        ScriptNameGroup g;
        g.title = "Action points (this level)";
        g.per_level = true;
        for (ActionPointId i = 1; i < ACTN_POINTS_COUNT; i++)
        {
            const struct ActionPoint *apt = action_point_get(i);
            if (action_point_exists(apt))
                g.names.push_back(std::to_string((int)apt->num));
        }
        std::sort(g.names.begin(), g.names.end(), [](const std::string &a, const std::string &b) {
            return (a.size() != b.size()) ? a.size() < b.size() : a < b;
        });
        groups.push_back(g);
    }
    return groups;
}
