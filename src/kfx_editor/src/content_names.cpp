/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_names.cpp
 *     See content_names.h.
 */
#include "pre_inc.h"
#include "content_names.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include "post_inc.h"

/******************************************************************************/
namespace {

bool read_file(const std::string &path, std::string &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void collect_file(const ConfigTarget &target, CfgLayer upto, const char *kind, const char *file, bool creature_list,
    CfgNameSets &out)
{
    for (int l = 0; l <= (int)upto; l++)
    {
        std::string bytes;
        const std::string p = target.path_for(file, (CfgLayer)l);
        if (p.empty() || !read_file(p, bytes))
            continue;
        const ConfigContent c = read_config_content(ConfigDocument::parse(bytes), kind, l != CfgLayer_Base);
        cfgc_collect_names(c, out); // instances, jobs, anger jobs, attack preferences of creature.cfg, or the blocks' names
        if (creature_list)
            cfgc_collect_creature_names(c, out); // the layer's creature list replaces the one beneath
    }
}

} // namespace

CfgNameSets content_collect_names(const ConfigTarget &target, CfgLayer upto)
{
    CfgNameSets names;
    collect_file(target, upto, "trapdoor", "trapdoor.cfg", false, names);
    collect_file(target, upto, "objects", "objects.cfg", false, names);
    collect_file(target, upto, "terrain", "terrain.cfg", false, names);
    collect_file(target, upto, "magic", "magic.cfg", false, names);
    collect_file(target, upto, "creature", "creature.cfg", true, names);
    return names;
}

std::vector<std::string> content_registry_list(const CfgNameSets &names, const std::string &registry)
{
    std::vector<std::string> out;
    const std::set<std::string> *set = names.find(registry);
    if (set != nullptr)
        out.assign(set->begin(), set->end());
    return out;
}
