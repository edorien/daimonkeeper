/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_spells.cpp
 *     The Spell and Ability editor: a configuration of the shared entity window. See content_spells.h.
 */
#include "pre_inc.h"
#include "content_spells.h"
#include "content_entity.h"
#include "content_form.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
const std::vector<std::string> &spells_groups(const std::string &basename)
{
    static const std::map<std::string, std::vector<std::string>> g = {
        {"power", {"Cost and strength", "Casting", "Look and sound", "Advanced"}},
        {"spell", {"Effect", "Duration and aura", "Advanced"}},
        {"shot", {"Damage", "Hit rules", "Effects and sound", "Size and physics", "Advanced"}},
        {"special", {"Special"}},
        {"instance", {"Timing", "First person", "Targeting", "Look and sound", "Advanced"}},
    };
    static const std::vector<std::string> none;
    const auto it = g.find(basename);
    return it != g.end() ? it->second : none;
}

std::string spells_group_of(const std::string &basename, const std::string &key)
{
    const std::string k = form_lower(key);
    static const std::map<std::string, std::map<std::string, std::set<std::string>>> table = {
        {"power", {
            {"Cost and strength", {"cost", "power", "costformula", "duration", "cooldown"}},
            {"Casting", {"castability", "properties", "playerstate", "parentpower", "spell", "artifact", "creaturetype",
                         "effect", "usefunction", "castexpandfunc"}},
            {"Look and sound", {"nametextid", "tooltiptextid", "symbolsprites", "pointersprites", "paneltabindex",
                                "soundsamples", "soundplayed"}},
        }},
        {"spell", {
            {"Effect", {"shotmodel", "effectmodel", "damage", "damagefrequency", "spellpower", "selfcasted", "castatthing",
                        "summoncreature", "cleanseflags", "healingrecovery", "spellflags"}},
            {"Duration and aura", {"duration", "auraeffect", "auraduration", "aurafrequency", "countdown"}},
        }},
        {"shot", {
            {"Damage", {"damage", "areadamage", "speed", "maxrange", "health", "speeddeviation", "baseexperiencegain"}},
            {"Hit rules", {"hittype", "destroyonhit", "ismagical", "withstandhitagainst", "pushonhit", "targethitstopturns",
                           "firelogic", "updatelogic", "hitthingfunc", "periodical", "spelleffect"}},
            {"Effects and sound", {"firingsound", "firingsoundvariants", "shotsound", "shotsoundpriority", "hitcreatureeffect",
                                   "hitcreaturesound", "hitwalleffect", "hitwallsound", "hitdooreffect", "hitdoorsound",
                                   "hitheartseffect", "hithearteffect", "hitheartsound", "hitlavaeffect", "hitlavasound",
                                   "hitwatereffect", "hitwatersound", "dighiteffect", "dighitsound", "explosioneffects",
                                   "visualeffect", "visualeffectamount", "visualeffecthealth", "visualeffectspread",
                                   "effectmodel", "effectamount", "effectspacing", "bleedingeffect", "frozeneffect"}},
            {"Size and physics", {"size_xy", "size_yz", "size_z", "inertia", "bounceangle", "fallacceleration", "softlanding",
                                  "spread_xy", "spread_z", "animation", "animationsize", "animationtransparency", "lighting", "unshaded"}},
        }},
        {"special", {
            {"Special", {"artifact", "activationeffect", "speechplayed", "tooltiptextid", "value"}},
        }},
        {"instance", {
            {"Timing", {"time", "actiontime", "resettime"}},
            {"First person", {"fptime", "fpactiontime", "fpresettime", "fpinstantcast", "fpallowselfcastwhilefrozen",
                              "fpallowselfcastwhenchicken"}},
            {"Targeting", {"rangemin", "rangemax", "primarytarget", "properties", "postalpriority", "forcevisibility", "noanimationloop"}},
            {"Look and sound", {"tooltiptextid", "symbolsprites", "graphics"}},
        }},
    };
    const auto kind = table.find(basename);
    if (kind != table.end())
        for (const auto &grp : kind->second)
            if (grp.second.count(k))
                return grp.first;
    // Anything not listed: the last tab of the kind ("Advanced"; the special block has one tab only).
    const std::vector<std::string> &g = spells_groups(basename);
    return g.empty() ? std::string() : g.back();
}

/******************************************************************************/
namespace {

EntityEditor &editor()
{
    static EntityEditor e = [] {
        EntityConfig c;
        c.title = "Spell and Ability Editor";
        c.imgui_id = "##ContentSpells";
        c.kind = "magic";
        c.file_name = "magic.cfg";
        c.empty_text = "Pick a campaign or map pack to edit its magic.";
        c.plot_keys = {"cost", "power"}; // what a power costs and how strong it is at each level, as a curve
        c.read_only = {"usefunction", "castexpandfunc", "hitthingfunc", "firelogic", "updatelogic", "function", "validatesourcefunc",
            "validatetargetfunc", "searchtargetsfunc"};

        auto link = [&](const char *mode, const char *key, const char *group, const char *target, const char *title,
                        std::vector<std::string> show, const char *note) {
            EntityLink l;
            l.mode_basename = mode;
            l.key = key;
            l.group = group;
            l.kind = "magic";
            l.file_name = "magic.cfg";
            l.target_basename = target;
            l.title = title;
            l.show = std::move(show);
            l.note = note;
            c.links.push_back(l);
            return c.links.size() - 1;
        };
        // A power casts a spell; a spell fires a shot.
        link("power", "Spell", "Casting", "spell", "Casts", {"Duration", "Damage", "ShotModel", "SelfCasted"},
            "The spell's own numbers are on the Spells tab.");
        const size_t spell_shot = link("spell", "ShotModel", "Effect", "shot", "Fires", {"Damage", "Speed", "MaxRange", "HitType"},
            "The shot's numbers are on the Shots tab. A shot can be shared by several spells and traps.");

        auto mode = [&](const char *label, const char *basename) {
            EntityMode m;
            m.label = label;
            m.basename = basename;
            m.groups = spells_groups(basename);
            const std::string base = basename;
            m.group_of = [base](const std::string &key) { return spells_group_of(base, key); };
            return m;
        };
        EntityMode powers = mode("Powers", "power");
        powers.compare_keys = {"Cost[0]", "Cost[last]", "Power[0]", "Power[last]", "Duration", "Cooldown"};
        powers.unique_keys = {"PanelTabIndex"};
        powers.unique_group = "Look and sound";
        c.modes.push_back(powers);
        EntityMode spells = mode("Spells", "spell");
        spells.compare_keys = {"Duration", "Damage", "AuraDuration", "SpellPower"};
        spells.compare_linked.push_back({spell_shot, "Damage", "Shot damage"});
        c.modes.push_back(spells);
        EntityMode shots = mode("Shots", "shot");
        shots.compare_keys = {"Damage", "Speed", "MaxRange", "Health", "HitType"};
        c.modes.push_back(shots);
        EntityMode specials = mode("Specials", "special");
        specials.compare_keys = {"Value"};
        c.modes.push_back(specials);
        // Abilities are the creatures' instances: they live in creature.cfg, not magic.cfg (plan 06 §2).
        EntityMode abilities = mode("Abilities", "instance");
        abilities.kind = "creature";
        abilities.file_name = "creature.cfg";
        abilities.compare_keys = {"Time", "ActionTime", "ResetTime", "RangeMin", "RangeMax"};
        c.modes.push_back(abilities);

        // The function of an ability names what it does and its argument: a shot or a spell.
        auto arg_link = [&](const char *target, const char *title, const char *note) {
            EntityLink l;
            l.mode_basename = "instance";
            l.key = "Function";
            l.key_word = 1;
            l.optional = true;
            l.group = "Advanced";
            l.kind = "magic";
            l.file_name = "magic.cfg";
            l.target_basename = target;
            l.title = title;
            l.show = {"Damage", "Speed", "MaxRange", "Duration", "HitType"};
            l.note = note;
            c.links.push_back(l);
        };
        arg_link("shot", "Fires shot", "The shot's numbers are on the Shots tab.");
        arg_link("spell", "Casts spell", "The spell's numbers are on the Spells tab.");
        return EntityEditor(c);
    }();
    return e;
}

} // namespace

void content_spells_open(bool map_host)
{
    editor().open(map_host);
}

void content_spells_frame(void)
{
    editor().frame();
}

bool content_spells_is_open(void)
{
    return editor().is_open();
}

void content_spells_show(int mode, const char *group)
{
    editor().show((size_t)mode, group != nullptr ? group : "");
}
