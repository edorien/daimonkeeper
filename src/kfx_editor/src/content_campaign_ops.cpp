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
            if (!cfgc_file_exists_ci(c->levels_dir + name))
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
    content_campaign_rescan_lists();
    return true;
}

bool content_campaign_id_free(const std::string &id, ContentKind kind)
{
    if (id.empty())
        return false;
    std::string want = id;
    std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    for (const ContentCampaign &c : content_list_everything())
    {
        std::string have = c.fname;
        std::transform(have.begin(), have.end(), have.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        if (c.kind == kind && have == want + ".cfg")
            return false;
    }
    std::error_code ec;
    return !std::filesystem::exists(content_root() + "/" + content_kind_folder(kind) + "/" + id + ".cfg", ec);
}

/** A campaign land-view folder to copy the default images from (`rgmap00` + `viframe00`): keeporig's when present, else the first
 *  any `campgns/..._lnd` folder that has them; empty when there is none. */
static std::string content_campaign_default_land_source(void)
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
    content_campaign_rescan_lists();
    return true;
}

static std::vector<std::string> menu_order(ContentKind kind)
{
    std::vector<std::string> out;
    for (const ContentCampaign &c : content_list_everything())
        if (c.kind == kind && c.listed)
            out.push_back(c.fname);
    return out;
}

static std::string order_file(ContentKind kind)
{
    return content_root() + "/" + content_kind_folder(kind) + "/"
        + (kind == ContentKind_Campaign ? "campgn_order.txt" : kind == ContentKind_FreePlay ? "mappck_order.txt" : "mp_mappck_order.txt");
}

bool content_campaign_menu_position(const std::string &campaign_fname, size_t *position, size_t *count, ContentKind kind)
{
    const std::vector<std::string> order = menu_order(kind);
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

bool content_campaign_move_in_menu(const std::string &campaign_fname, int delta, std::string *error, ContentKind kind)
{
    const std::vector<std::string> order = menu_order(kind);
    size_t at = order.size();
    for (size_t i = 0; i < order.size(); i++)
        if (order[i] == campaign_fname)
            at = i;
    if (at == order.size())
    {
        if (error != nullptr)
            *error = "It is not in the game's list.";
        return false;
    }
    const std::string path = order_file(kind);
    WriteBatch batch;
    batch.put(path, cfgc_order_file_text(slurp(path), cfgc_move_in_order(order, at, delta)));
    if (!batch.commit(error))
        return false;
    content_campaign_rescan_lists();
    return true;
}

void content_campaign_rescan_lists(void)
{
    load_campaigns_list(&campaigns_list, FGrp_Campgn, "campaigns", "campgn_order.txt");
    load_campaigns_list(&mappacks_list, FGrp_VarLevels, "mappacks", "mappck_order.txt");
    load_campaigns_list(&mp_mappacks_list, FGrp_MpLevels, "multiplayer mappacks", "mp_mappck_order.txt");
}

static std::string lower_str(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::vector<PackLevel> content_pack_levels(const ContentCampaign &pack)
{
    std::vector<PackLevel> out;
    for (int64_t n : cfgc_level_numbers_in_dir(pack.levels_dir))
    {
        PackLevel l;
        l.number = n;
        const std::vector<std::string> files = cfgc_level_files(pack.levels_dir, n);
        l.files = files.size();
        for (const std::string &f : files)
        {
            const std::string ext = lower_str(f.substr(f.rfind('.') == std::string::npos ? f.size() : f.rfind('.')));
            if (ext == ".lof")
            {
                const ConfigDocument d = ConfigDocument::parse(slurp(pack.levels_dir + "/" + f));
                for (const CfgLine &ln : d.lines())
                    if (ln.kind == CfgLine_Key)
                    {
                        const std::string k = lower_str(ln.name);
                        std::string v = ln.value;
                        while (!v.empty() && (v.back() == ' ' || v.back() == '\r'))
                            v.pop_back();
                        if (k == "name_text" && !v.empty())
                            l.name = v;
                        else if (k == "players")
                            l.players = v;
                        else if (k == "author")
                            l.author = v;
                    }
            }
            else if (ext == ".lif" && l.name.empty())
            {
                // "number, name" (the classic level info line)
                std::istringstream in(slurp(pack.levels_dir + "/" + f));
                for (std::string line; std::getline(in, line);)
                {
                    const size_t comma = line.find(',');
                    if (comma != std::string::npos && !line.empty() && line[0] != ';')
                    {
                        l.name = line.substr(comma + 1);
                        while (!l.name.empty() && (l.name.front() == ' ' || l.name.back() == '\r' || l.name.back() == ' '))
                            l.name.erase(l.name.front() == ' ' ? 0 : l.name.size() - 1, 1);
                        break;
                    }
                }
            }
        }
        out.push_back(std::move(l));
    }
    return out;
}

int64_t content_pack_next_level(const ContentCampaign &pack)
{
    const std::vector<int64_t> have = cfgc_level_numbers_in_dir(pack.levels_dir);
    int64_t n = 1;
    while (std::find(have.begin(), have.end(), n) != have.end())
        n++;
    return n;
}

bool content_pack_create(const std::string &name, const std::string &id, ContentKind kind, const std::string &human_player, bool own_config,
    std::string *error)
{
    if (kind == ContentKind_Campaign || !content_campaign_id_free(id, kind))
    {
        if (error != nullptr)
            *error = "A pack called " + id + " already exists.";
        return false;
    }
    const std::string folder = content_kind_folder(kind);
    const std::string base = content_root() + "/" + folder + "/";
    std::vector<std::string> dirs = {base + id};
    if (own_config)
    {
        dirs.push_back(base + id + "_cfg");
        dirs.push_back(base + id + "_crtr");
    }
    std::error_code ec;
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
    WriteBatch batch;
    batch.put(base + id + ".cfg", cfgc_new_pack_text(name, id, folder, human_player, own_config));
    if (!batch.commit(error))
        return false;
    content_campaign_rescan_lists();
    return true;
}

bool content_campaign_copy_level(const ContentCampaign &src, int64_t from, const ContentCampaign &dst, int64_t to, CampaignListKind list,
    bool remove_from_source, std::string *error)
{
    if (src.levels_dir.empty() || dst.levels_dir.empty())
    {
        if (error != nullptr)
            *error = "The campaign or pack has no levels folder.";
        return false;
    }
    if (src.cfg_file == dst.cfg_file && from == to)
    {
        if (error != nullptr)
            *error = "Give the copy another number.";
        return false;
    }
    WriteBatch batch;
    if (!cfgc_plan_level_copy(src.levels_dir, from, dst.levels_dir, to, batch, error))
        return false;
    // The name the new entry gets: the source's entry, else the level's own name.
    std::string name;
    if (src.kind == ContentKind_Campaign)
    {
        const ConfigContent c = read_config_content(ConfigDocument::parse(slurp(src.cfg_file)), "campaign", false);
        if (const CfgContentSection *e = c.find_section("map", from))
            if (const std::string *v = e->last_value("NAME_TEXT"))
                name = *v;
    }
    else
        for (const PackLevel &l : content_pack_levels(src))
            if (l.number == from)
                name = l.name;
    if (name.empty())
        name = "Level " + std::to_string(to);
    // Files of the campaign that change: the target's lists and entry, and (a move) the source's. They can be one file.
    std::string dst_text, src_text;
    bool dst_changes = false, src_changes = false;
    if (dst.kind == ContentKind_Campaign)
    {
        dst_text = cfgc_campaign_add_level(slurp(dst.cfg_file), to, list, name, &dst_changes);
    }
    if (remove_from_source && src.kind == ContentKind_Campaign)
    {
        src_text = cfgc_campaign_remove_level(src.cfg_file == dst.cfg_file && dst_changes ? dst_text : slurp(src.cfg_file), from, true, &src_changes);
        if (src.cfg_file == dst.cfg_file)
        {
            dst_text = src_text;
            dst_changes = dst_changes || src_changes;
            src_changes = false;
        }
    }
    if (dst_changes)
        batch.put(dst.cfg_file, dst_text);
    if (src_changes)
        batch.put(src.cfg_file, src_text);
    if (!batch.commit(error))
        return false;
    content_campaign_rescan_lists();
    return true;
}
