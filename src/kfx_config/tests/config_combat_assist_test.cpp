// kfx_config: config_combat_assist.c -- a player's combat assistant profile (save/combat_assistant.toml,
// docs/newFeatures/llm/01 section 10.3): written and read back unchanged, a missing file means off, a broken rule is
// dropped with the rest kept, and a profile expands into the rules its preset and sliders promise.
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>

#include "config_combat_assist.h"
#include "config_magic.h"
#include "kfx_config_state.h"
#include "kfx_config_test_paths.h"

namespace {

struct Fixture {
    std::unique_ptr<MagicConfig> saved;
    std::string dir;
    VALUE presets;
    Fixture() : saved(new MagicConfig(kfx_config_state.conf.magic_conf)) {
        MagicConfig &m = kfx_config_state.conf.magic_conf;
        m.power_types_count = PwrK_MKTUNNELLER + 1;
        const struct { PowerKind k; const char *n; } names[] = {
            {PwrK_HEALCRTR, "POWER_HEAL_CREATURE"}, {PwrK_SPEEDCRTR, "POWER_SPEED"}, {PwrK_PROTECT, "POWER_PROTECT"},
            {PwrK_CONCEAL, "POWER_CONCEAL"}, {PwrK_CHICKEN, "POWER_CHICKEN"}, {PwrK_DISEASE, "POWER_DISEASE"},
            {PwrK_LIGHTNING, "POWER_LIGHTNING"}, {PwrK_CAVEIN, "POWER_CAVE_IN"}, {PwrK_CALL2ARMS, "POWER_CALL_TO_ARMS"},
            {PwrK_OBEY, "POWER_OBEY"}};
        for (auto &e : names) std::snprintf(m.power_cfgstats[e.k].code_name, COMMAND_WORD_LEN, "%s", e.n);
        dir = (std::filesystem::temp_directory_path() / ("kfx-assist-profile-" + std::to_string(std::random_device{}()))).string();
        std::filesystem::create_directories(dir);
        REQUIRE(keeper_assist_load_presets((std::string(KFX_CONFIG_TEST_REPO_ROOT) + "/config/fxdata/combat_assist_presets.toml").c_str(), &presets));
    }
    ~Fixture() {
        value_fini(&presets);
        std::filesystem::remove_all(dir);
        kfx_config_state.conf.magic_conf = *saved;
    }
    std::string path(const char *name) const { return dir + "/" + name; }
    void write(const char *name, const char *text) const { std::ofstream(path(name)) << text; }
};

const KeeperAssistRule *find(const KeeperAssistRule *rules, PowerKind power) {
    for (int i = 0; i < KEEPER_ASSIST_RULES; i++) if (rules[i].used && rules[i].power == power) return &rules[i];
    return nullptr;
}
int32_t cond(const KeeperAssistRule *r, KeeperAssistFact f) {
    for (int c = 0; c < r->cond_count; c++) if (r->cond[c].fact == f) return r->cond[c].value;
    return -12345;
}

} // namespace

TEST_CASE_METHOD(Fixture, "a missing profile is the defaults: off, guardian, every power", "[kfx_config][assist_profile]") {
    KeeperAssistProfile p;
    CHECK_FALSE(keeper_assist_profile_load_from(path("none.toml").c_str(), &p));
    CHECK((p.globals.enabled == 0 && std::string(p.preset) == "guardian" && p.custom_count == 0));
    CHECK(p.tuning.powers == keeper_assist_all_powers_mask());
}

TEST_CASE_METHOD(Fixture, "a profile written is read back the same, hand-written rules included", "[kfx_config][assist_profile]") {
    KeeperAssistProfile p;
    keeper_assist_profile_defaults(&p);
    p.globals.enabled = 1; p.globals.reserve = 1234; p.globals.min_gap = 9; p.globals.charge_time = 0;
    std::snprintf(p.preset, sizeof(p.preset), "striker");
    p.tuning = {28, 4, 125, 1500, 700, (1u << PwrK_HEALCRTR) | (1u << PwrK_OBEY)};
    keeper_assist_default_rule(&p.custom[0], KAA_Cast, PwrK_CHICKEN, KAT_EnemyInFight);
    p.custom[0].pick = KAP_MostValuable; p.custom[0].budget = 3000;
    p.custom[0].cond[0] = {KAF_ForceRatio, KAO_Gt, 140}; p.custom[0].cond_count = 1;
    keeper_assist_default_rule(&p.custom[1], KAA_PowerOff, PwrK_OBEY, KAT_None);
    p.custom[1].cond[0] = {KAF_Gold, KAO_Lt, 600}; p.custom[1].cond_count = 1;
    p.custom_count = 2;
    REQUIRE(keeper_assist_profile_save_to(path("p.toml").c_str(), &p));
    KeeperAssistProfile q;
    REQUIRE(keeper_assist_profile_load_from(path("p.toml").c_str(), &q));
    CHECK(std::memcmp(&q.globals, &p.globals, sizeof(p.globals)) == 0);
    CHECK(std::string(q.preset) == "striker");
    CHECK(std::memcmp(&q.tuning, &p.tuning, sizeof(p.tuning)) == 0);
    CHECK(q.custom_count == 2);
    CHECK(std::memcmp(q.custom, p.custom, sizeof(KeeperAssistRule) * 2) == 0);
}

TEST_CASE_METHOD(Fixture, "a broken hand-written rule is dropped, the rest kept; unknown keys are ignored", "[kfx_config][assist_profile]") {
    write("b.toml", "enabled = true\nreserve = 50\nflavour = \"spicy\"\n"
                    "[[rules]]\npower = \"POWER_SPEED\"\n"
                    "[[rules]]\npower = \"POWER_NOPE\"\n"
                    "[[rules]]\npower_off = \"POWER_OBEY\"\nwhen = [[\"gold\", \"<\", 900]]\n");
    KeeperAssistProfile p;
    REQUIRE(keeper_assist_profile_load_from(path("b.toml").c_str(), &p));
    CHECK((p.globals.enabled == 1 && p.globals.reserve == 50));
    REQUIRE(p.custom_count == 2);
    CHECK((p.custom[0].power == PwrK_SPEEDCRTR && p.custom[1].power == PwrK_OBEY && p.custom[1].cond[0].value == 900));
}

TEST_CASE_METHOD(Fixture, "a preset and the sliders expand into the rules they promise", "[kfx_config][assist_profile]") {
    KeeperAssistProfile p;
    keeper_assist_profile_defaults(&p);
    p.globals.enabled = 1;
    p.globals.reserve = 2500;
    p.tuning = {22, 5, 150, 3000, 2000, keeper_assist_all_powers_mask()};
    KeeperAssistRule rules[KEEPER_ASSIST_RULES];
    KeeperAssistGlobals g;
    REQUIRE(keeper_assist_profile_expand(&p, &presets, rules, &g) == nullptr);
    CHECK((g.enabled == 1 && g.reserve == 2500));
    const KeeperAssistRule *heal = find(rules, PwrK_HEALCRTR);
    REQUIRE(heal);
    CHECK((heal->level == 5 && cond(heal, KAF_HealthPct) == 22));
    CHECK(cond(heal, KAF_GoldAfterWages) == 1500);           // what the sliders do not touch stays the preset's
    CHECK(cond(find(rules, PwrK_SPEEDCRTR), KAF_ForceRatio) == 150);
    CHECK(cond(find(rules, PwrK_CALL2ARMS), KAF_Gold) == 3000);
    CHECK(cond(find(rules, PwrK_OBEY), KAF_Gold) == 2000);
    for (int i = 0; i < KEEPER_ASSIST_RULES; i++) if (rules[i].used) CHECK(keeper_assist_rule_validate(&rules[i]) == nullptr);
    // Unticking Speed and Must Obey leaves their rules out, the others in order.
    p.tuning.powers &= ~((1u << PwrK_SPEEDCRTR) | (1u << PwrK_OBEY));
    REQUIRE(keeper_assist_profile_expand(&p, &presets, rules, &g) == nullptr);
    CHECK((rules[0].power == PwrK_HEALCRTR && rules[1].power == PwrK_CALL2ARMS && rules[2].used == 0));
}

TEST_CASE_METHOD(Fixture, "hand-written rules replace the preset; an unknown preset gives nothing", "[kfx_config][assist_profile]") {
    KeeperAssistProfile p;
    keeper_assist_profile_defaults(&p);
    keeper_assist_default_rule(&p.custom[0], KAA_Cast, PwrK_CONCEAL, KAT_AllyAny);
    p.custom_count = 1;
    KeeperAssistRule rules[KEEPER_ASSIST_RULES];
    KeeperAssistGlobals g;
    REQUIRE(keeper_assist_profile_expand(&p, &presets, rules, &g) == nullptr);
    CHECK((rules[0].power == PwrK_CONCEAL && rules[1].used == 0));
    p.custom_count = 0;
    std::snprintf(p.preset, sizeof(p.preset), "berserker");
    CHECK(std::string(keeper_assist_profile_expand(&p, &presets, rules, &g)) == "UNKNOWN_PRESET");
    CHECK(rules[0].used == 0);
}

TEST_CASE_METHOD(Fixture, "selecting a preset sets the sliders to its own values", "[kfx_config][assist_profile]") {
    KeeperAssistTuning t{};
    int32_t reserve = -1;
    REQUIRE(keeper_assist_tuning_from_preset(&presets, "guardian", &t, &reserve));
    CHECK((t.heal_below_pct == 35 && t.heal_level == 2 && t.buff_force_ratio == 110 && t.cta_floor == 1000 && t.obey_floor == 800));
    CHECK(reserve == 1000);
    CHECK_FALSE(keeper_assist_tuning_from_preset(&presets, "berserker", &t, &reserve));
}

TEST_CASE_METHOD(Fixture, "a human player's profile cannot hold a hand rule: those are an agent's only", "[kfx_config][assist_profile]") {
    write("h.toml", "enabled = true\n"
                    "[[rules]]\nhand = \"reinforce\"\n"
                    "[[rules]]\npower = \"POWER_SPEED\"\n"
                    "[[rules]]\nhand = \"evacuate_diggers\"\nto = [30, 30]\n");
    KeeperAssistProfile p;
    REQUIRE(keeper_assist_profile_load_from(path("h.toml").c_str(), &p));
    REQUIRE(p.custom_count == 1);
    CHECK(p.custom[0].power == PwrK_SPEEDCRTR);
}
