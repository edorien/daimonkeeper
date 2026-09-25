/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_trapdoor.cpp
 *     The Trap and Door editor window. See content_trapdoor.h.
 */
#include "pre_inc.h"
#include "content_trapdoor.h"
#include "content_entity.h"
#include "content_form.h"
#include "frontgui_widgets.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
const std::vector<std::string> &trapdoor_groups(void)
{
    static const std::vector<std::string> g = {"Build", "Behaviour", "Placement", "Look & sound", "Advanced"};
    return g;
}

std::string trapdoor_group_of(bool is_door, const std::string &key)
{
    const std::string k = form_lower(key);
    static const std::set<std::string> build = {"manufacturelevel", "manufacturerequired", "sellingvalue", "unsellable", "crate", "paneltabindex"};
    static const std::set<std::string> trap_behaviour = {"triggertype", "activationtype", "effecttype", "shots", "timebetweenshots",
        "initialdelay", "activationlevel", "health", "hittype", "destructible", "slappable", "hidden", "detectinvisible",
        "triggeralarm", "unstable", "removeoncedepleted", "destroyedeffect"};
    static const std::set<std::string> door_behaviour = {"health", "openspeed", "slabkind", "properties"};
    static const std::set<std::string> placement = {"placeonbridge", "placeonsubtile", "instantplacement"};
    static const std::set<std::string> look = {"nametextid", "tooltiptextid", "symbolsprites", "pointersprites", "placesound", "triggersound"};
    if (build.count(k))
        return "Build";
    if ((is_door ? door_behaviour : trap_behaviour).count(k))
        return "Behaviour";
    if (placement.count(k))
        return "Placement";
    if (look.count(k))
        return "Look & sound";
    return "Advanced";
}

/******************************************************************************/
namespace {

EntityEditor &editor()
{
    static EntityEditor e = [] {
        EntityConfig c;
        c.title = "Trap and Door Editor";
        c.imgui_id = "##ContentTrapDoor";
        c.kind = "trapdoor";
        c.file_name = "trapdoor.cfg";
        c.empty_text = "Pick a campaign or map pack to edit its traps and doors.";
        c.read_only = {"activationluafunc", "updatefunction"};
        EntityLink shot;
        shot.mode_basename = "trap";
        shot.key = "EffectType";
        shot.group = "Behaviour";
        shot.kind = "magic";
        shot.file_name = "magic.cfg";
        shot.target_basename = "shot";
        shot.title = "Fires";
        shot.show = {"Damage", "Speed", "Health", "HitType"};
        shot.note = "The damage lives in the shot: change it in the Spell and Ability editor.";
        c.links.push_back(shot);
        for (int door = 0; door < 2; door++)
        {
            EntityMode m;
            m.label = door ? "Doors" : "Traps";
            m.basename = door ? "door" : "trap";
            m.groups = trapdoor_groups();
            m.group_of = [door](const std::string &key) { return trapdoor_group_of(door != 0, key); };
            m.unique_keys = {"PanelTabIndex"};
            m.unique_group = "Build";
            if (door)
                m.compare_keys = {"ManufactureRequired", "ManufactureLevel", "Health", "OpenSpeed", "SellingValue"};
            else
            {
                m.compare_keys = {"ManufactureRequired", "ManufactureLevel", "Health", "Shots", "TimeBetweenShots", "SellingValue"};
                m.compare_linked.push_back({0, "Damage", "Shot damage"});
            }
            c.modes.push_back(m);
        }
        return EntityEditor(c);
    }();
    return e;
}

} // namespace

void content_trapdoor_open(bool map_host)
{
    editor().open(map_host);
}

void content_trapdoor_frame(void)
{
    editor().frame();
}

bool content_trapdoor_is_open(void)
{
    return editor().is_open();
}

void content_trapdoor_show(bool doors, const char *group)
{
    editor().show(doors ? 1 : 0, group != nullptr ? group : "");
}
