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
#include <filesystem>
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
void read_campaign_file(ContentCampaign &cc, const std::string &root)
{
    cc.cfg_file = content_resolve_location(root, std::string(content_kind_folder(cc.kind)) + "/" + cc.fname);
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

void list_from(std::vector<ContentCampaign> &out, const struct CampaignsList &clist, ContentKind kind, const std::string &root)
{
    for (uint64_t i = 0; i < clist.items_num; i++)
    {
        const struct GameCampaign &c = clist.items[i];
        ContentCampaign cc;
        cc.name = c.display_name[0] != '\0' ? c.display_name : c.name;
        if (cc.name.empty())
            cc.name = c.fname;
        cc.fname = c.fname;
        cc.kind = kind;
        cc.is_mappack = kind != ContentKind_Campaign;
        cc.cfg_dir = content_resolve_location(root, c.configs_location);
        cc.crtr_dir = content_resolve_location(root, c.creatures_location);
        cc.levels_dir = content_resolve_location(root, c.levels_location);
        read_campaign_file(cc, root);
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

const char *content_kind_folder(ContentKind kind)
{
    return kind == ContentKind_Campaign ? "campgns" : kind == ContentKind_FreePlay ? "levels" : "multiplayer";
}

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
    list_from(out, campaigns_list, ContentKind_Campaign, root);
    list_from(out, mappacks_list, ContentKind_FreePlay, root);
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

namespace {

// A ContentCampaign read straight from a .cfg file the game did not list.
bool from_file(ContentCampaign &cc, const std::string &root, ContentKind kind, const std::string &fname)
{
    cc.kind = kind;
    cc.is_mappack = kind != ContentKind_Campaign;
    cc.fname = fname;
    cc.listed = false;
    read_campaign_file(cc, root);
    std::ifstream f(cc.cfg_file, std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const ConfigContent c = read_config_content(ConfigDocument::parse(ss.str()), "campaign", false);
    const CfgContentSection *common = c.find_section("common");
    if (common == nullptr)
        return false;
    auto value = [&](const char *key) {
        const std::string *v = common->last_value(key);
        return v != nullptr ? *v : std::string();
    };
    auto trimmed = [](std::string v) {
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r'))
            v.pop_back();
        return v;
    };
    cc.name = trimmed(value("NAME"));
    if (cc.name.empty())
        cc.name = fname;
    cc.cfg_dir = content_resolve_location(root, trimmed(value("CONFIGS_LOCATION")));
    cc.crtr_dir = content_resolve_location(root, trimmed(value("CREATURES_LOCATION")));
    cc.levels_dir = content_resolve_location(root, trimmed(value("LEVELS_LOCATION")));
    if (kind == ContentKind_Campaign)
    {
        for (const char *key : {"SINGLE_LEVELS", "BONUS_LEVELS", "EXTRA_LEVELS"})
        {
            std::istringstream in(value(key));
            for (long long n; in >> n;)
                if (n > 0)
                    cc.levels.push_back((int64_t)n);
        }
        std::sort(cc.levels.begin(), cc.levels.end());
        cc.levels.erase(std::unique(cc.levels.begin(), cc.levels.end()), cc.levels.end());
    }
    return true;
}

} // namespace

std::vector<ContentCampaign> content_list_everything(void)
{
    std::vector<ContentCampaign> out;
    const std::string root = content_root();
    list_from(out, campaigns_list, ContentKind_Campaign, root);
    list_from(out, mappacks_list, ContentKind_FreePlay, root);
    list_from(out, mp_mappacks_list, ContentKind_Multiplayer, root);
    // The .cfg files the game did not list.
    for (ContentKind kind : {ContentKind_Campaign, ContentKind_FreePlay, ContentKind_Multiplayer})
    {
        std::error_code ec;
        std::vector<std::string> names;
        for (std::filesystem::directory_iterator it(content_resolve_location(root, content_kind_folder(kind)), ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == ".cfg")
                names.push_back(it->path().filename().string());
        std::sort(names.begin(), names.end());
        for (const std::string &n : names)
        {
            bool known = false;
            for (const ContentCampaign &c : out)
            {
                std::string a = c.fname, b = n;
                std::transform(a.begin(), a.end(), a.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                std::transform(b.begin(), b.end(), b.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                if (c.kind == kind && a == b)
                    known = true;
            }
            ContentCampaign cc;
            if (!known && from_file(cc, root, kind, n))
                out.push_back(std::move(cc));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const ContentCampaign &a, const ContentCampaign &b) { return a.kind < b.kind; });
    return out;
}
