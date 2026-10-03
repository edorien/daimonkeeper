// kfx_sim: golden hashes of the two level-information parsers (refactor pass 3,
// S05: docs/refactor-pass3/stage-05-level-info-parser.md).
//
// A campaign's [mapN] blocks (parse_campaign_map_block(), kfx_config) and a
// level's .lof file (level_lof_file_parse(), kfx_sim) read the same keys into
// the same struct LevelInformation. S05 gives them one parser for the shared
// keys; these hashes must not change when it does.
//
// Cases, each hashed and compared against fixtures/level_info_golden.txt:
//  - corpus: every [mapN] block of every campaign file in campgns/;
//  - variants: every level-information key with each of a list of awkward
//    values, once as a campaign "[map00001]" block and once as a .lof file.
// A hash covers the level's LevelInformation, the campaign's level lists (a
// .lof KIND adds to them) and the compat report.
//
// Regenerate (only on a commit meant to change parsing):
//     KFX_SIM_GOLDEN_WRITE=1 kfx_sim_utest "[golden]"
// The .lof files of a local game data folder aren't in the repo; to compare
// them by hand before and after a change:
//     LEVEL_INFO_DUMP=out.txt LEVEL_INFO_DATA=core_files kfx_sim_utest "[.lofdata]"
#include <catch2/catch_test_macros.hpp>

#include "config.h"
#include "config_campaigns.h"
#include "compat_report.h"
#include "lvl_filesdk1.h"
#include "ports/render_port.h"
#include "kfx_config/tests/scoped_port_override.h"
#include "kfx_sim_test_paths.h" // KFX_SIM_TEST_FIXTURES_DIR, KFX_SIM_TEST_REPO_ROOT

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// Defined in lvl_filesdk1.c and config_campaigns.c but not part of their public headers.
extern "C" TbBool level_lof_file_parse(const char *fname, char *buf, int64_t len);
extern "C" int64_t parse_campaign_map_block(int64_t lvnum, uint64_t lvoptions, char *buf, int64_t len, const char *config_textname);

namespace {

namespace fs = std::filesystem;

const std::string kGoldenFile = std::string(KFX_SIM_TEST_FIXTURES_DIR) + "/level_info_golden.txt";

struct Fnv {
    uint64_t v = 1469598103934665603ULL;
    void add(const void *p, size_t n) {
        const unsigned char *b = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; i++) { v ^= b[i]; v *= 1099511628211ULL; }
    }
    void add_str(const char *s) { add(s, std::strlen(s) + 1); }
    void add_i(int64_t x) { add(&x, sizeof(x)); }
};

int64_t stub_get_ensign_id(const char *name) {
    if (name == nullptr || std::strcmp(name, "NO_SUCH_ENSIGN") == 0)
        return -1;
    Fnv h;
    h.add_str(name);
    return (int64_t)(h.v % 997);
}

/** An empty campaign: no level entries, empty level lists, empty compat report. */
void reset_campaign() {
    free_campaign(&campaign);
    std::memset(&campaign, 0, sizeof(campaign));
    init_level_info_entries(&campaign, 0);
    compat_report_clear();
}

uint64_t level_hash(LevelNumber lvnum) {
    Fnv h;
    const struct LevelInformation *lvinfo = get_level_info(lvnum);
    h.add_i(lvinfo != nullptr);
    if (lvinfo != nullptr)
        h.add(lvinfo, sizeof(*lvinfo));
    h.add(campaign.single_levels, sizeof(campaign.single_levels));
    h.add(campaign.multi_levels, sizeof(campaign.multi_levels));
    h.add(campaign.bonus_levels, sizeof(campaign.bonus_levels));
    h.add(campaign.extra_levels, sizeof(campaign.extra_levels));
    h.add(campaign.freeplay_levels, sizeof(campaign.freeplay_levels));
    h.add_i((int64_t)campaign.single_levels_count);
    h.add_i((int64_t)campaign.multi_levels_count);
    h.add_i((int64_t)campaign.bonus_levels_count);
    h.add_i((int64_t)campaign.extra_levels_count);
    h.add_i((int64_t)campaign.freeplay_levels_count);
    h.add_i(compat_report_count());
    for (int64_t i = 0; i < compat_report_count(); i++) {
        const struct CompatIssue *issue = compat_report_get(i);
        h.add_i(issue->kind);
        h.add_str(issue->what);
        h.add_str(issue->where);
        h.add_i((int64_t)issue->line);
        h.add_i((int64_t)issue->count);
    }
    return h.v;
}

std::string read_file(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void parse_lof_text(const char *fname, const std::string &text) {
    std::string buf = text;
    buf.push_back('\0');
    level_lof_file_parse(fname, &buf[0], (int64_t)text.size());
}

void parse_campaign_text(LevelNumber lvnum, const std::string &text, const char *fname) {
    std::string buf = text;
    buf.push_back('\0');
    parse_campaign_map_block(lvnum, LvKind_IsSingle, &buf[0], (int64_t)text.size(), fname);
}

const std::vector<const char *> kKeys = {
    "NAME_TEXT", "NAME_ID", "ENSIGN_POS", "ENSIGN_ZOOM", "PLAYERS", "ENSIGN", "OPTIONS", "SPEECH", "LAND_VIEW",
    "KIND", "AUTHOR", "DESCRIPTION", "DATE", "MAPSIZE", "MAP_FORMAT_VERSION", "SKIRMISH_SETUP", "NOTAKEY",
};

std::vector<std::string> values() {
    std::vector<std::string> v = {
        "", "0", "1", "-1", "7", "12 34", "0 5", "-3 4", "300 400 500", "12abc", "abc", "NULL", "SINGLE",
        "MULTI BONUS FREE", "EXTRA", "LOCKED", "allow", "MAYBE", "a.wav b.wav", "only_one.wav",
        "Level Name With Spaces", "GUI_NO_SUCH_ALIAS", "NO_SUCH_ENSIGN", "TUTORIAL", "   padded   ",
    };
    v.push_back(std::string(300, 'x'));
    v.push_back(std::string(1100, 'y'));
    return v;
}

std::string short_value(const std::string &value) {
    return value.size() > 40 ? value.substr(0, 12) + "...(" + std::to_string(value.size()) + " chars)" : value;
}

class Golden {
public:
    void put(const std::string &name, uint64_t hash) { actual_[name] = hash; }

    void check(const std::string &section) {
        std::map<std::string, uint64_t> expected;
        std::vector<std::string> other_lines;
        {
            std::ifstream in(kGoldenFile);
            std::string line;
            while (std::getline(in, line)) {
                const size_t tab = line.find('\t');
                if (line.empty() || line[0] == '#' || tab == std::string::npos) continue;
                const std::string name = line.substr(0, tab);
                if (name.rfind(section + "/", 0) == 0)
                    expected[name] = std::strtoull(line.c_str() + tab + 1, nullptr, 16);
                else
                    other_lines.push_back(line);
            }
        }
        if (std::getenv("KFX_SIM_GOLDEN_WRITE") != nullptr) {
            for (const auto &kv : actual_) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)kv.second);
                other_lines.push_back(kv.first + "\t" + buf);
            }
            std::sort(other_lines.begin(), other_lines.end());
            std::ofstream out(kGoldenFile, std::ios::trunc);
            out << "# Golden hashes for level_info_golden_test.cpp (refactor pass 3, S05).\n";
            out << "# Regenerate only on a commit meant to change parsing: KFX_SIM_GOLDEN_WRITE=1.\n";
            for (const auto &l : other_lines) out << l << "\n";
            WARN("rewrote " << actual_.size() << " " << section << " golden hashes");
            return;
        }
        REQUIRE_FALSE(expected.empty());
        std::ostringstream diff;
        int64_t bad = 0;
        for (const auto &kv : actual_) {
            auto it = expected.find(kv.first);
            if ((it == expected.end() || it->second != kv.second) && bad++ < 40)
                diff << (it == expected.end() ? "new: " : "changed: ") << kv.first << "\n";
        }
        for (const auto &kv : expected)
            if (actual_.find(kv.first) == actual_.end() && bad++ < 40)
                diff << "missing: " << kv.first << "\n";
        INFO(bad << " of " << expected.size() << " golden hashes differ:\n" << diff.str());
        CHECK(bad == 0);
    }

private:
    std::map<std::string, uint64_t> actual_;
};

struct Ports {
    ScopedPortOverride<RenderPort> render{render_port, set_render_port};
    Ports() { render->get_ensign_id = stub_get_ensign_id; }
};

} // namespace

TEST_CASE("golden: the [mapN] blocks of the shipped campaign files", "[kfx_sim][golden]")
{
    Ports ports;
    Golden golden;
    const std::regex block(R"(^\[map(\d+)\])", std::regex::icase | std::regex::multiline);
    int64_t blocks = 0;
    for (const auto &e : fs::directory_iterator(fs::path(KFX_SIM_TEST_REPO_ROOT) / "campgns"))
    {
        if (e.path().extension() != ".cfg")
            continue;
        const std::string text = read_file(e.path());
        for (std::sregex_iterator it(text.begin(), text.end(), block), end; it != end; ++it)
        {
            const LevelNumber lvnum = std::atoi((*it)[1].str().c_str());
            reset_campaign();
            parse_campaign_text(lvnum, text, e.path().filename().string().c_str());
            golden.put("campaign/" + e.path().filename().string() + "/map" + std::to_string(lvnum), level_hash(lvnum));
            blocks++;
        }
    }
    CHECK(blocks > 100);
    reset_campaign();
    golden.check("campaign");
}

TEST_CASE("golden: every level-information key with each awkward value, in a [mapN] block and in a .lof file", "[kfx_sim][golden]")
{
    Ports ports;
    Golden golden;
    for (const char *key : kKeys)
    {
        for (const std::string &value : values())
        {
            const std::string line = std::string(key) + " = " + value + "\n";
            reset_campaign();
            parse_campaign_text(1, "[common]\nNAME = x\n[map00001]\n" + line + "[map00002]\nNAME_TEXT = next\n", "variant.cfg");
            golden.put(std::string("variant/campaign/") + key + "=" + short_value(value), level_hash(1));
            reset_campaign();
            parse_lof_text("map00001.lof", line);
            golden.put(std::string("variant/lof/") + key + "=" + short_value(value), level_hash(1));
        }
    }
    // A key after a key with a bad value, and a file without an end of line.
    reset_campaign();
    parse_lof_text("map00001.lof", "PLAYERS = abc\nNAME_ID = 12\nMAPSIZE = 5");
    golden.put("variant/lof/sequence", level_hash(1));
    reset_campaign();
    golden.check("variant");
}

TEST_CASE("the .lof files of a local game data folder, hashed for a by-hand comparison", "[.lofdata]")
{
    const char *data = std::getenv("LEVEL_INFO_DATA");
    const char *dump = std::getenv("LEVEL_INFO_DUMP");
    REQUIRE(data != nullptr);
    REQUIRE(dump != nullptr);
    Ports ports;
    std::vector<fs::path> files;
    for (const auto &e : fs::recursive_directory_iterator(data))
        if (e.is_regular_file() && (e.path().extension() == ".lof" || e.path().extension() == ".LOF"))
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    std::ofstream out(dump);
    for (const fs::path &p : files)
    {
        reset_campaign();
        const std::string name = p.filename().string();
        parse_lof_text(name.c_str(), read_file(p));
        const LevelNumber lvnum = get_level_number_from_file_name(name.c_str());
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)level_hash(lvnum));
        out << fs::relative(p, data).string() << "\t" << buf << "\n";
    }
    reset_campaign();
    CHECK(files.size() > 0);
}
