/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_campaign_check.cpp
 *     The campaign checker. See cfgc_campaign_check.h.
 */
#include "pre_inc.h"
#include "cfgc_campaign_check.h"
#include "cfgc_content.h"
#include "cfgc_validate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include "post_inc.h"

/******************************************************************************/
namespace {

std::string lower(std::string s)
{
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string norm_dir(std::string s)
{
    for (char &c : s)
        if (c == '\\')
            c = '/';
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    return lower(s);
}

std::string join_path(const std::string &root, const std::string &rel)
{
    if (rel.empty())
        return std::string();
    if (root.empty() || rel[0] == '/' || (rel.size() > 1 && rel[1] == ':'))
        return rel;
    return root + "/" + rel;
}

std::string five(int64_t n)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%05lld", (long long)n);
    return buf;
}

struct Finder
{
    const ConfigDocument &doc;
    // Section by canonical id -> its first block, with the line of each key (last occurrence wins, like the loader).
    struct Block
    {
        std::string name;
        int64_t header_line = 0;
        std::map<std::string, std::pair<std::string, int64_t>> keys; // lower key -> (value, 1-based line)
    };
    std::vector<Block> blocks;
    std::set<std::string> seen;

    explicit Finder(const ConfigDocument &d) : doc(d)
    {
        for (size_t i = 0; i < doc.sections().size(); i++)
        {
            const CfgSection &s = doc.sections()[i];
            if (s.header_line < 0)
                continue;
            if (!seen.insert(s.name).second)
                continue; // a repeated block is never read
            Block b;
            b.name = s.name;
            b.header_line = s.header_line + 1;
            for (int64_t li : doc.key_lines((int64_t)i))
            {
                const CfgLine &l = doc.lines()[(size_t)li];
                b.keys[lower(l.name)] = std::make_pair(l.value, li + 1);
            }
            blocks.push_back(std::move(b));
        }
    }
    const Block *find(const std::string &name) const
    {
        for (const Block &b : blocks)
            if (b.name == name)
                return &b;
        return nullptr;
    }
};

const std::pair<std::string, int64_t> *key_of(const Finder::Block *b, const char *key)
{
    if (b == nullptr)
        return nullptr;
    const auto it = b->keys.find(lower(key));
    return it == b->keys.end() ? nullptr : &it->second;
}

std::vector<std::string> words(const std::string &v)
{
    std::istringstream in(v);
    std::vector<std::string> w;
    for (std::string t; in >> t;)
        w.push_back(t);
    return w;
}

} // namespace

/******************************************************************************/
std::vector<int64_t> cfgc_parse_level_list(const std::string &value)
{
    std::vector<int64_t> out;
    for (const std::string &w : words(value))
    {
        char *end = nullptr;
        const long long n = strtoll(w.c_str(), &end, 10);
        if (end != nullptr && *end == 0 && end != w.c_str())
            out.push_back(n);
    }
    return out;
}

std::vector<CfgDiagnostic> cfgc_check_campaign(const ConfigDocument &doc, const ConfigSchema &schema, const CampaignCheckEnv &env)
{
    std::vector<CfgDiagnostic> out;
    auto add = [&](CfgSeverity sev, const char *code, int64_t line, const std::string &msg) {
        CfgDiagnostic d;
        d.severity = sev;
        d.code = code;
        d.message = msg;
        d.line = line;
        out.push_back(std::move(d));
    };
    auto exists = [&](const std::string &p) { return env.file_exists ? env.file_exists(p) : true; };
    auto dir_ok = [&](const std::string &p) { return env.dir_exists ? env.dir_exists(p) : true; };

    // The structural findings of the generic validator (unknown keys, value shapes), then ours.
    for (CfgDiagnostic &d : cfgc_validate_document(doc, schema.find("campaign"), nullptr))
    {
        if (d.code == "missing_value")
            d.severity = CfgSev_Info; // shipped files leave some keys empty; the loader skips them
        out.push_back(std::move(d));
    }
    // A line that looks like a block header but is not one: the loader does not see the block.
    for (size_t i = 0; i < doc.lines().size(); i++)
    {
        const CfgLine &l = doc.lines()[i];
        if (l.kind == CfgLine_Other && l.text.find('[') != std::string::npos && l.text.find(']') != std::string::npos)
            add(CfgSev_Warning, "not_a_header", (int64_t)i + 1,
                "this line looks like a block header but has other characters before it, so the game does not see the block");
    }

    const Finder f(doc);
    const Finder::Block *common = f.find("common");

    // Level lists.
    std::vector<int64_t> single, bonus, extra;
    const auto *sl = key_of(common, "SINGLE_LEVELS");
    const auto *bl = key_of(common, "BONUS_LEVELS");
    const auto *el = key_of(common, "EXTRA_LEVELS");
    if (sl != nullptr) single = cfgc_parse_level_list(sl->first);
    if (bl != nullptr) bonus = cfgc_parse_level_list(bl->first);
    if (el != nullptr) extra = cfgc_parse_level_list(el->first);
    // Bonus levels pair with single levels by position; a shorter list just has no bonus for the last ones
    // (shipped campaigns do that), a longer one has entries nothing refers to.
    if (bl != nullptr && bonus.size() > single.size())
        add(CfgSev_Warning, "bonus_length", bl->second,
            "BONUS_LEVELS has " + std::to_string(bonus.size()) + " entries but SINGLE_LEVELS only " + std::to_string(single.size())
                + "; the lists are parallel (0 means no bonus level)");

    std::map<int64_t, std::string> listed; // level -> list it is in
    auto add_list = [&](const std::vector<int64_t> &v, const char *label, const std::pair<std::string, int64_t> *at) {
        for (int64_t n : v)
        {
            if (n == 0 && std::string(label) == "BONUS_LEVELS")
                continue;
            const auto it = listed.find(n);
            if (it != listed.end())
                add(CfgSev_Error, "level_duplicate", at != nullptr ? at->second : 0,
                    "level " + std::to_string(n) + " is listed in " + it->second + " and in " + label);
            else
                listed[n] = label;
        }
    };
    add_list(single, "SINGLE_LEVELS", sl);
    add_list(bonus, "BONUS_LEVELS", bl);
    add_list(extra, "EXTRA_LEVELS", el);

    // Locations: resolved, exist, shared.
    auto location = [&](const char *key) -> std::string {
        const auto *k = key_of(common, key);
        return k != nullptr ? join_path(env.root, k->first) : std::string();
    };
    const std::string levels_dir = location("LEVELS_LOCATION");
    const std::string land_dir = location("LAND_LOCATION");
    struct Loc { const char *key; std::string dir; };
    for (const Loc &loc : {Loc{"LEVELS_LOCATION", levels_dir}, Loc{"LAND_LOCATION", land_dir},
             Loc{"CREATURES_LOCATION", location("CREATURES_LOCATION")}, Loc{"CONFIGS_LOCATION", location("CONFIGS_LOCATION")},
             Loc{"MEDIA_LOCATION", location("MEDIA_LOCATION")}})
    {
        const auto *k = key_of(common, loc.key);
        if (k == nullptr || loc.dir.empty())
            continue;
        const bool must_exist = std::string(loc.key) == "LEVELS_LOCATION";
        if (!dir_ok(loc.dir) && (must_exist || std::string(loc.key) == "LAND_LOCATION"))
            add(CfgSev_Warning, "location_missing", k->second, std::string(loc.key) + " " + k->first + " is not a folder");
        const bool cfg = std::string(loc.key) == "CONFIGS_LOCATION";
        if (cfg || std::string(loc.key) == "CREATURES_LOCATION")
        {
            std::string names;
            for (const CampaignPeer &p : env.peers)
                if (p.fname != env.own_fname && norm_dir(cfg ? p.cfg_dir : p.crtr_dir) == norm_dir(loc.dir))
                    names += (names.empty() ? "" : ", ") + p.name;
            if (!names.empty())
                add(CfgSev_Warning, "location_shared", k->second,
                    std::string(loc.key) + " is shared with " + names + "; editing it changes them too");
        }
    }

    // Levels: map files, and the [mapNNNNN] entries.
    if (!levels_dir.empty() && dir_ok(levels_dir))
        for (const auto &l : listed)
            if (!exists(levels_dir + "/map" + five(l.first) + ".slb"))
            {
                const auto *at = l.second == "SINGLE_LEVELS" ? sl : l.second == "BONUS_LEVELS" ? bl : el;
                add(CfgSev_Error, "level_files_missing", at != nullptr ? at->second : 0,
                    "level " + std::to_string(l.first) + " (" + l.second + ") has no map" + five(l.first) + ".slb in " + levels_dir);
            }
    std::map<int64_t, int> entry_count;
    for (const Finder::Block &b : f.blocks)
    {
        std::string base;
        int64_t index;
        cfgc_split_section_name(b.name, base, index);
        if (base != "map" || index < 0)
            continue;
        entry_count[index]++;
        if (listed.find(index) == listed.end())
            add(CfgSev_Warning, "entry_unused", b.header_line,
                "[" + b.name + "] describes level " + std::to_string(index) + ", which no level list contains");
    }
    for (const auto &l : listed)
        if (entry_count.find(l.first) == entry_count.end())
            add(CfgSev_Info, "entry_no_level_list", 0,
                "level " + std::to_string(l.first) + " (" + l.second + ") has no [map" + five(l.first) + "] entry: it has no name or ensign in the land view");
    // A repeated [mapNNNNN] with different zero padding names the same level twice.
    {
        std::map<int64_t, int> by_index;
        for (const CfgSection &s : doc.sections())
        {
            std::string base;
            int64_t index;
            cfgc_split_section_name(s.name, base, index);
            if (s.header_line >= 0 && base == "map" && index >= 0 && ++by_index[index] == 2)
                add(CfgSev_Error, "map_entry_duplicate", s.header_line + 1, "level " + std::to_string(index) + " has more than one [map] entry");
        }
    }

    // HUMAN_PLAYER.
    if (const auto *hp = key_of(common, "HUMAN_PLAYER"))
    {
        const std::string w = lower(words(hp->first).empty() ? std::string() : words(hp->first)[0]);
        static const char *ok[] = {"red", "blue", "green", "yellow", "white", "purple", "black", "orange", "neutral"};
        if (!w.empty() && std::find(std::begin(ok), std::end(ok), w) == std::end(ok))
            add(CfgSev_Error, "human_player", hp->second, "HUMAN_PLAYER " + hp->first + " is not a player colour");
    }

    // Land view files.
    for (const char *key : {"LAND_VIEW_START", "LAND_VIEW_END"})
        if (const auto *k = key_of(common, key))
            if (!land_dir.empty() && dir_ok(land_dir) && !words(k->first).empty())
            {
                const std::string img = land_dir + "/" + words(k->first)[0];
            {
                const std::string img_name = words(k->first)[0];
                if (exists(img + ".png"))
                {
                    // A PNG takes the place of the .raw + .pal pair; its size is in the IHDR chunk.
                    const std::string head = env.read_prefix ? env.read_prefix(img + ".png", 24) : std::string();
                    if (head.size() >= 24 && head.compare(1, 3, "PNG") == 0)
                    {
                        auto be32 = [&](size_t at) {
                            return ((uint32_t)(uint8_t)head[at] << 24) | ((uint32_t)(uint8_t)head[at + 1] << 16)
                                | ((uint32_t)(uint8_t)head[at + 2] << 8) | (uint32_t)(uint8_t)head[at + 3];
                        };
                        if (be32(16) != 1280 || be32(20) != 960)
                            add(CfgSev_Error, "landview_size", k->second,
                                std::string(key) + ": " + img_name + ".png is " + std::to_string(be32(16)) + " x " + std::to_string(be32(20))
                                    + ", it must be 1280 x 960");
                    }
                    else if (head.size() >= 24)
                        add(CfgSev_Error, "landview_size", k->second, std::string(key) + ": " + img_name + ".png is not a PNG file");
                }
                else if (!exists(img + ".raw"))
                    add(CfgSev_Warning, "landview_missing", k->second, std::string(key) + ": no " + img_name + ".png or .raw in " + land_dir);
                else
                {
                    if (!exists(img + ".pal"))
                        add(CfgSev_Warning, "landview_missing", k->second, std::string(key) + ": " + img_name + ".raw has no .pal");
                    else if (env.file_size && env.file_size(img + ".pal") != 768)
                        add(CfgSev_Warning, "landview_size", k->second, std::string(key) + ": " + img_name + ".pal is not 768 bytes");
                }
            }
            }

    // Per-level land view picture and speech files.
    {
        std::string speech_dir;
        if (const Finder::Block *sp = f.find("speech"))
            for (const auto &kv : sp->keys)
            {
                speech_dir = kv.second.first;
                break;
            }
        for (const Finder::Block &b : f.blocks)
        {
            std::string base;
            int64_t index;
            cfgc_split_section_name(b.name, base, index);
            if (base != "map" || index < 0)
                continue;
            if (const auto *lv = key_of(&b, "LAND_VIEW"))
            {
                const std::vector<std::string> w = words(lv->first);
                if (!w.empty() && !land_dir.empty() && dir_ok(land_dir) && !exists(land_dir + "/" + w[0] + ".png") && !exists(land_dir + "/" + w[0] + ".raw"))
                    add(CfgSev_Warning, "landview_missing", lv->second, "[" + b.name + "] LAND_VIEW: no " + w[0] + ".png or .raw in " + land_dir);
                if (w.size() == 1)
                    add(CfgSev_Warning, "landview_frame", lv->second, "[" + b.name + "] LAND_VIEW needs a picture and a frame (LAND_VIEW = image frame)");
            }
            if (const auto *sp = key_of(&b, "SPEECH"))
            {
                const std::vector<std::string> w = words(sp->first);
                if (w.size() == 1)
                    add(CfgSev_Warning, "speech_pair", sp->second, "[" + b.name + "] SPEECH needs two file names (SPEECH = before after)");
                if (!speech_dir.empty())
                {
                    const std::string dir = join_path(env.root, speech_dir);
                    for (const std::string &file : w)
                        if (dir_ok(dir) && !exists(dir + "/" + file))
                            add(CfgSev_Warning, "speech_missing", sp->second, "[" + b.name + "] SPEECH: " + file + " is not in " + speech_dir);
                }
            }
        }
    }

    // [strings]: the paths and the loader's 16-byte minimum.
    if (const Finder::Block *sb = f.find("strings"))
        for (const auto &kv : sb->keys)
        {
            const std::string p = join_path(env.root, kv.second.first);
            if (kv.second.first.empty())
                continue;
            const bool ex = exists(p);
            if (!ex)
                add(CfgSev_Warning, "strings_missing", kv.second.second, "[strings] " + kv.first + ": " + kv.second.first + " does not exist");
            else if (env.file_size && env.file_size(p) >= 0 && env.file_size(p) < 16)
                add(CfgSev_Warning, "strings_short", kv.second.second, "[strings] " + kv.first + ": " + kv.second.first + " is shorter than the 16 bytes the game needs");
        }

    std::stable_sort(out.begin(), out.end(), [](const CfgDiagnostic &a, const CfgDiagnostic &b) { return a.line < b.line; });
    return out;
}
