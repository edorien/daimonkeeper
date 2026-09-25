/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_campaign_ops.cpp
 *     See content_campaign_ops.h.
 */
#include "pre_inc.h"
#include "content_campaign_ops.h"
#include "content_target.h"
#include "cfgc_writebatch.h"
#include "cfgc_campaign_edit.h"

#include "config_campaigns.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "post_inc.h"

/******************************************************************************/
namespace {

const ContentCampaign *find(const std::vector<ContentCampaign> &all, const std::string &fname)
{
    for (const ContentCampaign &c : all)
        if (!c.is_mappack && c.fname == fname)
            return &c;
    return nullptr;
}

std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

int64_t content_campaign_next_level(const std::string &campaign_fname, CampaignListKind kind)
{
    const std::vector<ContentCampaign> all = content_list_campaigns();
    const ContentCampaign *c = find(all, campaign_fname);
    if (c == nullptr)
        return 1;
    const CampaignLevels lv = cfgc_read_levels(read_config_content(ConfigDocument::parse(slurp(c->cfg_file)), "campaign", false));
    // A listed single level whose map does not exist yet (a new campaign's level 1) is the next one to fill.
    if (kind == CampList_Single)
        for (int64_t n : lv.single)
        {
            char name[32];
            snprintf(name, sizeof(name), "/map%05lld.slb", (long long)n);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(c->levels_dir + name, ec))
                return n;
        }
    return cfgc_next_level_number(lv, kind);
}

bool content_campaign_register_level(const std::string &campaign_fname, int64_t n, CampaignListKind kind,
    const std::string &name, std::string *error)
{
    const std::vector<ContentCampaign> all = content_list_campaigns();
    const ContentCampaign *c = find(all, campaign_fname);
    if (c == nullptr)
    {
        if (error != nullptr)
            *error = "The campaign " + campaign_fname + " is not in the campaign list.";
        return false;
    }
    bool changed = false;
    const std::string text = cfgc_campaign_add_level(slurp(c->cfg_file), n, kind, name, &changed);
    if (!changed)
        return true;
    WriteBatch batch;
    batch.put(c->cfg_file, text);
    if (!batch.commit(error))
        return false;
    // The lists are read at start-up; read them again so the level shows in the game and the other editors now.
    load_campaigns_list(&campaigns_list, FGrp_Campgn, "campaigns", "campgn_order.txt");
    return true;
}

bool content_campaign_id_free(const std::string &id)
{
    if (id.empty())
        return false;
    std::string want = id;
    std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    for (const ContentCampaign &c : content_list_campaigns())
    {
        std::string have = c.fname;
        std::transform(have.begin(), have.end(), have.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        if (!c.is_mappack && have == want + ".cfg")
            return false;
    }
    std::error_code ec;
    return !std::filesystem::exists(content_root() + "/campgns/" + id + ".cfg", ec);
}

std::string content_campaign_default_land_source(void)
{
    namespace fs = std::filesystem;
    const std::string base = content_root() + "/campgns/";
    auto usable = [](const std::string &d) {
        std::error_code ec;
        return fs::is_regular_file(d + "/rgmap00.raw", ec) && fs::is_regular_file(d + "/rgmap00.pal", ec) && fs::is_regular_file(d + "/viframe00.dat", ec);
    };
    if (usable(base + "keeporig_lnd"))
        return base + "keeporig_lnd";
    std::error_code ec;
    std::vector<std::string> found;
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec) && it->path().filename().string().size() > 4 && it->path().filename().string().compare(it->path().filename().string().size() - 4, 4, "_lnd") == 0 && usable(it->path().string()))
            found.push_back(it->path().string());
    std::sort(found.begin(), found.end());
    return found.empty() ? std::string() : found[0];
}

bool content_campaign_create(const std::string &name, const std::string &id, const std::string &human_player, bool own_config,
    std::string *error)
{
    if (!content_campaign_id_free(id))
    {
        if (error != nullptr)
            *error = "A campaign called " + id + " already exists.";
        return false;
    }
    const std::string base = content_root() + "/campgns/";
    std::error_code ec;
    std::vector<std::string> dirs = {base + id};
    if (own_config)
    {
        dirs.push_back(base + id + "_cfg");
        dirs.push_back(base + id + "_crtr");
    }
    for (const std::string &d : dirs)
    {
        std::filesystem::create_directories(d, ec);
        if (!std::filesystem::is_directory(d, ec))
        {
            if (error != nullptr)
                *error = "Could not create the folder " + d + ".";
            return false;
        }
    }
    // The land screen needs images in the campaign's own land folder: start it with a copy of the default ones.
    const std::string land_source = content_campaign_default_land_source();
    WriteBatch batch;
    if (!land_source.empty())
    {
        for (const char *f : {"rgmap00.raw", "rgmap00.pal", "viframe00.dat"})
        {
            std::ifstream in(land_source + "/" + f, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            batch.put(base + id + "_lnd/" + f, ss.str());
        }
    }
    batch.put(base + id + ".cfg", cfgc_new_campaign_text(name, id, human_player, own_config, !land_source.empty()));
    if (!batch.commit(error))
        return false;
    load_campaigns_list(&campaigns_list, FGrp_Campgn, "campaigns", "campgn_order.txt");
    return true;
}

static std::vector<std::string> menu_order(void)
{
    std::vector<std::string> out;
    for (const ContentCampaign &c : content_list_campaigns())
        if (!c.is_mappack)
            out.push_back(c.fname);
    return out;
}

bool content_campaign_menu_position(const std::string &campaign_fname, size_t *position, size_t *count)
{
    const std::vector<std::string> order = menu_order();
    for (size_t i = 0; i < order.size(); i++)
        if (order[i] == campaign_fname)
        {
            if (position != nullptr)
                *position = i;
            if (count != nullptr)
                *count = order.size();
            return true;
        }
    return false;
}

bool content_campaign_move_in_menu(const std::string &campaign_fname, int delta, std::string *error)
{
    const std::vector<std::string> order = menu_order();
    size_t at = order.size();
    for (size_t i = 0; i < order.size(); i++)
        if (order[i] == campaign_fname)
            at = i;
    if (at == order.size())
    {
        if (error != nullptr)
            *error = "The campaign is not in the campaign list.";
        return false;
    }
    const std::string path = content_root() + "/campgns/campgn_order.txt";
    WriteBatch batch;
    batch.put(path, cfgc_order_file_text(slurp(path), cfgc_move_in_order(order, at, delta)));
    if (!batch.commit(error))
        return false;
    load_campaigns_list(&campaigns_list, FGrp_Campgn, "campaigns", "campgn_order.txt");
    return true;
}
