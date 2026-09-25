/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_target.cpp
 *     See content_target.h.
 */
#include "pre_inc.h"
#include "content_target.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "cfgc_content.h"
#include "config.h"
#include "config_campaigns.h"
#include "config_keeperfx.h"
#include "post_inc.h"

/******************************************************************************/
namespace {

void add_levels(std::vector<int64_t> &out, const LevelNumber *list, uint64_t count)
{
    for (uint64_t i = 0; i < count; i++)
        if (list[i] > 0) // a bonus list keeps a 0 for "no bonus level here"
            out.push_back((int64_t)list[i]);
}

// Reads the campaign file for what the text editor needs: its [strings] lines and the NAME_ID of every level entry.
void read_campaign_file(ContentCampaign &cc, const std::string &root, bool mappack)
{
    cc.cfg_file = content_resolve_location(root, std::string(mappack ? "levels/" : "campgns/") + cc.fname);
    std::ifstream f(cc.cfg_file, std::ios::binary);
    if (!f)
        return;
    std::stringstream ss;
    ss << f.rdbuf();
    const ConfigContent c = read_config_content(ConfigDocument::parse(ss.str()), "campaign", false);
    if (const CfgContentSection *strings = c.find_section("strings"))
        for (const CfgField &field : strings->fields)
        {
            std::string lang = field.key;
            for (char &ch : lang)
                ch = (char)std::tolower((unsigned char)ch);
            if (!field.values.empty() && !field.values.back().empty())
                cc.strings[lang] = content_resolve_location(root, field.values.back());
        }
    for (const CfgContentSection &s : c.sections)
        if (s.basename == "map" && s.index >= 0)
            if (const std::string *id = s.last_value("NAME_ID"))
            {
                char label[32];
                snprintf(label, sizeof(label), "map%05lld", (long long)s.index);
                cc.name_ids[std::atoll(id->c_str())] = label;
            }
}

void list_from(std::vector<ContentCampaign> &out, const struct CampaignsList &clist, bool mappack, const std::string &root)
{
    for (uint64_t i = 0; i < clist.items_num; i++)
    {
        const struct GameCampaign &c = clist.items[i];
        ContentCampaign cc;
        cc.name = c.display_name[0] != '\0' ? c.display_name : c.name;
        if (cc.name.empty())
            cc.name = c.fname;
        cc.fname = c.fname;
        cc.is_mappack = mappack;
        cc.cfg_dir = content_resolve_location(root, c.configs_location);
        cc.crtr_dir = content_resolve_location(root, c.creatures_location);
        cc.levels_dir = content_resolve_location(root, c.levels_location);
        read_campaign_file(cc, root, mappack);
        add_levels(cc.levels, c.single_levels, c.single_levels_count);
        add_levels(cc.levels, c.multi_levels, c.multi_levels_count);
        add_levels(cc.levels, c.bonus_levels, c.bonus_levels_index);
        add_levels(cc.levels, c.extra_levels, c.extra_levels_index);
        add_levels(cc.levels, c.freeplay_levels, c.freeplay_levels_count);
        std::sort(cc.levels.begin(), cc.levels.end());
        cc.levels.erase(std::unique(cc.levels.begin(), cc.levels.end()), cc.levels.end());
        out.push_back(std::move(cc));
    }
}

} // namespace

std::string content_resolve_location(const std::string &root, const std::string &location)
{
    if (location.empty())
        return std::string();
    if (root.empty())
        return location;
    const char last = root[root.size() - 1];
    return (last == '/' || last == '\\') ? root + location : root + "/" + location;
}

std::string content_root(void)
{
    return install_info.inst_path[0] != '\0' ? std::string(install_info.inst_path) : std::string(keeper_runtime_directory);
}

std::vector<ContentCampaign> content_list_campaigns(void)
{
    std::vector<ContentCampaign> out;
    const std::string root = content_root();
    list_from(out, campaigns_list, false, root);
    list_from(out, mappacks_list, true, root);
    return out;
}

namespace {
std::string normalise_dir(std::string d)
{
    for (char &c : d)
        if (c == '\\')
            c = '/';
    while (d.size() > 1 && d[d.size() - 1] == '/')
        d.erase(d.size() - 1);
    // "./levels/x" and "levels/x" are the same folder.
    while (d.compare(0, 2, "./") == 0)
        d.erase(0, 2);
    return d;
}
}

const ContentCampaign *content_find_campaign_for_dir(const std::vector<ContentCampaign> &list, const std::string &dir)
{
    if (dir.empty())
        return nullptr;
    const std::string want = normalise_dir(dir);
    for (const ContentCampaign &c : list)
        if (!c.levels_dir.empty() && normalise_dir(c.levels_dir) == want)
            return &c;
    return nullptr;
}

ConfigTarget content_target_make(const std::string &base_dir, const std::string &base_crtr_dir,
    const ContentCampaign *camp, int64_t level_number)
{
    ConfigTarget t;
    t.base_dir = base_dir;
    t.base_crtr_dir = base_crtr_dir;
    if (camp != nullptr)
    {
        t.campaign_cfg_dir = camp->cfg_dir;
        t.campaign_crtr_dir = camp->crtr_dir;
        t.level_dir = camp->levels_dir;
        t.level_number = level_number;
    }
    return t;
}

ConfigTarget content_target_for_map(const std::string &base_dir, const std::string &base_crtr_dir,
    const std::string &level_dir, int64_t level_number)
{
    ConfigTarget t;
    t.base_dir = base_dir;
    t.base_crtr_dir = base_crtr_dir;
    t.level_dir = level_dir;
    t.level_number = level_number;
    return t;
}
