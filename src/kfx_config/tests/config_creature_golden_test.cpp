// kfx_config: golden hashes of the creature, magic and rules config parsers
// (refactor pass 3, S04: docs/refactor-pass3/stage-04-creature-config-tables.md).
//
// The hand-written block parsers in config_crtrmodel.c, config_creature.c,
// config_magic.c and config_rules.c are converted to NamedField tables one
// block at a time; these hashes must not change at any of those commits.
//
// Two kinds of cases, each hashed and compared against
// fixtures/creature_config_golden.txt:
//
//  - The corpus: the repo's own config/fxdata and config/creatrs files,
//    loaded in load_stats_files()'s order, then again with each
//    campaign's, each mod's and a map's overrides on top.
//  - Variants: for every key of every block, a one-line override file
//    ("[block]\nKEY = value") for each of a fixed list of awkward values
//    (out of range for the field's type, not a number, a number followed
//    by text, missing, one value too few or too many, unknown names),
//    applied on top of the base state.
//
// A hash covers the whole CreatureConfig, MagicConfig and RulesConfig
// state, the research list the rules file sets up (through SimPort), and
// the compat report (unknown keys and values, with file and line). Sprite
// and icon names are hashed to ids by the RenderPort stubs below, so a
// change in which name reaches them shows.
//
// To regenerate the golden file (only on a commit that is meant to change
// parsing): KFX_CONFIG_GOLDEN_WRITE=1 kfx_config_utest "[golden]".
// KFX_CONFIG_GOLDEN_FULL_RELOAD=1 checks the file variants' snapshot shortcut.
#include <catch2/catch_test_macros.hpp>

#include "config.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
#include "config_crtrstates.h"
#include "config_effects.h"
#include "config_lenses.h"
#include "config_magic.h"
#include "config_objects.h"
#include "config_players.h"
#include "config_rules.h"
#include "config_sounds.h"
#include "config_terrain.h"
#include "config_translation.h"
#include "config_trapdoor.h"
#include "compat_report.h"
#include "kfx_config_state.h"
#include "ports/render_port.h"
#include "ports/sim_port.h"
#include "kfx_config/tests/scoped_port_override.h"
#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR, KFX_CONFIG_TEST_REPO_ROOT

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

const std::string kRepo = KFX_CONFIG_TEST_REPO_ROOT;
const std::string kFx = kRepo + "/config/fxdata/";
const std::string kGoldenFile = std::string(KFX_CONFIG_TEST_FIXTURES_DIR) + "/creature_config_golden.txt";

struct Fnv {
    uint64_t v = 1469598103934665603ULL;
    // FNV-1a over 8-byte words (bytes for the tail): the states hashed are
    // megabytes and there are thousands of variants.
    void add(const void *p, size_t n) {
        const unsigned char *b = static_cast<const unsigned char *>(p);
        size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            uint64_t w;
            std::memcpy(&w, b + i, 8);
            v ^= w;
            v *= 1099511628211ULL;
        }
        for (; i < n; i++) { v ^= b[i]; v *= 1099511628211ULL; }
    }
    void add_str(const char *s) { add(s, std::strlen(s) + 1); }
    void add_i(int64_t x) { add(&x, sizeof(x)); }
};

std::vector<int64_t> research_log;

TbBool log_add_research(int64_t rtyp, int64_t rkind, int64_t amount) {
    research_log.push_back(rtyp);
    research_log.push_back(rkind);
    research_log.push_back(amount);
    return true;
}
TbBool log_clear_research(void) {
    research_log.push_back(-1);
    return true;
}

int64_t name_id(const char *name) {
    if (name == nullptr) return 0;
    Fnv h;
    h.add_str(name);
    return (int64_t)(h.v % 10007) + 1;
}
int64_t stub_get_anim_id(const char *name) { return name_id(name); }
int64_t stub_get_icon_id(const char *name) { return name_id(name); }

bool file_exists(const std::string &path) {
    return std::filesystem::is_regular_file(path);
}

void load_file(const ConfigFileData &fd, const std::string &path, int64_t flags) {
    if (!file_exists(path)) return;
    compat_report_set_source(path.c_str());
    fd.load_func(path.c_str(), flags);
    compat_report_set_source(nullptr);
}

/** Override directories, in load_config()'s order. Empty strings are skipped. */
struct Overlay {
    std::string mod_fx;      // a mod's fxdata/ (after the base files)
    std::string mod_crtr;    // a mod's creatrs/
    std::string cfgs;        // a campaign's <name>_cfgs/
    std::string crtr;        // a campaign's <name>_crtr/
    std::string map_prefix;  // "<dir>/map00457." for map-level overrides
};

void load_layered(const ConfigFileData &fd, int64_t flags, const Overlay &ov) {
    if (fd.pre_load_func != nullptr) fd.pre_load_func();
    load_file(fd, kFx + fd.filename, flags);
    const int64_t over = flags | CnfLd_AcceptPartial | CnfLd_IgnoreErrors;
    if (!ov.mod_fx.empty()) load_file(fd, ov.mod_fx + "/" + fd.filename, over);
    if (!ov.cfgs.empty()) load_file(fd, ov.cfgs + "/" + fd.filename, over);
    if (!ov.map_prefix.empty()) load_file(fd, ov.map_prefix + fd.filename, over);
    if (fd.post_load_func != nullptr) fd.post_load_func();
}

std::string lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

/** load_stats_files()'s order, with explicit paths instead of the FGrp_* lookups. */
void load_all(const Overlay &ov) {
    std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    research_log.clear();
    compat_report_clear();
    init_all_creature_model_stats();
    init_creature_model_graphics();

    load_layered(keeper_translation_file_data, CnfLd_Standard, ov);

    const ConfigFileData *listed[] = {
        &keeper_creaturetp_file_data, &keeper_terrain_file_data, &keeper_objects_file_data,
        &keeper_trapdoor_file_data, &keeper_effects_file_data, &keeper_lenses_file_data,
        &keeper_magic_file_data, &creature_states_file_data, &keeper_playerstates_file_data,
    };
    for (const ConfigFileData *fd : listed)
        load_layered(*fd, CnfLd_ListOnly, ov);

    const ConfigFileData *full[] = {
        &keeper_terrain_file_data, &keeper_objects_file_data, &keeper_trapdoor_file_data,
        &keeper_effects_file_data, &keeper_lenses_file_data, &keeper_magic_file_data,
        &keeper_creaturetp_file_data, &creature_states_file_data,
    };
    for (const ConfigFileData *fd : full)
        load_layered(*fd, CnfLd_Standard | CnfLd_PreListed, ov);
    load_layered(keeper_rules_file_data, CnfLd_Standard, ov);

    // load_creaturemodel_config()'s order for every model.
    for (int64_t i = 1; i < kfx_config_state.conf.crtr_conf.model_count; i++) {
        const std::string name = lower(creature_code_name(i));
        init_creature_model_stats(i);
        int64_t flags = CnfLd_AcceptPartial;
        std::vector<std::string> files = {kRepo + "/config/creatrs/" + name + ".cfg"};
        if (!ov.mod_crtr.empty()) files.push_back(ov.mod_crtr + "/" + name + ".cfg");
        if (!ov.crtr.empty()) files.push_back(ov.crtr + "/" + name + ".cfg");
        for (size_t f = 0; f < files.size(); f++) {
            const int64_t file_flags = (f > 0 && !ov.mod_crtr.empty() && files[f].rfind(ov.mod_crtr, 0) == 0)
                ? (flags | CnfLd_IgnoreErrors) : flags;
            if (file_exists(files[f]) && load_creaturemodel_config_file(i, files[f].c_str(), file_flags))
                set_flag(flags, CnfLd_IgnoreErrors);
        }
    }
}

uint64_t state_hash() {
    Fnv h;
    h.add(&kfx_config_state.conf.crtr_conf, sizeof(kfx_config_state.conf.crtr_conf));
    h.add(&kfx_config_state.conf.magic_conf, sizeof(kfx_config_state.conf.magic_conf));
    h.add(&kfx_config_state.conf.rules, sizeof(kfx_config_state.conf.rules));
    for (int64_t r : research_log) h.add_i(r);
    h.add_i(compat_report_count());
    h.add_i(compat_report_overflow());
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

/** Every key of every block, as the parsers know them today. */
struct BlockKeys {
    const char *block;
    std::vector<const char *> keys;
};

const std::vector<BlockKeys> kCrtrModelBlocks = {
    {"attributes", {"NAME", "HEALTH", "HEALREQUIREMENT", "HEALTHRESHOLD", "STRENGTH", "ARMOUR", "DEXTERITY",
        "FEARWOUNDED", "FEARSTRONGER", "DEFENCE", "LUCK", "RECOVERY", "HUNGERRATE", "HUNGERFILL", "LAIRSIZE",
        "HURTBYLAVA", "BASESPEED", "GOLDHOLD", "SIZE", "ATTACKPREFERENCE", "PAY", "HEROVSKEEPERCOST", "SLAPSTOKILL",
        "CREATURELOYALTY", "LOYALTYLEVEL", "DAMAGETOBOULDER", "THINGSIZE", "PROPERTIES", "NAMETEXTID",
        "FEARSOMEFACTOR", "TOKINGRECOVERY", "CORPSEVANISHEFFECT", "FOOTSTEPPITCH", "LAIROBJECT", "PRISONKIND",
        "TORTUREKIND", "SPELLIMMUNITY", "HOSTILETOWARDS", "NOTAKEY"}},
    {"attraction", {"ENTRANCEROOM", "ROOMSLABSREQUIRED", "BASEENTRANCESCORE", "SCAVENGEREQUIREMENT", "TORTURETIME",
        "NOTAKEY"}},
    {"annoyance", {"EATFOOD", "WILLNOTDOJOB", "INHAND", "NOLAIR", "NOHATCHERY", "WOKENUP", "STANDINGONDEADENEMY",
        "SULKING", "NOSALARY", "SLAPPED", "STANDINGONDEADFRIEND", "INTORTURE", "INTEMPLE", "SLEEPING", "GOTWAGE",
        "WINBATTLE", "UNTRAINED", "OTHERSLEAVING", "JOBSTRESS", "QUEUE", "LAIRENEMY", "ANNOYLEVEL", "ANGERJOBS",
        "GOINGPOSTAL", "NOTAKEY"}},
    {"senses", {"HEARING", "EYEHEIGHT", "FIELDOFVIEW", "EYEEFFECT", "MAXANGLECHANGE", "NOTAKEY"}},
    {"appearance", {"WALKINGANIMSPEED", "VISUALRANGE", "POSSESSSWIPEINDEX", "NATURALDEATHKIND", "SHOTORIGIN",
        "CORPSEVANISHEFFECT", "FOOTSTEPPITCH", "PICKUPOFFSET", "STATUSOFFSET", "TRANSPARENCYFLAGS", "FIXEDANIMSPEED",
        "NOTAKEY"}},
    {"experience", {"POWERS", "POWERSLEVELREQUIRED", "LEVELSTRAINVALUES", "GROWUP", "SLEEPEXPERIENCE",
        "EXPERIENCEFORHITTING", "REBIRTH", "NOTAKEY"}},
    {"jobs", {"PRIMARYJOBS", "SECONDARYJOBS", "NOTDOJOBS", "STRESSFULJOBS", "TRAININGVALUE", "TRAININGCOST",
        "SCAVENGEVALUE", "SCAVENGERCOST", "RESEARCHVALUE", "MANUFACTUREVALUE", "PARTNERTRAINING", "NOTAKEY"}},
    {"sprites", {"STAND", "AMBULATE", "DRAG", "ATTACK", "DIG", "SMOKE", "RELAX", "PRETTYDANCE", "GOTHIT",
        "POWERGRAB", "GOTSLAPPED", "CELEBRATE", "SLEEP", "EATCHICKEN", "TORTURE", "SCREAM", "DROPDEAD", "DEADSPLAT",
        "ROAR", "QUERYSYMBOL", "HANDSYMBOL", "PISS", "CASTSPELL", "RANGEDATTACK", "CUSTOM", "NOTAKEY"}},
    {"sounds", {"HIT", "HAPPY", "SAD", "HANG", "DROP", "TORTURE", "SLAP", "DIE", "FOOT", "FIGHT", "PISS", "NOTAKEY"}},
};

const std::vector<BlockKeys> kCreatureBlocks = {
    {"common", {"CREATURES", "JOBSCOUNT", "ANGERJOBSCOUNT", "ATTACKPREFERENCESCOUNT", "SPRITESIZE", "NOTAKEY"}},
    {"experience", {"SIZEINCREASEONEXP", "PAYINCREASEONEXP", "SPELLDAMAGEINCREASEONEXP", "RANGEINCREASEONEXP",
        "JOBVALUEINCREASEONEXP", "HEALTHINCREASEONEXP", "STRENGTHINCREASEONEXP", "DEXTERITYINCREASEONEXP",
        "DEFENSEINCREASEONEXP", "LOYALTYINCREASEONEXP", "ARMOURINCREASEONEXP", "EXPFORHITTINGINCREASEONEXP",
        "TRAININGCOSTINCREASEONEXP", "SCAVENGINGCOSTINCREASEONEXP", "NOTAKEY"}},
    {"instance5", {"Name", "Time", "ActionTime", "ResetTime", "FPTime", "FPActiontime", "FPResettime",
        "ForceVisibility", "TooltipTextID", "SymbolSprites", "Graphics", "Function", "RangeMin", "RangeMax",
        "Properties", "FpinstantCast", "PrimaryTarget", "ValidateSourceFunc", "ValidateTargetFunc",
        "SearchTargetsFunc", "PostalPriority", "NoAnimationLoop", "FPAllowSelfCastWhileFrozen",
        "FPAllowSelfCastWhenChicken", "NOTAKEY"}},
    {"job3", {"NAME", "RELATEDROOMROLE", "RELATEDEVENT", "ASSIGN", "INITIALSTATE", "CONTINUESTATE",
        "PLAYERFUNCTIONS", "COORDSFUNCTIONS", "PROPERTIES", "NOTAKEY"}},
    {"angerjob2", {"NAME", "NOTAKEY"}},
    {"attackpref1", {"NAME", "NOTAKEY"}},
    {"instance999", {"Name", "Time"}},
    {"job999", {"NAME"}},
};

const std::vector<BlockKeys> kMagicBlocks = {
    {"spell3", {"NAME", "DURATION", "SELFCASTED", "CASTATTHING", "SHOTMODEL", "EFFECTMODEL", "SYMBOLSPRITES",
        "SPELLPOWER", "AURAEFFECT", "SPELLFLAGS", "SUMMONCREATURE", "COUNTDOWN", "HEALINGRECOVERY", "DAMAGE",
        "DAMAGEFREQUENCY", "AURADURATION", "AURAFREQUENCY", "CLEANSEFLAGS", "PROPERTIES", "NOTAKEY"}},
    {"special2", {"NAME", "ARTIFACT", "TOOLTIPTEXTID", "SPEECHPLAYED", "ACTIVATIONEFFECT", "VALUE", "NOTAKEY"}},
    {"spell999", {"NAME", "DURATION"}},
    {"special999", {"NAME"}},
};

const std::vector<BlockKeys> kRulesBlocks = {
    {"research", {"RESEARCH", "NOTAKEY"}},
    {"sacrifices", {"MKCREATURE", "MKGOODHERO", "NEGSPELLALL", "POSSPELLALL", "NEGUNIQFUNC", "POSUNIQFUNC",
        "CUSTOMREWARD", "CUSTOMPUNISH", "NOTAKEY"}},
};

/** The awkward values every key is given. */
const std::vector<const char *> kValues = {
    "", "0", "1", "-1", "7", "255", "256", "300", "-129", "32768", "65536", "2147483647", "2147483648",
    "-2147483649", "99999999999", "abc", "12abc", "NULL", "none", "IMP", "ANY_CREATURE", "EVIL FLYING NOT_A_FLAG",
    "DIG NOT_A_JOB TRAIN", "1 2", "1 2 3 4 5 6 7 8 9 10 11 12 13", "IMP 5 3", "HARD 1 NOT_A_SLAB 2", "LAIR_IMP",
    "GARDEN LIBRARY", "SWING_WEAPON_SWORD FIREBALL", "MELEE", "2 1", "SPELL POWER_HAND 10",
};

std::string repeated(const char *word, int count) {
    std::string s;
    for (int i = 0; i < count; i++) s += std::string(i ? " " : "") + word;
    return s;
}

/** Long lists: more entries than any list key holds, and a line past the 1024-character line buffer. */
const std::string kLongList = repeated("IMP", 140);
const std::string kVeryLongList = repeated("SKELETON", 150);

std::vector<const char *> all_values() {
    std::vector<const char *> v = kValues;
    v.push_back(kLongList.c_str());
    v.push_back(kVeryLongList.c_str());
    return v;
}

/** A scratch file per test process (ctest may run cases in parallel). */
std::string scratch_file() {
    static const std::string dir = [] {
        std::filesystem::path p = std::filesystem::temp_directory_path() / ("kfx_golden_" + std::to_string(getpid()));
        std::filesystem::create_directories(p);
        return p.string();
    }();
    return dir + "/variant.cfg";
}

void write_variant(const char *block, const char *key, const char *value) {
    std::ofstream out(scratch_file(), std::ios::binary | std::ios::trunc);
    // MIN_CONFIG_FILE_SIZE: pad with a comment so a short file isn't refused as empty.
    out << "; golden-test variant, padded so the file is never too short to be read.\n";
    out << "[" << block << "]\n" << key << " = " << value << "\n";
}

struct Snapshot {
    struct CreatureConfig crtr;
    struct MagicConfig magic;
    struct RulesConfig rules[9];
    void save() {
        crtr = kfx_config_state.conf.crtr_conf;
        magic = kfx_config_state.conf.magic_conf;
        std::memcpy(rules, kfx_config_state.conf.rules, sizeof(rules));
    }
    void restore() const {
        kfx_config_state.conf.crtr_conf = crtr;
        kfx_config_state.conf.magic_conf = magic;
        std::memcpy(kfx_config_state.conf.rules, rules, sizeof(rules));
    }
};

class Golden {
public:
    void put(const std::string &name, uint64_t hash) { actual_[name] = hash; }

    /** Compares with the golden file, or rewrites it when KFX_CONFIG_GOLDEN_WRITE is set. */
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
        if (std::getenv("KFX_CONFIG_GOLDEN_WRITE") != nullptr) {
            for (const auto &kv : actual_) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)kv.second);
                other_lines.push_back(kv.first + "\t" + buf);
            }
            std::sort(other_lines.begin(), other_lines.end());
            std::ofstream out(kGoldenFile, std::ios::trunc);
            out << "# Golden hashes for config_creature_golden_test.cpp (refactor pass 3, S04).\n";
            out << "# Regenerate only on a commit meant to change parsing: KFX_CONFIG_GOLDEN_WRITE=1.\n";
            for (const auto &l : other_lines) out << l << "\n";
            WARN("rewrote " << actual_.size() << " " << section << " golden hashes");
            return;
        }
        REQUIRE_FALSE(expected.empty());
        std::ostringstream diff;
        int64_t bad = 0;
        for (const auto &kv : actual_) {
            auto it = expected.find(kv.first);
            if (it == expected.end() || it->second != kv.second) {
                if (bad++ < 40) diff << (it == expected.end() ? "new: " : "changed: ") << kv.first << "\n";
            }
        }
        for (const auto &kv : expected) {
            if (actual_.find(kv.first) == actual_.end()) {
                if (bad++ < 40) diff << "missing: " << kv.first << "\n";
            }
        }
        INFO(bad << " of " << expected.size() << " golden hashes differ:\n" << diff.str());
        CHECK(bad == 0);
    }

private:
    std::map<std::string, uint64_t> actual_;
};

struct Ports {
    ScopedPortOverride<SimPort> sim{sim_port, set_sim_port};
    ScopedPortOverride<RenderPort> render{render_port, set_render_port};
    Ports() {
        sim->add_research_to_all_players = log_add_research;
        sim->clear_research_for_all_players = log_clear_research;
        render->get_anim_id_ = stub_get_anim_id;
        render->get_icon_id = stub_get_icon_id;
        static bool sounds_loaded = false;
        if (!sounds_loaded) {
            load_file(keeper_sounds_file_data, kFx + "sounds.cfg", CnfLd_Standard);
            sounds_loaded = true;
        }
    }
};

std::string variant_name(const char *file, const char *block, const char *key, const char *value) {
    std::string v = value;
    if (v.size() > 64) v = v.substr(0, 16) + "...(" + std::to_string(v.size()) + " chars)";
    return std::string(file) + "/[" + block + "]/" + key + "=" + v;
}

} // namespace

TEST_CASE("golden: the shipped creature, magic and rules configs, with every campaign's and mod's overrides", "[kfx_config][golden]") {
    Ports ports;
    Golden golden;
    struct Case { const char *name; Overlay ov; };
    const std::vector<Case> cases = {
        {"base", {}},
        {"levels_classic", {"", "", kRepo + "/levels/classic_cfgs", kRepo + "/levels/classic_crtr", ""}},
        {"levels_legacy", {"", "", kRepo + "/levels/legacy_cfgs", kRepo + "/levels/legacy_crtr", ""}},
        {"multiplayer_classic", {"", "", kRepo + "/multiplayer/classic_cfgs", kRepo + "/multiplayer/classic_crtr", ""}},
        {"ami2019", {"", "", "", kRepo + "/campgns/ami2019_crtr", ""}},
        {"dzjr06lv", {"", "", "", kRepo + "/campgns/dzjr06lv_crtr", ""}},
        {"lqizgood", {"", "", "", kRepo + "/campgns/lqizgood_crtr", ""}},
        {"twinkprs", {"", "", "", kRepo + "/campgns/twinkprs_crtr", ""}},
        {"mod_tunneller_imp_swap", {kRepo + "/config/mods/tunneller_imp_swap/fxdata",
            kRepo + "/config/mods/tunneller_imp_swap/creatrs", "", "", ""}},
        {"mod_instant_charge_up", {kRepo + "/config/mods/instant_charge_up/fxdata", "", "", "", ""}},
        {"map00457", {"", "", "", "", kRepo + "/levels/standard/map00457."}},
    };
    for (const Case &c : cases) {
        load_all(c.ov);
        INFO("case " << c.name);
        // Sanity: the corpus really loaded.
        REQUIRE(kfx_config_state.conf.crtr_conf.model_count > 30);
        golden.put(std::string("corpus/") + c.name, state_hash());
    }
    golden.check("corpus");
}

/**
 * Every creature model key with each awkward value, as a one-line override of the imp. With `wrap`, under
 * the CREATURE_STATS_WRAP classic bug (set for the override, cleared before hashing, so the hashes compare
 * with the plain run's where the rules agree): the numbers keep KeeperFX's atoi() rules (pass 3 F10).
 */
static void run_crtrmodel_variants(const char *section, bool wrap) {
    Ports ports;
    Golden golden;
    load_all({});
    const int64_t imp = get_id(creature_desc, "IMP");
    REQUIRE(imp > 0);
    auto snap = std::make_unique<Snapshot>();
    snap->save();
    const bool full_reload = std::getenv("KFX_CONFIG_GOLDEN_FULL_RELOAD") != nullptr;
    for (const BlockKeys &bk : kCrtrModelBlocks) {
        for (const char *key : bk.keys) {
            for (const char *value : all_values()) {
                if (full_reload)
                    load_all({});
                else
                    snap->restore();
                research_log.clear();
                compat_report_clear();
                write_variant(bk.block, key, value);
                if (wrap)
                    set_flag(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CreatureStatsWrap);
                load_creaturemodel_config_file(imp, scratch_file().c_str(), CnfLd_AcceptPartial | CnfLd_IgnoreErrors);
                if (wrap)
                    clear_flag(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CreatureStatsWrap);
                golden.put(variant_name(section, bk.block, key, value), state_hash());
            }
        }
    }
    golden.check(section);
}

TEST_CASE("golden: every creature model key with each awkward value", "[kfx_config][golden]") {
    run_crtrmodel_variants("crtrmodel", false);
}

TEST_CASE("golden: every creature model key with each awkward value, with the CREATURE_STATS_WRAP classic bug", "[kfx_config][golden]") {
    run_crtrmodel_variants("crtrmodel_wrap", true);
}

namespace {
/** All of the config state plus the name tables these files rename entries in. */
struct FullSnapshot {
    std::unique_ptr<struct KfxConfigState> state{new struct KfxConfigState};
    std::vector<std::pair<struct NamedCommand *, std::vector<struct NamedCommand>>> tables;
    FullSnapshot() {
        *state = kfx_config_state;
        struct NamedCommand *named[] = {creature_desc, instance_desc, creaturejob_desc, angerjob_desc,
            attackpref_desc, spell_desc, shot_desc, power_desc, special_desc};
        const size_t counts[] = {CREATURE_TYPES_MAX, INSTANCE_TYPES_MAX, INSTANCE_TYPES_MAX, INSTANCE_TYPES_MAX,
            INSTANCE_TYPES_MAX, MAGIC_ITEMS_MAX, MAGIC_ITEMS_MAX, MAGIC_ITEMS_MAX, MAGIC_ITEMS_MAX};
        for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++)
            tables.emplace_back(named[i], std::vector<struct NamedCommand>(named[i], named[i] + counts[i]));
    }
    void restore() const {
        kfx_config_state = *state;
        for (const auto &t : tables)
            std::copy(t.second.begin(), t.second.end(), t.first);
    }
};

/**
 * list_pass: the game reads creature.cfg and magic.cfg overrides twice, a
 * CnfLd_ListOnly pass for the names and then the full pass (load_stats_files()),
 * so their variants replay both.
 */
void run_file_variants(const char *file, const ConfigFileData &fd, int64_t flags, bool list_pass, const std::vector<BlockKeys> &blocks) {
    Ports ports;
    Golden golden;
    load_all({});
    const FullSnapshot snap;
    // KFX_CONFIG_GOLDEN_FULL_RELOAD=1 reloads every file before each variant instead
    // of restoring the snapshot: slow, but checks the snapshot misses nothing.
    const bool full_reload = std::getenv("KFX_CONFIG_GOLDEN_FULL_RELOAD") != nullptr;
    for (const BlockKeys &bk : blocks) {
        for (const char *key : bk.keys) {
            for (const char *value : all_values()) {
                if (full_reload)
                    load_all({});
                else
                    snap.restore();
                research_log.clear();
                compat_report_clear();
                write_variant(bk.block, key, value);
                if (list_pass)
                    load_file(fd, scratch_file(), CnfLd_ListOnly | CnfLd_AcceptPartial | CnfLd_IgnoreErrors);
                load_file(fd, scratch_file(), flags | CnfLd_AcceptPartial | CnfLd_IgnoreErrors);
                golden.put(variant_name(file, bk.block, key, value), state_hash());
            }
        }
    }
    golden.check(file);
}
} // namespace

TEST_CASE("golden: every creature.cfg key with each awkward value", "[kfx_config][golden]") {
    run_file_variants("creature", keeper_creaturetp_file_data, CnfLd_Standard | CnfLd_PreListed, true, kCreatureBlocks);
}

TEST_CASE("golden: every magic.cfg spell and special key with each awkward value", "[kfx_config][golden]") {
    run_file_variants("magic", keeper_magic_file_data, CnfLd_Standard | CnfLd_PreListed, true, kMagicBlocks);
}

TEST_CASE("golden: every rules.cfg research and sacrifices key with each awkward value", "[kfx_config][golden]") {
    run_file_variants("rules", keeper_rules_file_data, CnfLd_Standard, false, kRulesBlocks);
}
