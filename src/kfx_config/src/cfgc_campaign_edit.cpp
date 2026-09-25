/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_campaign_edit.cpp
 *     See cfgc_campaign_edit.h.
 */
#include "pre_inc.h"
#include "cfgc_campaign_edit.h"
#include "cfgc_campaign_levels.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include "post_inc.h"

namespace fs = std::filesystem;

/******************************************************************************/
std::string cfgc_own_location(const std::string &location, const std::string &cfg_fname, const std::string &suffix)
{
    std::string stem = cfg_fname;
    const size_t dot = stem.rfind('.');
    if (dot != std::string::npos)
        stem.erase(dot);
    std::string loc = location;
    for (char &c : loc)
        if (c == '\\')
            c = '/';
    while (!loc.empty() && loc.back() == '/')
        loc.pop_back();
    const size_t slash = loc.rfind('/');
    const std::string parent = slash == std::string::npos ? std::string() : loc.substr(0, slash + 1);
    return parent + stem + "_" + suffix;
}

bool cfgc_plan_folder_copy(const std::string &src, const std::string &dst, WriteBatch &batch, std::string *error)
{
    std::error_code ec;
    if (!fs::is_directory(src, ec))
    {
        if (error != nullptr)
            *error = src + " is not a folder.";
        return false;
    }
    if (fs::exists(dst, ec) && !fs::is_empty(dst, ec))
    {
        if (error != nullptr)
            *error = dst + " already exists.";
        return false;
    }
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec))
            files.push_back(it->path());
    std::sort(files.begin(), files.end());
    WriteBatch staged;
    for (const fs::path &p : files)
    {
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        if (!f && !f.eof())
        {
            if (error != nullptr)
                *error = "Could not read " + p.string() + ".";
            return false;
        }
        staged.put((fs::path(dst) / fs::relative(p, src, ec)).generic_string(), ss.str());
    }
    for (const WriteBatch::Op &op : staged.ops())
        batch.put(op.path, op.bytes);
    return true;
}

std::string cfgc_campaign_id_from_name(const std::string &name)
{
    std::string id;
    for (char c : name)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (ok)
            id += c;
        else if (!id.empty() && id.back() != '_')
            id += '_';
    }
    while (!id.empty() && id.back() == '_')
        id.pop_back();
    return id.empty() ? std::string("campaign") : id;
}

std::string cfgc_new_campaign_text(const std::string &name, const std::string &id, const std::string &human_player, bool own_config,
    bool own_land)
{
    std::string t;
    t += "; KeeperFX campaign file -- written by the map editor.\n\n[common]\n";
    t += "NAME = " + name + "\n";
    t += "LEVELS_LOCATION = campgns/" + id + "\n";
    if (own_config)
    {
        t += "CONFIGS_LOCATION = campgns/" + id + "_cfg\n";
        t += "CREATURES_LOCATION = campgns/" + id + "_crtr\n";
    }
    if (own_land)
    {
        t += "LAND_LOCATION = campgns/" + id + "_lnd\n";
        t += "LAND_VIEW_START = rgmap00 viframe00\n";
        t += "LAND_VIEW_END = rgmap00 viframe00\n";
    }
    t += "LAND_MARKERS = ENSIGNS\n";
    t += "HUMAN_PLAYER = " + human_player + "\n";
    // The game lists a campaign only once it has a single level, so it starts with level 1 (the first map saved into the
    // campaign takes that number).
    t += "SINGLE_LEVELS = 1\nBONUS_LEVELS = 0\n";
    // The game refuses to load a campaign that has no [strings] and [speech] blocks: use the base game's text and speech.
    t += "\n[strings]\nENG = fxdata/gtext_eng.dat\n\n[speech]\nENG = campgns/keeporig_eng\n";
    return cfgc_campaign_add_level(t, 1, CampList_Single, "Level 1", nullptr);
}

std::vector<std::string> cfgc_move_in_order(std::vector<std::string> order, size_t index, int delta)
{
    const long to = (long)index + delta;
    if (index < order.size() && to >= 0 && to < (long)order.size())
        std::swap(order[index], order[(size_t)to]);
    return order;
}

std::string cfgc_order_file_text(const std::string &old_text, const std::vector<std::string> &order)
{
    const std::string eol = old_text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string out;
    std::istringstream in(old_text);
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] != '#')
            break; // the comment header ends at the first other line
        out += line + eol;
    }
    for (const std::string &n : order)
        out += n + eol;
    return out;
}

namespace {

std::string lower_of(std::string s)
{
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string prefix_of(int64_t n)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "map%05lld.", (long long)n);
    return buf;
}

} // namespace

bool cfgc_file_exists_ci(const std::string &path)
{
    std::error_code ec;
    if (fs::is_regular_file(path, ec))
        return true;
    const fs::path p(path);
    const std::string want = lower_of(p.filename().string());
    for (fs::directory_iterator it(p.parent_path().empty() ? fs::path(".") : p.parent_path(), ec), end; !ec && it != end; it.increment(ec))
        if (lower_of(it->path().filename().string()) == want && it->is_regular_file(ec))
            return true;
    return false;
}

std::vector<std::string> cfgc_level_files(const std::string &dir, int64_t n)
{
    std::vector<std::string> out;
    const std::string prefix = prefix_of(n);
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        if (it->is_regular_file(ec) && lower_of(name).compare(0, prefix.size(), prefix) == 0)
            out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int64_t> cfgc_level_numbers_in_dir(const std::string &dir)
{
    std::vector<int64_t> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = lower_of(it->path().filename().string());
        if (name.size() == 12 && name.compare(0, 3, "map") == 0 && name.compare(8, 4, ".slb") == 0)
        {
            const int64_t n = std::atoll(name.substr(3, 5).c_str());
            if (n > 0)
                out.push_back(n);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string cfgc_level_file_rename(const std::string &name, int64_t from, int64_t to)
{
    const std::string prefix = prefix_of(from);
    if (lower_of(name).compare(0, prefix.size(), prefix) != 0)
        return name;
    std::string head = prefix_of(to);
    if (name.compare(0, 3, "MAP") == 0)
        head = "MAP" + head.substr(3);
    return head + name.substr(prefix.size());
}

bool cfgc_plan_level_copy(const std::string &src_dir, int64_t from, const std::string &dst_dir, int64_t to, WriteBatch &batch,
    std::string *error)
{
    const std::vector<std::string> files = cfgc_level_files(src_dir, from);
    if (files.empty())
    {
        if (error != nullptr)
            *error = "Level " + std::to_string(from) + " has no files in " + src_dir + ".";
        return false;
    }
    if (!cfgc_level_files(dst_dir, to).empty())
    {
        if (error != nullptr)
            *error = "Level " + std::to_string(to) + " already has files in " + dst_dir + ".";
        return false;
    }
    WriteBatch staged;
    for (const std::string &f : files)
    {
        std::ifstream in((fs::path(src_dir) / f).string(), std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        if (!in && !in.eof())
        {
            if (error != nullptr)
                *error = "Could not read " + f + ".";
            return false;
        }
        staged.put((fs::path(dst_dir) / cfgc_level_file_rename(f, from, to)).generic_string(), ss.str());
    }
    for (const WriteBatch::Op &op : staged.ops())
        batch.put(op.path, op.bytes);
    return true;
}

std::string cfgc_new_pack_text(const std::string &name, const std::string &id, const std::string &folder, const std::string &human_player,
    bool own_config)
{
    std::string t;
    t += "; KeeperFX map pack file -- written by the map editor.\n\n[common]\n";
    t += "NAME = " + name + "\n";
    t += "LEVELS_LOCATION = " + folder + "/" + id + "\n";
    if (own_config)
    {
        t += "CONFIGS_LOCATION = " + folder + "/" + id + "_cfg\n";
        t += "CREATURES_LOCATION = " + folder + "/" + id + "_crtr\n";
    }
    t += "HUMAN_PLAYER = " + human_player + "\n";
    t += "\n[strings]\nENG = fxdata/gtext_eng.dat\n\n[speech]\nENG = campgns/keeporig_eng\n";
    return t;
}
