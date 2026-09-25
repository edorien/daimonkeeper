#include "ftest_config_content.h"

#ifdef FUNCTESTING

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <imgui.h>
#include "content_campaign_ops.h"
#include "cfgc_campaign_edit.h"
#include <spng.h>

extern "C" {
#include "pre_inc.h"
#include "../ftest.h"
#include "../ftest_util.h"
#include "game_legacy.h"
#include "config.h"
#include "config_keeperfx.h"
#include "config_trapdoor.h"
#include "config_objects.h"
#include "config_terrain.h"
#include "config_campaigns.h"
#include "lvl_filesdk1.h"
#include "kfx_editor.h"
#include "content_tools.h"
#include "content_rules.h"
#include "content_trapdoor.h"
#include "content_spells.h"
#include "content_creature.h"
#include "content_text.h"
#include "content_rooms.h"
#include "content_campaign.h"
#include "front_landview.h"
#include "frontend.h"
#include "vidfade.h"
#include "vidmode.h"
#include "kfx_frontend_state.h"
#include "config_terrain.h"
#include "kfx_sim_state.h"
#include "config_rules.h"
#include "config_magic.h"
#include "config_creature.h"
#include "thing_stats.h"
#include "kfx_config_state.h"
#include "post_inc.h"
}
#include "cfgc_writer.h"
#include "content_struct.h"
#include "content_strings.h"
#include "content_form.h"

namespace fs = std::filesystem;

namespace {

int64_t s_failures = 0;
int64_t s_checks = 0;

#define CHECK_EQ(what, actual, expected) \
    do { \
        s_checks++; \
        const int64_t a_ = (int64_t)(actual); \
        const int64_t e_ = (int64_t)(expected); \
        if (a_ != e_) { \
            s_failures++; \
            if (s_failures <= 30) \
                FTEST_FAIL_TEST("%s: got %" PRId64 ", expected %" PRId64, what, (int64_t)(a_), (int64_t)(e_)); \
        } \
    } while (0)

std::string dir_of(int64_t group)
{
    const char *p = prepare_file_path(group, "");
    return p != nullptr ? std::string(p) : std::string();
}

ConfigTarget live_target()
{
    ConfigTarget t;
    t.base_dir = dir_of(FGrp_FxData);
    t.base_crtr_dir = dir_of(FGrp_CrtrData);
    t.campaign_crtr_dir = dir_of(FGrp_CmpgCrtrs);
    t.campaign_cfg_dir = dir_of(FGrp_CmpgConfig);
    t.level_dir = dir_of(FGrp_CmpgLvls);
    t.level_number = get_selected_level_number();
    return t;
}

std::string upper(std::string s)
{
    for (char &c : s)
        c = (char)std::toupper((unsigned char)c);
    return s;
}

std::vector<std::string> words_of(const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string w;
    while (in >> w)
        out.push_back(w);
    return out;
}

bool is_number(const std::string &s)
{
    size_t i = (!s.empty() && s[0] == '-') ? 1 : 0;
    if (i >= s.size())
        return false;
    for (; i < s.size(); i++)
        if (!std::isdigit((unsigned char)s[i]))
            return false;
    return true;
}

const ConfigSchema &schema()
{
    static const ConfigSchema s = build_engine_schema();
    return s;
}

struct SetInfo
{
    const char *kind;
    const char *file;
    const struct NamedFieldSet *set;
};

const SetInfo *sets(size_t &n)
{
    static const SetInfo list[] = {
        {"trapdoor", "trapdoor.cfg", &trapdoor_trap_named_fields_set},
        {"trapdoor", "trapdoor.cfg", &trapdoor_door_named_fields_set},
        {"objects", "objects.cfg", &objects_named_fields_set},
        {"terrain", "terrain.cfg", &terrain_slab_named_fields_set},
        {"terrain", "terrain.cfg", &terrain_room_named_fields_set},
    };
    n = sizeof(list) / sizeof(list[0]);
    return list;
}

void compare_set(const SetInfo &info)
{
    const ConfigTarget target = live_target();
    const CfgFileSchema *fschema = schema().find(info.kind);
    ConfigStack stack = ConfigStack::load(target, info.kind, info.file, fschema);
    const CfgSectionSpec *sspec = fschema != nullptr ? fschema->find_section(info.set->block_basename) : nullptr;
    const std::string base = info.set->block_basename;
    if (sspec == nullptr)
    {
        s_failures++;
        FTEST_FAIL_TEST("schema has no section '%s'", base.c_str());
        return;
    }

    // Block count and names.
    const int64_t live_count = *info.set->get_count();
    CHECK_EQ((base + ": block count").c_str(), stack.section_count(base), live_count);

    CfgNameSets names;
    stack.collect_names(names);
    std::set<std::string> live_names;
    for (int64_t i = 0; i < info.set->max_count && info.set->names[i].name != nullptr; i++)
        if (info.set->names[i].name[0] != '\0')
            live_names.insert(upper(info.set->names[i].name));
    const std::set<std::string> *reg = names.find(base);
    CHECK_EQ((base + ": name registry present").c_str(), reg != nullptr, 1);
    if (reg != nullptr)
    {
        CHECK_EQ((base + ": registry size").c_str(), reg->size(), live_names.size());
        for (const std::string &n : live_names)
            CHECK_EQ((base + ": live name in registry: " + n).c_str(), reg->count(n), 1);
    }

    // Plain numeric fields: content layer (merged text, schema range) against the live struct.
    size_t compared = 0, aliased = 0;
    for (const struct NamedField *f = info.set->named_fields; f->name != nullptr; f++)
    {
        if (f->parse_func != value_default || f->assign_func != assign_default || f->namedCommand != nullptr)
            continue;
        const CfgFieldSpec *spec = sspec->find_field(f->name);
        if (spec == nullptr || spec->whole_string || (size_t)f->argnum >= spec->parts.size())
            continue;
        // Keys that write one struct field (Size_YZ / Size_Z): which of them the file set last decides the
        // value, so the pair is compared as a group by the schema test, not here.
        bool in_alias_group = !spec->alias_of.empty();
        for (const CfgFieldSpec &other : sspec->fields)
            in_alias_group = in_alias_group || strcasecmp(other.alias_of.c_str(), spec->key.c_str()) == 0;
        if (in_alias_group)
        {
            aliased++;
            continue;
        }
        const CfgValueSpec &part = spec->parts[(size_t)f->argnum];
        for (int64_t i = 0; i < live_count; i++)
        {
            int64_t expected = f->default_value;
            CfgEffective e;
            if (stack.effective(base + std::to_string(i), f->name, e) && e.values.size() == 1)
            {
                const std::vector<std::string> w = words_of(e.values[0]);
                if ((size_t)f->argnum < w.size())
                {
                    if (!is_number(w[(size_t)f->argnum]))
                        continue; // the loader warns and reads 0; not a value the schema describes
                    expected = std::atoll(w[(size_t)f->argnum].c_str());
                    expected = std::max(part.min, std::min(part.max, expected));
                }
            }
            const std::string what = base + std::to_string(i) + " " + f->name + "[" + std::to_string((int)f->argnum) + "]";
            CHECK_EQ(what.c_str(), get_named_field_value(f, info.set, i), expected);
            compared++;
        }
    }
    FTESTLOG("%s: %" PRId64 " blocks, %zu names, %zu values compared, %zu alias rows skipped", base.c_str(),
        (int64_t)live_count, live_names.size(), compared, aliased);
}

} // namespace

extern "C" {

// Creature model files (plan 04 C1): the stack of every creature (base, campaign creature folder, level file)
// against the live model stats, for the plain numeric attributes.
static void compare_creatures()
{
    const ConfigTarget target = live_target();
    ConfigStack global = ConfigStack::load(target, "creature", "creature.cfg", schema().find("creature"));
    CfgEffective list;
    if (!global.effective("common", "Creatures", list) || list.values.size() != 1)
    {
        s_failures++;
        FTEST_FAIL_TEST("creature.cfg has no Creatures list");
        return;
    }
    const std::vector<std::string> names = words_of(list.values[0]);
    struct Attr { const char *key; int64_t (*live)(const struct CreatureModelConfig *); };
    static const Attr attrs[] = {
        {"Health", [](const struct CreatureModelConfig *m) { return (int64_t)m->health; }},
        {"Strength", [](const struct CreatureModelConfig *m) { return (int64_t)m->strength; }},
        {"Armour", [](const struct CreatureModelConfig *m) { return (int64_t)m->armour; }},
        {"Dexterity", [](const struct CreatureModelConfig *m) { return (int64_t)m->dexterity; }},
        {"Defence", [](const struct CreatureModelConfig *m) { return (int64_t)m->defense; }},
        {"Luck", [](const struct CreatureModelConfig *m) { return (int64_t)m->luck; }},
        {"GoldHold", [](const struct CreatureModelConfig *m) { return (int64_t)m->gold_hold; }},
        {"HungerRate", [](const struct CreatureModelConfig *m) { return (int64_t)m->hunger_rate; }},
    };
    // The level's script may change stats at run time (SET_CREATURE_ARMOUR(KNIGHT,30)): that is not a config layer,
    // so a creature and attribute the script sets is left out of the comparison (plan 03 §6: the editors say so).
    std::string script;
    {
        char path[64];
        snprintf(path, sizeof(path), "map%05d.txt", 1); // the anchor test runs keeporig level 1
        std::ifstream f(target.level_dir + "/" + path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        script = ss.str();
        for (char &c : script)
            c = (char)std::toupper((unsigned char)c);
    }
    size_t compared = 0, skipped = 0;
    for (const std::string &name : names)
    {
        std::string file = name;
        for (char &c : file)
            c = (char)std::tolower((unsigned char)c);
        file += ".cfg";
        ConfigStack stack = ConfigStack::load(target, "creaturemodel", file, schema().find("creaturemodel"), true);
        const ThingModel model = (ThingModel)get_rid(creature_desc, name.c_str());
        if (model <= 0)
        {
            s_failures++;
            FTEST_FAIL_TEST("creature %s is not in the live list", name.c_str());
            continue;
        }
        for (const Attr &a : attrs)
        {
            CfgEffective e;
            if (!stack.effective("attributes", a.key, e) || e.values.size() != 1 || !is_number(e.values[0]))
                continue;
            std::string set_by_script = std::string("SET_CREATURE_") + upper(a.key) + "(" + upper(name);
            if (script.find(set_by_script) != std::string::npos)
            {
                skipped++;
                continue;
            }
            const std::string what = name + " " + a.key;
            CHECK_EQ(what.c_str(), a.live(&kfx_config_state.conf.crtr_conf.model[model]), std::atoll(e.values[0].c_str()));
            compared++;
        }
    }
    FTESTLOG("creatures: %zu creatures, %zu values compared, %zu set by the level script", names.size(), compared, skipped);
}

FTestActionResult ftest_config_content_anchor_action001__compare(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    size_t n = 0;
    const SetInfo *list = sets(n);
    for (size_t i = 0; i < n; i++)
        compare_set(list[i]);
    compare_creatures();
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    else
        FTESTLOG("%" PRId64 " of %" PRId64 " checks failed", (int64_t)s_failures, (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

static int64_t s_unused;

TbBool ftest_config_content_anchor_init()
{
    ftest_append_action(ftest_config_content_anchor_action001__compare, 20, &s_unused);
    return true;
}

} // extern "C"

/******************************************************************************/
// Loader read-back.
namespace {

const int64_t kTrap = 1;
const char *const kKeyCampaignOnly = "Health";       // set in the campaign layer only
const char *const kKeyBothLayers = "Shots";          // campaign, then overridden by the level layer
const char *const kKeyUntouched = "SellingValue";    // in neither
const char *const kCampaignHealth = "91";
const char *const kCampaignShots = "92";
const char *const kLevelShots = "82";

struct FileBackup
{
    std::string path;
    bool existed = false;
    std::string bytes;
    bool dir_existed = true;
};

FileBackup s_campaign_backup, s_level_backup;
ConfigTarget s_target;
int64_t s_expect_untouched = 0;
int64_t s_expect_health_base = 0;
int64_t s_expect_shots_base = 0;
bool s_prepared = false;

std::string slurp(const std::string &path, bool &exists)
{
    std::ifstream f(path, std::ios::binary);
    exists = (bool)f;
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

FileBackup backup(const std::string &path)
{
    FileBackup b;
    b.path = path;
    b.bytes = slurp(path, b.existed);
    b.dir_existed = fs::exists(fs::path(path).parent_path());
    return b;
}

void restore(const FileBackup &b)
{
    if (b.path.empty())
        return;
    WriteBatch batch;
    if (b.existed)
        batch.put(b.path, b.bytes);
    else
        batch.remove(b.path);
    std::string err;
    if (!batch.commit(&err))
        FTEST_FAIL_TEST("could not restore %s: %s", b.path.c_str(), err.c_str());
    std::error_code ec;
    if (!b.dir_existed)
        fs::remove(fs::path(b.path).parent_path(), ec); // only removes an empty directory
}

int64_t default_of(const struct NamedFieldSet *set, const char *name)
{
    for (const struct NamedField *f = set->named_fields; f->name != nullptr; f++)
        if (strcasecmp(f->name, name) == 0)
            return f->default_value;
    return 0;
}

// The lower layers' (untouched) value for a plain numeric key of [trap1], or the table default.
int64_t base_value(const ConfigStack &stack, const char *key)
{
    CfgEffective e;
    if (stack.effective("trap" + std::to_string(kTrap), key, e) && e.values.size() == 1 && is_number(e.values[0]))
        return std::atoll(e.values[0].c_str());
    return default_of(&trapdoor_trap_named_fields_set, key);
}

int64_t live_value(const char *key)
{
    for (const struct NamedField *f = trapdoor_trap_named_fields_set.named_fields; f->name != nullptr; f++)
        if (strcasecmp(f->name, key) == 0 && f->argnum == 0)
            return get_named_field_value(f, &trapdoor_trap_named_fields_set, kTrap);
    return -999999;
}

bool prepare(const char *who)
{
    s_target = live_target();
    if (s_target.campaign_cfg_dir.empty() || s_target.level_dir.empty())
    {
        FTEST_FAIL_TEST("%s: no campaign config or level directory to write to", who);
        return false;
    }
    s_campaign_backup = backup(s_target.path_for("trapdoor.cfg", CfgLayer_Campaign));
    s_level_backup = backup(s_target.path_for("trapdoor.cfg", CfgLayer_Level));
    const CfgFileSchema *fs_ = schema().find("trapdoor");
    ConfigStack before = ConfigStack::load(s_target, "trapdoor", "trapdoor.cfg", fs_);
    s_expect_untouched = base_value(before, kKeyUntouched);
    s_expect_health_base = base_value(before, kKeyCampaignOnly);
    s_expect_shots_base = base_value(before, kKeyBothLayers);
    FTESTLOG("scratch files: campaign %s (%s), level %s (%s)", s_campaign_backup.path.c_str(),
        s_campaign_backup.existed ? "exists" : "new", s_level_backup.path.c_str(), s_level_backup.existed ? "exists" : "new");
    return true;
}

bool commit(const ConfigContentWriter &w, CfgLayer layer, const ChangeSet &cs, const char *what)
{
    WriteBatch batch;
    ChangeResult res;
    if (!w.write(s_target, layer, "trapdoor.cfg", cs, batch, &res))
    {
        FTEST_FAIL_TEST("%s: writer refused", what);
        return false;
    }
    for (const CfgDiagnostic &d : res.diagnostics)
        if (d.severity != CfgSev_Info)
            FTESTLOG("%s: diagnostic %s: %s", what, d.code.c_str(), d.message.c_str());
    std::string err;
    if (!batch.commit(&err))
    {
        FTEST_FAIL_TEST("%s: commit failed: %s", what, err.c_str());
        return false;
    }
    return true;
}

} // namespace

extern "C" {

void ftest_config_content_readback_pre_start()
{
    s_prepared = false;
    if (!prepare("readback"))
        return;
    TrapDoorConfigWriter w(schema());
    const std::string id = "trap" + std::to_string(kTrap);
    ChangeSet camp_changes;
    camp_changes.set(id, kKeyCampaignOnly, kCampaignHealth).set(id, kKeyBothLayers, kCampaignShots);
    ChangeSet level;
    level.set(id, kKeyBothLayers, kLevelShots);
    if (!commit(w, CfgLayer_Campaign, camp_changes, "campaign write") || !commit(w, CfgLayer_Level, level, "level write"))
        return;
    s_prepared = true;
}

FTestActionResult ftest_config_content_readback_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_prepared)
    {
        FTEST_FAIL_TEST("pre-start writing did not complete");
        restore(s_level_backup);
        restore(s_campaign_backup);
        return FTRs_Go_To_Next_Action;
    }
    CHECK_EQ("campaign-only key reads the campaign value", live_value(kKeyCampaignOnly), std::atoll(kCampaignHealth));
    CHECK_EQ("key in both layers reads the level value", live_value(kKeyBothLayers), std::atoll(kLevelShots));
    CHECK_EQ("untouched key keeps the lower layers' value", live_value(kKeyUntouched), s_expect_untouched);
    // The stack agrees with what the loader did.
    const CfgFileSchema *fs_ = schema().find("trapdoor");
    ConfigStack after = ConfigStack::load(s_target, "trapdoor", "trapdoor.cfg", fs_);
    CfgEffective e;
    const std::string id = "trap" + std::to_string(kTrap);
    CHECK_EQ("stack: campaign-only source", after.effective(id, kKeyCampaignOnly, e) && e.source == CfgLayer_Campaign, 1);
    CHECK_EQ("stack: both-layers source", after.effective(id, kKeyBothLayers, e) && e.source == CfgLayer_Level, 1);
    CHECK_EQ("stack: both-layers beneath is the campaign value",
        e.has_beneath && e.beneath.size() == 1 && e.beneath[0] == kCampaignShots, 1);

    restore(s_level_backup);
    restore(s_campaign_backup);
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    else
        FTESTLOG("%" PRId64 " of %" PRId64 " checks failed", (int64_t)s_failures, (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_readback_init()
{
    ftest_append_action(ftest_config_content_readback_action001__check, 20, &s_unused);
    return true;
}

void ftest_config_content_reset_pre_start()
{
    s_prepared = false;
    if (!prepare("reset"))
        return;
    TrapDoorConfigWriter w(schema());
    const std::string id = "trap" + std::to_string(kTrap);
    ChangeSet set;
    set.set(id, kKeyBothLayers, kLevelShots);
    if (!commit(w, CfgLayer_Level, set, "level set"))
        return;
    bool exists = false;
    slurp(s_target.path_for("trapdoor.cfg", CfgLayer_Level), exists);
    if (s_level_backup.existed == false && !exists)
    {
        FTEST_FAIL_TEST("level file was not created");
        return;
    }
    ChangeSet reset;
    reset.reset(id, kKeyBothLayers);
    if (!commit(w, CfgLayer_Level, reset, "level reset"))
        return;
    slurp(s_target.path_for("trapdoor.cfg", CfgLayer_Level), exists);
    if (!s_level_backup.existed && exists)
        FTEST_FAIL_TEST("the generated level file should be deleted once its last override is reset");
    s_prepared = true;
}

FTestActionResult ftest_config_content_reset_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_prepared)
    {
        FTEST_FAIL_TEST("pre-start writing did not complete");
        restore(s_level_backup);
        return FTRs_Go_To_Next_Action;
    }
    CHECK_EQ("after Reset the key reads the lower layers' value", live_value(kKeyBothLayers), s_expect_shots_base);
    bool exists = false;
    slurp(s_target.path_for("trapdoor.cfg", CfgLayer_Level), exists);
    CHECK_EQ("no level file left behind", exists, s_level_backup.existed);
    restore(s_level_backup);
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    else
        FTESTLOG("%" PRId64 " of %" PRId64 " checks failed", (int64_t)s_failures, (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_reset_init()
{
    ftest_append_action(ftest_config_content_reset_action001__check, 20, &s_unused);
    return true;
}

static int64_t s_rules_expected = 0;
static int64_t s_rules_base_value = 0;
static bool s_rules_prepared = false;
static FileBackup s_rules_backup;

static int64_t live_rules_value(const char *key)
{
    for (int64_t b = 0; b < 8; b++)
        for (const struct NamedField *f = ruleblocks[b]; f->name != nullptr; f++)
            if (strcasecmp(f->name, key) == 0)
                return get_named_field_value(f, &rules_named_fields_set, 0);
    return -999999;
}

void ftest_config_content_rules_editor_pre_start()
{
    s_rules_prepared = false;
    const ConfigTarget target = live_target();
    if (target.level_dir.empty())
    {
        FTEST_FAIL_TEST("no level directory");
        return;
    }
    s_rules_backup = backup(target.path_for("rules.cfg", CfgLayer_Level));
    StructuredSession session;
    if (!session.open(target, "rules", "rules.cfg", CfgLayer_Level))
    {
        FTEST_FAIL_TEST("the Rules session did not open");
        return;
    }
    const FieldView before = session.value_of("game", "PayDaySpeed");
    s_rules_base_value = std::atoll(before.text.c_str());
    s_rules_expected = s_rules_base_value + 37;
    session.set("game", "PayDaySpeed", std::to_string((long long)s_rules_expected));
    std::string err;
    if (!session.apply(&err))
    {
        FTEST_FAIL_TEST("apply failed: %s", err.c_str());
        return;
    }
    // The form reads it back as this layer's value.
    const FieldView after = session.value_of("game", "PayDaySpeed");
    if (!after.overridden_here || after.source != CfgLayer_Level)
        FTEST_FAIL_TEST("after Apply the value is not shown as a level override");
    s_rules_prepared = true;
}

FTestActionResult ftest_config_content_rules_editor_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_rules_prepared)
    {
        FTEST_FAIL_TEST("pre-start editing did not complete");
    }
    else
        CHECK_EQ("the running game uses the edited [game] PayDaySpeed", live_rules_value("PayDaySpeed"), s_rules_expected);
    restore(s_rules_backup);
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_rules_editor_init()
{
    ftest_append_action(ftest_config_content_rules_editor_action001__check, 20, &s_unused);
    return true;
}

static std::string s_scratch_dir;
static LevelNumber s_scratch_level = 0;
static bool s_scratch_prepared = false;
static int64_t s_scratch_expected_speed = 0;
static FileBackup s_scratch_campaign_backup;

// Removes the "map900002.*" files of the scratch level from `dir` (nothing else).
static void remove_scratch_files(const std::string &dir)
{
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "map%05lld.", (long long)s_scratch_level);
    std::error_code ec;
    std::vector<fs::path> doomed;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename().string().compare(0, strlen(prefix), prefix) == 0)
            doomed.push_back(it->path());
    for (const fs::path &p : doomed)
        fs::remove(p, ec);
}

void ftest_config_content_scratch_level_pre_start()
{
    s_scratch_prepared = false;
    const ConfigTarget target = live_target();
    const LevelNumber lv = get_selected_level_number();
    s_scratch_level = lv;
    s_scratch_dir = target.level_dir;
    if (s_scratch_dir.empty() || target.campaign_cfg_dir.empty())
    {
        FTEST_FAIL_TEST("the campaign has no levels or configuration folder");
        return;
    }
    // A copy of keeporig level 1 under the scratch number, as the editor's Playtest would save it.
    std::error_code ec;
    for (fs::directory_iterator it(s_scratch_dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        if (name.compare(0, 9, "map00001.") == 0 && it->is_regular_file(ec))
        {
            char dst[64];
            snprintf(dst, sizeof(dst), "map%05lld.%s", (long long)lv, name.c_str() + 9);
            fs::copy_file(it->path(), fs::path(s_scratch_dir) / dst, fs::copy_options::overwrite_existing, ec);
        }
    }
    // Its own level-layer rules, through the Rules editor's session; and a campaign-layer trap value.
    s_scratch_campaign_backup = backup(target.path_for("trapdoor.cfg", CfgLayer_Campaign));
    StructuredSession rules;
    if (!rules.open(target, "rules", "rules.cfg", CfgLayer_Level))
    {
        FTEST_FAIL_TEST("the rules session did not open for the scratch level");
        return;
    }
    s_scratch_expected_speed = std::atoll(rules.value_of("game", "PayDaySpeed").text.c_str()) + 41;
    rules.set("game", "PayDaySpeed", std::to_string((long long)s_scratch_expected_speed));
    std::string err;
    if (!rules.apply(&err))
    {
        FTEST_FAIL_TEST("scratch level rules: %s", err.c_str());
        return;
    }
    StructuredSession traps;
    if (!traps.open(target, "trapdoor", "trapdoor.cfg", CfgLayer_Campaign))
    {
        FTEST_FAIL_TEST("the trapdoor session did not open at campaign scope");
        return;
    }
    traps.set("trap1", "Health", "93");
    if (!traps.apply(&err))
    {
        FTEST_FAIL_TEST("campaign trapdoor: %s", err.c_str());
        return;
    }
    s_scratch_prepared = true;
}

FTestActionResult ftest_config_content_scratch_level_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_scratch_prepared)
    {
        FTEST_FAIL_TEST("pre-start preparation did not complete");
    }
    else
    {
        CHECK_EQ("the scratch level was prepared under its own number", s_scratch_level, 900002);
        CHECK_EQ("the scratch level's own rules layer applies", live_rules_value("PayDaySpeed"), s_scratch_expected_speed);
        CHECK_EQ("the campaign's trapdoor layer applies to the scratch level", live_value("Health"), 93);
    }
    remove_scratch_files(s_scratch_dir);
    restore(s_scratch_campaign_backup);
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_scratch_level_init()
{
    ftest_append_action(ftest_config_content_scratch_level_action001__check, 20, &s_unused);
    return true;
}

static bool s_td_prepared = false;
static FileBackup s_td_backup;
static int64_t s_td_trap_base = 0, s_td_door_base = 0;

static int64_t live_field(const struct NamedFieldSet *set, const char *key, int64_t idx)
{
    for (const struct NamedField *f = set->named_fields; f->name != nullptr; f++)
        if (strcasecmp(f->name, key) == 0 && f->argnum == 0)
            return get_named_field_value(f, set, idx);
    return -999999;
}

void ftest_config_content_trapdoor_editor_pre_start()
{
    s_td_prepared = false;
    const ConfigTarget target = live_target();
    s_td_backup = backup(target.path_for("trapdoor.cfg", CfgLayer_Level));
    StructuredSession s;
    if (!s.open(target, "trapdoor", "trapdoor.cfg", CfgLayer_Level))
    {
        FTEST_FAIL_TEST("the trapdoor session did not open");
        return;
    }
    s_td_trap_base = std::atoll(s.value_of("trap1", "Health").text.c_str());
    s_td_door_base = std::atoll(s.value_of("door1", "Health").text.c_str());
    s.set("trap1", "Health", std::to_string((long long)s_td_trap_base + 11));
    s.set("door1", "Health", std::to_string((long long)s_td_door_base + 22));
    std::string err;
    if (!s.apply(&err))
    {
        FTEST_FAIL_TEST("apply failed: %s", err.c_str());
        return;
    }
    s_td_prepared = true;
}

FTestActionResult ftest_config_content_trapdoor_editor_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_td_prepared)
    {
        FTEST_FAIL_TEST("pre-start editing did not complete");
    }
    else
    {
        CHECK_EQ("the running game uses the edited trap1 Health", live_field(&trapdoor_trap_named_fields_set, "Health", 1), s_td_trap_base + 11);
        CHECK_EQ("the running game uses the edited door1 Health", live_field(&trapdoor_door_named_fields_set, "Health", 1), s_td_door_base + 22);
    }
    restore(s_td_backup);
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_trapdoor_editor_init()
{
    ftest_append_action(ftest_config_content_trapdoor_editor_action001__check, 20, &s_unused);
    return true;
}

static bool s_sp_prepared = false;
static FileBackup s_sp_backup;
static int64_t s_sp_cost_base = 0, s_sp_damage_base = 0;

void ftest_config_content_spell_editor_pre_start()
{
    s_sp_prepared = false;
    const ConfigTarget target = live_target();
    s_sp_backup = backup(target.path_for("magic.cfg", CfgLayer_Level));
    StructuredSession s;
    if (!s.open(target, "magic", "magic.cfg", CfgLayer_Level))
    {
        FTEST_FAIL_TEST("the magic session did not open");
        return;
    }
    // power2's Cost is an array of nine per-level values: change the first cell.
    const std::vector<std::string> cost = form_split_words(s.value_of("power2", "Cost").text);
    if (cost.size() != 9)
    {
        FTEST_FAIL_TEST("power2 Cost has %" PRId64 " values, expected 9", (int64_t)cost.size());
        return;
    }
    s_sp_cost_base = std::atoll(cost[0].c_str());
    std::vector<std::string> edited = cost;
    edited[0] = std::to_string((long long)s_sp_cost_base + 13);
    s.set("power2", "Cost", form_join_words(edited));
    s_sp_damage_base = std::atoll(s.value_of("shot1", "Damage").text.c_str());
    s.set("shot1", "Damage", std::to_string((long long)s_sp_damage_base + 7));
    std::string err;
    if (!s.apply(&err))
    {
        FTEST_FAIL_TEST("apply failed: %s", err.c_str());
        return;
    }
    s_sp_prepared = true;
}

FTestActionResult ftest_config_content_spell_editor_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_sp_prepared)
    {
        FTEST_FAIL_TEST("pre-start editing did not complete");
    }
    else
    {
        CHECK_EQ("the running game uses the edited power2 Cost[0]", live_field(&magic_powers_named_fields_set, "Cost", 2), s_sp_cost_base + 13);
        CHECK_EQ("the running game uses the edited shot1 Damage", live_field(&magic_shot_named_fields_set, "Damage", 1), s_sp_damage_base + 7);
    }
    restore(s_sp_backup);
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_spell_editor_init()
{
    ftest_append_action(ftest_config_content_spell_editor_action001__check, 20, &s_unused);
    return true;
}

static bool s_cr_prepared = false;
static FileBackup s_cr_model_backup, s_cr_global_backup;
static std::string s_cr_name;
static int64_t s_cr_health_base = 0, s_cr_pct_base = 0, s_cr_time_base = 0;

void ftest_config_content_creature_editor_pre_start()
{
    s_cr_prepared = false;
    const ConfigTarget target = live_target();
    StructuredSession global;
    if (!global.open(target, "creature", "creature.cfg", CfgLayer_Level))
    {
        FTEST_FAIL_TEST("the creature.cfg session did not open");
        return;
    }
    const std::vector<std::string> names = form_split_words(global.value_of("common", "Creatures").text);
    if (names.empty())
    {
        FTEST_FAIL_TEST("no creature list");
        return;
    }
    s_cr_name = names[0];
    const std::string file = form_lower(s_cr_name) + ".cfg";
    s_cr_model_backup = backup(target.path_for(file, CfgLayer_Level, true));
    s_cr_global_backup = backup(target.path_for("creature.cfg", CfgLayer_Level));
    StructuredSession model;
    if (!model.open(target, "creaturemodel", file, CfgLayer_Level, true))
    {
        FTEST_FAIL_TEST("the creature model session did not open");
        return;
    }
    s_cr_health_base = std::atoll(model.value_of("attributes", "Health").text.c_str());
    model.set("attributes", "Health", std::to_string((long long)s_cr_health_base + 50));
    s_cr_pct_base = std::atoll(global.value_of("experience", "HealthIncreaseOnExp").text.c_str());
    global.set("experience", "HealthIncreaseOnExp", std::to_string((long long)s_cr_pct_base + 5));
    s_cr_time_base = std::atoll(global.value_of("instance1", "Time").text.c_str());
    global.set("instance1", "Time", std::to_string((long long)s_cr_time_base + 3));
    std::string err;
    if (!model.apply(&err) || !global.apply(&err))
    {
        FTEST_FAIL_TEST("apply failed: %s", err.c_str());
        return;
    }
    s_cr_prepared = true;
}

FTestActionResult ftest_config_content_creature_editor_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_cr_prepared)
    {
        FTEST_FAIL_TEST("pre-start editing did not complete");
    }
    else
    {
        const ThingModel model = (ThingModel)get_rid(creature_desc, s_cr_name.c_str());
        CHECK_EQ("the creature is in the live list", model > 0, 1);
        CHECK_EQ("the running game uses the edited Health", kfx_config_state.conf.crtr_conf.model[model].health, s_cr_health_base + 50);
        CHECK_EQ("the running game uses the edited [experience] HealthIncreaseOnExp", kfx_config_state.conf.crtr_conf.exp.health_increase_on_exp, s_cr_pct_base + 5);
        CHECK_EQ("the running game uses the edited ability Time", kfx_config_state.conf.magic_conf.instance_info[1].time, s_cr_time_base + 3);
        // The editor's preview formula against the engine's, at experience level 4 (index 3).
        const int64_t base = kfx_config_state.conf.crtr_conf.model[model].health;
        CHECK_EQ("the preview formula equals compute_creature_max_health()", creature_level_value(base, kfx_config_state.conf.crtr_conf.exp.health_increase_on_exp, 3),
            compute_creature_max_health(base, 3));
    }
    restore(s_cr_model_backup);
    restore(s_cr_global_backup);
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_creature_editor_init()
{
    ftest_append_action(ftest_config_content_creature_editor_action001__check, 20, &s_unused);
    return true;
}

static bool s_tx_prepared = false;
static std::string s_tx_path;
static bool s_tx_existed = false;
static size_t s_tx_id = 0;
static std::string s_tx_base_before;

void ftest_config_content_text_editor_pre_start()
{
    s_tx_prepared = false;
    const ConfigTarget target = live_target();
    const StringsPaths paths = content_strings_paths(content_root(), nullptr, target.level_dir, target.level_number, "eng");
    s_tx_path = paths.level;
    bool exists = false;
    slurp(s_tx_path, exists);
    s_tx_existed = exists;
    StringsSession s;
    if (!s.open(paths, "eng", CfgLayer_Level) || !s.writable())
    {
        FTEST_FAIL_TEST("the level string session is not writable: %s", s.why_read_only().c_str());
        return;
    }
    s_tx_id = s.next_free_id();
    s_tx_base_before = s.view(1).text;
    s.set_text(s_tx_id, "Caf\xC3\xA9 test\nsecond line");
    std::string err;
    if (!s.apply(&err))
    {
        FTEST_FAIL_TEST("apply failed: %s", err.c_str());
        return;
    }
    s_tx_prepared = true;
}

FTestActionResult ftest_config_content_text_editor_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_tx_prepared)
    {
        FTEST_FAIL_TEST("pre-start editing did not complete");
    }
    else
    {
        const std::string got = get_string((TextStringId)s_tx_id);
        CHECK_EQ("the loader returns the edited string as UTF-8 with its line break", got == "Caf\xC3\xA9 test\r\nsecond line", 1);
        CHECK_EQ("a string the level file does not touch is unchanged", std::string(get_string(1)) == s_tx_base_before, 1);
    }
    if (!s_tx_existed)
    {
        std::error_code ec;
        fs::remove(s_tx_path, ec);
    }
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_text_editor_init()
{
    ftest_append_action(ftest_config_content_text_editor_action001__check, 20, &s_unused);
    return true;
}


static bool s_cm_prepared = false;
static std::string s_cm_file, s_cm_dir;
static std::vector<int64_t> s_cm_levels_after;
static int64_t s_cm_next_before = 0, s_cm_next_after = 0, s_cm_files_none = 0, s_cm_files_count = 0;
static bool s_cm_menu_ok = false, s_cm_menu_moved = false, s_cm_rules_created = false;
static std::string s_cm_rules_text;
static bool s_pk_created = false, s_pk_listed = false, s_pk_id_taken = false, s_pk_copy_pack = false, s_pk_copy_extra = false, s_pk_copy_taken = false;
static bool s_pk_extra_file = false, s_pk_moved = false, s_pk_src_lists = false, s_pk_dst_lists = false;
static int64_t s_pk_pack_levels = 0;
static int64_t s_cm_return_state = -1, s_cm_reopen_tool = -1;
static bool s_cm_play_cleared = false;
static bool s_cm_created = false, s_cm_id_free_after = true, s_cm_created_dirs = false;
static std::string s_cm_created_name;
static int64_t s_cm_new_first = 0;
static std::string s_cm_levels_text;
static std::string s_cm_own_crtr, s_cm_name_after, s_cm_cfg_after, s_cm_shared_dir, s_cm_own_dir, s_cm_keeporig_before, s_cm_keeporig_after;

static int64_t count_files(const std::string &dir)
{
    int64_t n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec))
            n++;
    return n;
}

void ftest_config_content_campaign_editor_pre_start()
{
    s_cm_prepared = false;
    const std::string root = content_root();
    std::string cfg_loc, crtr_loc;
    for (const ContentCampaign &c : content_list_campaigns())
        if (c.fname == "keeporig.cfg")
        {
            cfg_loc = c.cfg_dir.substr(root.size() + 1);
            crtr_loc = c.crtr_dir.substr(root.size() + 1);
            s_cm_shared_dir = c.cfg_dir;
            bool exists = false;
            s_cm_keeporig_before = slurp(c.cfg_file, exists);
        }
    if (cfg_loc.empty())
    {
        FTEST_FAIL_TEST("keeporig has no configuration folder");
        return;
    }
    for (const char *leftover : {"/campgns/ft_camp_cfg", "/campgns/ft_camp_crtr", "/levels/ft_camp_cfg", "/levels/ft_camp_crtr"})
        fs::remove_all(root + leftover);
    s_cm_file = root + "/campgns/ft_camp.cfg";
    s_cm_dir = root + "/campgns/ft_camp";
    fs::create_directories(s_cm_dir);
    {
        std::ofstream out(s_cm_file, std::ios::binary);
        out << "; scratch\n[common]\nNAME = FT Campaign\nLEVELS_LOCATION = campgns/ft_camp\nCONFIGS_LOCATION = " << cfg_loc
            << "\nCREATURES_LOCATION = " << crtr_loc << "\nSINGLE_LEVELS = 1\n";
    }
    for (int n = 1; n <= 3; n++) // the levels the test lists exist as map files
        std::ofstream(s_cm_dir + "/map0000" + std::to_string(n) + ".slb", std::ios::binary) << "x";
    if (!content_campaign_test_select("ft_camp.cfg"))
    {
        FTEST_FAIL_TEST("the scratch campaign did not open in the Campaign editor");
        return;
    }
    content_campaign_test_set("common", "NAME", "FT Renamed");
    content_campaign_test_set("common", "HUMAN_PLAYER", "BLUE");
    if (!content_campaign_test_apply())
    {
        FTEST_FAIL_TEST("apply failed");
        return;
    }
    for (const ContentCampaign &c : content_list_campaigns())
        if (c.fname == "ft_camp.cfg")
            s_cm_name_after = c.name;
    // The Levels page: lists, parallel bonus, entries, reorder, remove.
    if (!content_campaign_test_level_op("add_single", 2, 0) || !content_campaign_test_level_op("add_single", 3, 0)
        || !content_campaign_test_level_op("add_extra", 50, 0) || !content_campaign_test_level_op("bonus", 100, 3)
        || !content_campaign_test_level_op("up", 3, 0) || !content_campaign_test_level_op("add_single", 9, 0)
        || !content_campaign_test_level_op("remove", 9, 0) || !content_campaign_test_apply())
    {
        FTEST_FAIL_TEST("the level list edits failed");
        return;
    }
    // A map saved into the campaign (Map Editor Save As): number, list entry and [map] entry, without a restart.
    s_cm_next_before = content_campaign_next_level("ft_camp.cfg", CampList_Single);
    std::string reg_err;
    if (!content_campaign_register_level("ft_camp.cfg", s_cm_next_before, CampList_Single, "Saved Map", &reg_err))
    {
        FTEST_FAIL_TEST("registering a saved level failed: %s", reg_err.c_str());
        return;
    }
    s_cm_next_after = content_campaign_next_level("ft_camp.cfg", CampList_Single);
    // A game started by the Campaign editor ends: the frontend goes to the main menu and asks for the tool to be opened again.
    content_tool_play_running = true;
    content_tool_return_tool = ContentTool_Campaign;
    s_cm_return_state = (int64_t)get_startup_menu_state();
    s_cm_reopen_tool = content_tool_reopen_tool;
    s_cm_play_cleared = !content_tool_play_running && content_tool_return_tool == -1;
    content_tool_reopen_tool = -1;
    // Config files page and menu order.
    {
        // ft_camp lists the shared classic folders until it gets its own, so create the file after "own configuration" below.
        size_t pos = 0, cnt = 0;
        s_cm_menu_ok = content_campaign_menu_position("ft_camp.cfg", &pos, &cnt);
        const std::string order_path = root + "/campgns/campgn_order.txt";
        bool order_existed = false;
        const std::string order_before = slurp(order_path, order_existed);
        std::string move_err;
        size_t pos2 = pos;
        if (s_cm_menu_ok && pos > 0 && content_campaign_move_in_menu("ft_camp.cfg", -1, &move_err))
            content_campaign_menu_position("ft_camp.cfg", &pos2, &cnt);
        s_cm_menu_moved = pos > 0 && pos2 + 1 == pos;
        // Put the order file back as it was.
        if (order_existed)
            std::ofstream(order_path, std::ios::binary) << order_before;
        else
            fs::remove(order_path);
        load_campaigns_list(&campaigns_list, FGrp_Campgn, "campaigns", "campgn_order.txt");
    }
    // A new campaign from the wizard's function: file, folders, and the game's list.
    fs::remove_all(root + "/campgns/ft_newcamp");
    fs::remove_all(root + "/campgns/ft_newcamp_cfg");
    fs::remove_all(root + "/campgns/ft_newcamp_crtr");
    fs::remove_all(root + "/campgns/ft_newcamp_lnd");
    fs::remove(root + "/campgns/ft_newcamp.cfg");
    std::string create_err;
    s_cm_created = content_campaign_create("FT New Campaign", "ft_newcamp", "BLUE", true, &create_err);
    s_cm_id_free_after = content_campaign_id_free("ft_newcamp");
    s_cm_new_first = content_campaign_next_level("ft_newcamp.cfg", CampList_Single);
    for (const ContentCampaign &c : content_list_campaigns())
        if (c.fname == "ft_newcamp.cfg")
        {
            s_cm_created_name = c.name;
            s_cm_created_dirs = fs::is_directory(c.levels_dir) && fs::is_directory(c.cfg_dir) && fs::is_directory(c.crtr_dir);
        }
    for (const ContentCampaign &c : content_list_campaigns())
        if (c.fname == "ft_camp.cfg")
            for (int64_t n : c.levels)
                if (n > 0) // the loader lists the "no bonus level" slots as 0
                    s_cm_levels_after.push_back(n);
    bool lv_exists = false;
    s_cm_levels_text = slurp(s_cm_file, lv_exists);
    // Map packs and copying a level between campaigns and packs.
    {
        for (const char *d : {"/levels/ft_pack", "/levels/ft_pack_cfg", "/levels/ft_pack_crtr"})
            fs::remove_all(root + d);
        fs::remove(root + "/levels/ft_pack.cfg");
        std::string perr;
        s_pk_created = content_pack_create("FT Pack", "ft_pack", ContentKind_FreePlay, "RED", true, &perr);
        auto find_c = [](const char *fname, ContentKind kind) {
            for (const ContentCampaign &c : content_list_everything())
                if (c.fname == fname && c.kind == kind)
                    return c;
            return ContentCampaign();
        };
        const ContentCampaign pack = find_c("ft_pack.cfg", ContentKind_FreePlay);
        s_pk_listed = !pack.fname.empty() && !pack.listed; // an empty pack is known to the editor, not to the game
        s_pk_id_taken = !content_campaign_id_free("ft_pack", ContentKind_FreePlay) && content_campaign_id_free("ft_pack", ContentKind_Multiplayer);
        const ContentCampaign camp = find_c("ft_camp.cfg", ContentKind_Campaign);
        // Copy level 1 into the pack as level 1, and into the campaign as an extra level 20.
        std::string cerr1, cerr2, cerr3;
        s_pk_copy_pack = content_campaign_copy_level(camp, 1, pack, content_pack_next_level(pack), CampList_Single, false, &cerr1);
        s_pk_copy_extra = content_campaign_copy_level(camp, 1, camp, 20, CampList_Extra, false, &cerr2);
        s_pk_copy_taken = !content_campaign_copy_level(camp, 1, camp, 20, CampList_Extra, false, &cerr3); // 20 has files now
        s_pk_pack_levels = (int64_t)content_pack_levels(find_c("ft_pack.cfg", ContentKind_FreePlay)).size();
        s_pk_extra_file = cfgc_file_exists_ci(s_cm_dir + "/map00020.slb");
        // Move level 2 into ft_newcamp: its lists gain it, the source's lose it and its entry; the source's files stay.
        const ContentCampaign other = find_c("ft_newcamp.cfg", ContentKind_Campaign);
        std::string cerr4;
        s_pk_moved = content_campaign_copy_level(find_c("ft_camp.cfg", ContentKind_Campaign), 2, other, 2, CampList_Single, true, &cerr4);
        bool ex = false;
        const std::string src_after = slurp(s_cm_file, ex);
        const ConfigContent sc = read_config_content(ConfigDocument::parse(src_after), "campaign", false);
        s_pk_src_lists = !cfgc_read_levels(sc).listed(2) && sc.find_section("map", 2) == nullptr && cfgc_file_exists_ci(s_cm_dir + "/map00002.slb");
        const std::string dst_after = slurp(other.cfg_file, ex);
        s_pk_dst_lists = cfgc_read_levels(read_config_content(ConfigDocument::parse(dst_after), "campaign", false)).listed(2);
        fs::remove_all(root + "/levels/ft_pack");
        fs::remove_all(root + "/levels/ft_pack_cfg");
        fs::remove_all(root + "/levels/ft_pack_crtr");
        fs::remove(root + "/levels/ft_pack.cfg");
        content_campaign_rescan_lists();
    }
    content_campaign_test_select("ft_camp.cfg"); // the file changed behind the window: open it again
    if (!content_campaign_test_own_config())
    {
        FTEST_FAIL_TEST("giving the campaign its own configuration failed");
        return;
    }
    for (const ContentCampaign &c : content_list_campaigns())
        if (c.fname == "ft_camp.cfg")
        {
            s_cm_own_dir = c.cfg_dir;
            s_cm_own_crtr = c.crtr_dir;
        }
    // Config files page: create the campaign's own rules.cfg (in its own folder now).
    s_cm_files_none = content_campaign_test_create_file("nosuchfile.cfg");
    s_cm_files_count = content_campaign_test_create_file("rules.cfg");
    bool exists = false;
    s_cm_rules_text = slurp(s_cm_own_dir + "/rules.cfg", exists);
    s_cm_rules_created = exists;
    s_cm_cfg_after = slurp(s_cm_file, exists);
    for (const ContentCampaign &c : content_list_campaigns())
        if (c.fname == "keeporig.cfg")
            s_cm_keeporig_after = slurp(c.cfg_file, exists);
    s_cm_prepared = true;
}

FTestActionResult ftest_config_content_campaign_editor_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_cm_prepared)
    {
        FTEST_FAIL_TEST("pre-start editing did not complete");
    }
    else
    {
        CHECK_EQ("the game's lists hold the single, bonus and extra levels, and the level saved into the campaign",
            s_cm_levels_after == std::vector<int64_t>({1, 2, 3, 4, 50, 100}), 1);
        CHECK_EQ("a new campaign is created, listed under its name, with its folders", s_cm_created && s_cm_created_name == "FT New Campaign" && s_cm_created_dirs, 1);
        CHECK_EQ("the campaign moves up one place in the menu, and the order file goes back", s_cm_menu_ok && s_cm_menu_moved, 1);
        CHECK_EQ("the Config files page creates the campaign's own rules.cfg with a comment line", s_cm_files_none == -1 && s_cm_files_count >= 1
            && s_cm_rules_created && s_cm_rules_text.compare(0, 2, "; ") == 0, 1);
        CHECK_EQ("the end of a game the Campaign editor started returns to the main menu and reopens the editor",
            s_cm_return_state == (int64_t)FeSt_MAIN_MENU && s_cm_reopen_tool == (int64_t)ContentTool_Campaign && s_cm_play_cleared, 1);
        CHECK_EQ("a map pack is created and known to the editor before the game lists it, its id taken per kind", s_pk_created && s_pk_listed && s_pk_id_taken, 1);
        CHECK_EQ("a level is copied into a pack and into a campaign as an extra level, and a taken number is refused",
            s_pk_copy_pack && s_pk_copy_extra && s_pk_copy_taken && s_pk_pack_levels == 1 && s_pk_extra_file, 1);
        CHECK_EQ("a move lists the level in the target, takes it out of the source's lists and entry, and keeps its files",
            s_pk_moved && s_pk_src_lists && s_pk_dst_lists, 1);
        CHECK_EQ("its id is taken afterwards", !s_cm_id_free_after, 1);
        CHECK_EQ("the first map saved into it takes level 1", s_cm_new_first == 1, 1);
        CHECK_EQ("the next level number is after the listed ones, and a listed level with no map yet is offered first", s_cm_next_before == 4 && s_cm_next_after == 4, 1);
        CHECK_EQ("the lists are written in order with the bonus level parallel", std::regex_search(s_cm_levels_text, std::regex("SINGLE_LEVELS *= *1 +3 +2"))
            && std::regex_search(s_cm_levels_text, std::regex("BONUS_LEVELS *= *0 +100 +0")), 1);
        CHECK_EQ("a level saved into the campaign has its entry", s_cm_levels_text.find("Saved Map") != std::string::npos, 1);
        CHECK_EQ("a new level gets a five-digit entry with a name", s_cm_levels_text.find("[map00002]") != std::string::npos
            && std::regex_search(s_cm_levels_text, std::regex("NAME_TEXT +=? *Level 2")), 1);
        CHECK_EQ("a removed level's entry is gone", s_cm_levels_text.find("map00009") == std::string::npos, 1);
        CHECK_EQ("the pack list shows the new name without a restart", s_cm_name_after == "FT Renamed", 1);
        CHECK_EQ("the file has the new HUMAN_PLAYER", std::regex_search(s_cm_cfg_after, std::regex("HUMAN_PLAYER +=? *BLUE")), 1);
        CHECK_EQ("the comment line survived", s_cm_cfg_after.compare(0, 9, "; scratch") == 0, 1);
        CHECK_EQ("the campaign points at its own configuration folder", s_cm_own_dir.find("ft_camp_cfg") != std::string::npos, 1);
        CHECK_EQ("the own folder holds a copy of every shared file", count_files(s_cm_own_dir) == count_files(s_cm_shared_dir) && count_files(s_cm_own_dir) > 0, 1);
        CHECK_EQ("the shared campaign's file is untouched", s_cm_keeporig_before == s_cm_keeporig_after, 1);
    }
    std::error_code ec;
    fs::remove(s_cm_file, ec);
    fs::remove(content_root() + "/campgns/ft_newcamp.cfg", ec);
    fs::remove_all(content_root() + "/campgns/ft_newcamp", ec);
    fs::remove_all(content_root() + "/campgns/ft_newcamp_cfg", ec);
    fs::remove_all(content_root() + "/campgns/ft_newcamp_crtr", ec);
    fs::remove_all(content_root() + "/campgns/ft_newcamp_lnd", ec);
    fs::remove_all(s_cm_dir, ec);
    fs::remove_all(s_cm_own_dir, ec);
    fs::remove_all(s_cm_own_crtr, ec);
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_campaign_editor_init()
{
    ftest_append_action(ftest_config_content_campaign_editor_action001__check, 20, &s_unused);
    return true;
}


static bool s_lv_prepared = false;
static bool s_lv_loaded_png = false, s_lv_loaded_raw = false;
static bool s_lv_switched = false;
static int s_lv_png_pixel = -1, s_lv_png_red = -1, s_lv_raw_pixel = -1;

// A 1280 x 960 indexed PNG whose every pixel is index 5, with palette entry 5 = (252, 0, 0).
static std::string make_test_png()
{
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);
    struct spng_ihdr ihdr = {};
    ihdr.width = 1280;
    ihdr.height = 960;
    ihdr.bit_depth = 8;
    ihdr.color_type = SPNG_COLOR_TYPE_INDEXED;
    spng_set_ihdr(ctx, &ihdr);
    struct spng_plte plte = {};
    plte.n_entries = 8;
    plte.entries[5].red = 252;
    spng_set_plte(ctx, &plte);
    std::vector<unsigned char> px((size_t)1280 * 960, 5);
    spng_encode_image(ctx, px.data(), px.size(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    size_t len = 0;
    int err = 0;
    void *buf = spng_get_png_buffer(ctx, &len, &err);
    std::string out((const char *)buf, len);
    free(buf);
    spng_ctx_free(ctx);
    return out;
}

void ftest_config_content_landview_png_pre_start()
{
    s_lv_prepared = false;
    s_lv_loaded_png = s_lv_loaded_raw = false;
    const std::string root = content_root();
    for (const char *d : {"/campgns/ft_land", "/campgns/ft_land_cfg", "/campgns/ft_land_crtr", "/campgns/ft_land_lnd"})
        fs::remove_all(root + d);
    fs::remove(root + "/campgns/ft_land.cfg");
    std::string err;
    if (!content_campaign_create("FT Land", "ft_land", "RED", false, &err))
    {
        FTEST_FAIL_TEST("could not create the scratch campaign: %s", err.c_str());
        return;
    }
    const std::string lnd = root + "/campgns/ft_land_lnd";
    if (!fs::is_regular_file(lnd + "/rgmap00.raw"))
    {
        // No default land images in this data tree: nothing to test the fallback against.
        FTESTLOG("no default land images to copy; the PNG path alone is checked");
    }
    {
        const std::string png = make_test_png();
        std::ofstream(lnd + "/rgmap00.png", std::ios::binary).write(png.data(), (long)png.size());
    }
    if (!change_campaign(CampgnT_Campaign, "ft_land.cfg"))
    {
        FTEST_FAIL_TEST("could not switch to the scratch campaign");
        return;
    }
    // load_map_and_window() keeps a backup of the palette, a buffer the land screen allocates: give it one for the test.
    static unsigned char s_backup_palette[768];
    unsigned char *const saved_backup = frontend_backup_palette;
    unsigned char saved_palette[768];
    memcpy(saved_palette, frontend_palette, sizeof(saved_palette));
    if (frontend_backup_palette == NULL)
        frontend_backup_palette = s_backup_palette;
    s_lv_switched = strcasecmp(campaign.fname, "ft_land.cfg") == 0;
    if (load_map_and_window(SINGLEPLAYER_NOTSTARTED))
    {
        s_lv_loaded_png = true;
        s_lv_png_pixel = kfx_frontend_state.land_map_start[0];
        s_lv_png_red = frontend_palette[15];
        unload_map_and_window();
    }
    // Without the PNG the .raw + .pal pair is used, as before.
    fs::remove(lnd + "/rgmap00.png");
    if (fs::is_regular_file(lnd + "/rgmap00.raw") && load_map_and_window(SINGLEPLAYER_NOTSTARTED))
    {
        s_lv_loaded_raw = true;
        s_lv_raw_pixel = kfx_frontend_state.land_map_start[0];
        unload_map_and_window();
    }
    frontend_backup_palette = saved_backup;
    memcpy(frontend_palette, saved_palette, sizeof(saved_palette));
    change_campaign(CampgnT_Campaign, "keeporig.cfg");
    s_lv_prepared = true;
}

FTestActionResult ftest_config_content_landview_png_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_lv_prepared)
    {
        FTEST_FAIL_TEST("pre-start work did not complete");
    }
    else
    {
        CHECK_EQ("the game loads the new campaign (it has the blocks the loader needs)", s_lv_switched, 1);
        CHECK_EQ("the game loads the land view from a PNG", s_lv_loaded_png, 1);
        CHECK_EQ("the PNG's pixels are the land map", s_lv_png_pixel == 5, 1);
        CHECK_EQ("the PNG's palette (6 bits) is the land palette", s_lv_png_red == 63, 1);
        CHECK_EQ("the .raw + .pal pair still loads without the PNG", s_lv_loaded_raw || !fs::is_regular_file(content_root() + "/campgns/keeporig_lnd/rgmap00.raw"), 1);
    }
    std::error_code ec;
    for (const char *d : {"/campgns/ft_land", "/campgns/ft_land_cfg", "/campgns/ft_land_crtr", "/campgns/ft_land_lnd"})
        fs::remove_all(content_root() + d, ec);
    fs::remove(content_root() + "/campgns/ft_land.cfg", ec);
    load_campaigns_list(&campaigns_list, FGrp_Campgn, "campaigns", "campgn_order.txt");
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_landview_png_init()
{
    ftest_append_action(ftest_config_content_landview_png_action001__check, 20, &s_unused);
    return true;
}

static bool s_rm_prepared = false;
static FileBackup s_rm_backup;
static int64_t s_rm_cost = 0, s_rm_gold = 0, s_rm_health = 0;

void ftest_config_content_room_editor_pre_start()
{
    s_rm_prepared = false;
    const ConfigTarget target = live_target();
    s_rm_backup = backup(target.path_for("terrain.cfg", CfgLayer_Level));
    StructuredSession s;
    if (!s.open(target, "terrain", "terrain.cfg", CfgLayer_Level))
    {
        FTEST_FAIL_TEST("the terrain session did not open");
        return;
    }
    s_rm_cost = std::atoll(s.value_of("room2", "Cost").text.c_str()) + 7;
    s_rm_gold = std::atoll(s.value_of("slab1", "GoldHeld").text.c_str()) + 11;
    s_rm_health = std::atoll(s.value_of("block_health", "DIRT").text.c_str()) + 3;
    s.set("room2", "Cost", std::to_string((long long)s_rm_cost));
    s.set("slab1", "GoldHeld", std::to_string((long long)s_rm_gold));
    s.set("block_health", "DIRT", std::to_string((long long)s_rm_health));
    std::string err;
    if (!s.apply(&err))
    {
        FTEST_FAIL_TEST("apply failed: %s", err.c_str());
        return;
    }
    s_rm_prepared = true;
}

FTestActionResult ftest_config_content_room_editor_action001__check(struct FTestActionArgs *const args)
{
    s_failures = 0;
    s_checks = 0;
    if (!s_rm_prepared)
    {
        FTEST_FAIL_TEST("pre-start editing did not complete");
    }
    else
    {
        CHECK_EQ("the running game uses the edited room2 Cost", live_field(&terrain_room_named_fields_set, "Cost", 2), s_rm_cost);
        CHECK_EQ("the running game uses the edited slab1 GoldHeld", live_field(&terrain_slab_named_fields_set, "GoldHeld", 1), s_rm_gold);
        CHECK_EQ("the running game uses the edited [block_health] DIRT", (int64_t)kfx_sim_state.block_health[0], s_rm_health);
    }
    restore(s_rm_backup);
    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)s_checks);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_room_editor_init()
{
    ftest_append_action(ftest_config_content_room_editor_action001__check, 20, &s_unused);
    return true;
}

static int64_t s_smoke_frames = 0;
static int64_t s_smoke_tab = 0;
static int s_smoke_imgui_frame0 = 0;

FTestActionResult ftest_config_content_tool_smoke_action001__open_editor(struct FTestActionArgs *const args)
{
    editor_open(1, false);
    if (!editor_is_active())
    {
        FTEST_FAIL_TEST("editor_open() did not activate the session");
        return FTRs_Go_To_Next_Action;
    }
    content_tools_open_for_map(ContentTool_ConfigFiles);
    if (!content_tools_is_open())
        FTEST_FAIL_TEST("the map-host tool did not open");
    s_smoke_frames = 0;
    s_smoke_imgui_frame0 = ImGui::GetFrameCount();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action002__draw_map_host(struct FTestActionArgs *const args)
{
    if (++s_smoke_frames < 40)
        return FTRs_Repeat_Current_Action;
    if (!content_tools_is_open())
        FTEST_FAIL_TEST("the tool closed by itself");
    content_tools_open(ContentTool_ConfigFiles); // standalone host: campaign picker, layers, file list
    s_smoke_frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action003__draw_standalone(struct FTestActionArgs *const args)
{
    if (++s_smoke_frames < 40)
        return FTRs_Repeat_Current_Action;
    if (!content_tools_is_open())
        FTEST_FAIL_TEST("the standalone tool closed by itself");
    // The window is only exercised if ImGui frames really ran while it was open.
    const int frames = ImGui::GetFrameCount() - s_smoke_imgui_frame0;
    if (frames < 40)
        FTEST_FAIL_TEST("only %" PRId64 " ImGui frames ran while the window was open", (int64_t)frames);
    FTESTLOG("drew the Config Files window for %" PRId64 " ImGui frames in both hosts", (int64_t)frames);
    // On to the Rules editor: map host first, then standalone.
    content_tools_open_for_map(ContentTool_Rules);
    s_smoke_frames = 0;
    s_smoke_imgui_frame0 = ImGui::GetFrameCount();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action004__draw_rules_map(struct FTestActionArgs *const args)
{
    if (++s_smoke_frames < 40)
        return FTRs_Repeat_Current_Action;
    content_tools_open(ContentTool_Rules);
    s_smoke_frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action005__draw_rules_standalone(struct FTestActionArgs *const args)
{
    if (++s_smoke_frames < 40)
        return FTRs_Repeat_Current_Action;
    const int frames = ImGui::GetFrameCount() - s_smoke_imgui_frame0;
    if (frames < 40)
        FTEST_FAIL_TEST("only %" PRId64 " ImGui frames ran while the Rules editor was open", (int64_t)frames);
    FTESTLOG("drew the Rules editor for %" PRId64 " ImGui frames in both hosts", (int64_t)frames);
    // Every tab, so each layout (value rows, research lines, sacrifice recipes) is drawn at least once.
    s_smoke_tab = 0;
    s_smoke_frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action006__draw_rules_tabs(struct FTestActionArgs *const args)
{
    static const char *const tabs[] = {"creatures", "rooms", "magic", "computer", "workers", "health", "research", "sacrifices"};
    const int64_t count = (int64_t)(sizeof(tabs) / sizeof(tabs[0]));
    if (s_smoke_frames == 0)
        content_rules_show_tab(tabs[s_smoke_tab]);
    if (++s_smoke_frames < 6)
        return FTRs_Repeat_Current_Action;
    s_smoke_frames = 0;
    if (++s_smoke_tab < count)
        return FTRs_Repeat_Current_Action;
    FTESTLOG("drew all %" PRId64 " remaining Rules editor tabs", count);
    content_tools_open_for_map(ContentTool_TrapDoor);
    s_smoke_tab = 0;
    s_smoke_frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action007__draw_trapdoor(struct FTestActionArgs *const args)
{
    // Traps, then doors: every group tab and the comparison table, first in the map host, then standalone.
    static const char *const groups[] = {"Build", "Behaviour", "Placement", "Look & sound", "Advanced", "Compare all"};
    const int64_t per_mode = (int64_t)(sizeof(groups) / sizeof(groups[0]));
    const int64_t steps = per_mode * 2; // traps and doors
    if (s_smoke_frames == 0)
        content_trapdoor_show((s_smoke_tab / per_mode) % 2 == 1, groups[s_smoke_tab % per_mode]);
    if (++s_smoke_frames < 6)
        return FTRs_Repeat_Current_Action;
    s_smoke_frames = 0;
    if (++s_smoke_tab == steps)
        content_tools_open(ContentTool_TrapDoor); // the standalone host, once more through the same tabs
    if (s_smoke_tab < steps * 2)
        return FTRs_Repeat_Current_Action;
    FTESTLOG("drew the Trap and Door editor: every group tab for traps and doors, in both hosts");
    content_tools_open_for_map(ContentTool_SpellAbility);
    s_smoke_tab = 0;
    s_smoke_frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action008__draw_spells(struct FTestActionArgs *const args)
{
    // Every mode and every tab of it (and the comparison table), in the map host and then standalone.
    struct Step { int mode; const char *group; };
    static const Step steps[] = {
        {0, "Cost and strength"}, {0, "Casting"}, {0, "Look and sound"}, {0, "Advanced"}, {0, "Compare all"},
        {1, "Effect"}, {1, "Duration and aura"}, {1, "Advanced"}, {1, "Compare all"},
        {2, "Damage"}, {2, "Hit rules"}, {2, "Effects and sound"}, {2, "Size and physics"}, {2, "Advanced"}, {2, "Compare all"},
        {3, "Special"}, {3, "Compare all"},
        {4, "Timing"}, {4, "First person"}, {4, "Targeting"}, {4, "Look and sound"}, {4, "Advanced"}, {4, "Compare all"},
    };
    const int64_t count = (int64_t)(sizeof(steps) / sizeof(steps[0]));
    if (s_smoke_frames == 0)
        content_spells_show(steps[s_smoke_tab % count].mode, steps[s_smoke_tab % count].group);
    if (++s_smoke_frames < 6)
        return FTRs_Repeat_Current_Action;
    s_smoke_frames = 0;
    if (++s_smoke_tab == count)
        content_tools_open(ContentTool_SpellAbility); // the standalone host, once more through the same tabs
    if (s_smoke_tab < count * 2)
        return FTRs_Repeat_Current_Action;
    FTESTLOG("drew the Spell and Ability editor: every mode and tab, in both hosts");
    content_tools_open_for_map(ContentTool_Creature);
    s_smoke_tab = 0;
    s_smoke_frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action009__draw_creatures(struct FTestActionArgs *const args)
{
    static const char *const tabs[] = {"Attributes", "Attraction", "Annoyance", "Jobs", "Experience", "Appearance", "Senses",
        "Sprites", "Sounds", "Preview", "Compare all", "Global"};
    const int64_t count = (int64_t)(sizeof(tabs) / sizeof(tabs[0]));
    // Every tab on the first creature, then on another one (heroes have different keys), map host then standalone.
    if (s_smoke_frames == 0)
        content_creature_show(tabs[s_smoke_tab % count], (s_smoke_tab / count) % 2 == 1 ? 6 : 0);
    if (++s_smoke_frames < 6)
        return FTRs_Repeat_Current_Action;
    s_smoke_frames = 0;
    if (++s_smoke_tab == count * 2)
        content_tools_open(ContentTool_Creature); // the standalone host, once more
    if (s_smoke_tab < count * 4)
        return FTRs_Repeat_Current_Action;
    FTESTLOG("drew the Creature editor: every tab for two creatures, in both hosts");
    content_tools_open_for_map(ContentTool_Text);
    s_smoke_tab = 0;
    s_smoke_frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action010__draw_text(struct FTestActionArgs *const args)
{
    // The map host, then standalone; a few languages and string ids selected on the way.
    static const struct { const char *lang; int id; } steps[] = {{"eng", 1}, {"eng", 202}, {"fre", 5}, {"chi", 3}};
    const int64_t count = (int64_t)(sizeof(steps) / sizeof(steps[0]));
    if (s_smoke_frames == 0)
        content_text_show(steps[s_smoke_tab % count].lang, steps[s_smoke_tab % count].id);
    if (++s_smoke_frames < 8)
        return FTRs_Repeat_Current_Action;
    s_smoke_frames = 0;
    if (++s_smoke_tab == count)
        content_tools_open(ContentTool_Text);
    if (s_smoke_tab < count * 2)
        return FTRs_Repeat_Current_Action;
    FTESTLOG("drew the Text editor: several languages and ids, in both hosts");
    content_tools_open_for_map(ContentTool_Room);
    s_smoke_tab = 0;
    s_smoke_frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action011__draw_rooms(struct FTestActionArgs *const args)
{
    struct Step { int mode; const char *group; };
    static const Step steps[] = {
        {0, "Build"}, {0, "Capacity"}, {0, "Roles"}, {0, "Look and sound"}, {0, "Advanced"}, {0, "Compare all"},
        {1, "Digging"}, {1, "Ownership"}, {1, "Block flags"}, {1, "Look and type"}, {1, "Compare all"},
        {2, "Health"},
    };
    const int64_t count = (int64_t)(sizeof(steps) / sizeof(steps[0]));
    if (s_smoke_frames == 0)
        content_rooms_show(steps[s_smoke_tab % count].mode, steps[s_smoke_tab % count].group);
    if (++s_smoke_frames < 6)
        return FTRs_Repeat_Current_Action;
    s_smoke_frames = 0;
    if (++s_smoke_tab == count)
        content_tools_open(ContentTool_Room);
    if (s_smoke_tab < count * 2)
        return FTRs_Repeat_Current_Action;
    FTESTLOG("drew the Room editor: every mode and tab, in both hosts");
    content_tools_open_for_map(ContentTool_Campaign);
    s_smoke_tab = 0;
    s_smoke_frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_config_content_tool_smoke_action012__draw_campaign(struct FTestActionArgs *const args)
{
    if (++s_smoke_frames < 30)
        return FTRs_Repeat_Current_Action;
    s_smoke_frames = 0;
    if (++s_smoke_tab == 1)
    {
        content_tools_open(ContentTool_Campaign);
        return FTRs_Repeat_Current_Action;
    }
    FTESTLOG("drew the Campaign editor, in both hosts");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_config_content_tool_smoke_init()
{
    ftest_append_action(ftest_config_content_tool_smoke_action001__open_editor, 20, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action002__draw_map_host, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action003__draw_standalone, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action004__draw_rules_map, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action005__draw_rules_standalone, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action006__draw_rules_tabs, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action007__draw_trapdoor, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action008__draw_spells, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action009__draw_creatures, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action010__draw_text, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action011__draw_rooms, 0, &s_unused);
    ftest_append_action(ftest_config_content_tool_smoke_action012__draw_campaign, 0, &s_unused);
    return true;
}

} // extern "C"

#endif // FUNCTESTING
