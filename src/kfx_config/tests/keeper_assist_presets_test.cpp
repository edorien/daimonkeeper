// kfx_config: the combat assistant's shipped presets (config/fxdata/combat_assist_presets.toml): the file loads, and every
// preset in it is a rule set the game takes (keeper_assist_preset_set, the same reading as set_assist's).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <string>

#include "keeper_assist_rules.h"
#include "config_magic.h"
#include "kfx_config_state.h"
#include "kfx_config_test_paths.h"

namespace {

struct PowerNames {
    std::unique_ptr<MagicConfig> saved;
    PowerNames() : saved(new MagicConfig(kfx_config_state.conf.magic_conf)) {
        MagicConfig &m = kfx_config_state.conf.magic_conf;
        m.power_types_count = PwrK_MKTUNNELLER + 1;
        const struct { PowerKind k; const char *n; } names[] = {
            {PwrK_HEALCRTR, "POWER_HEAL_CREATURE"}, {PwrK_SPEEDCRTR, "POWER_SPEED"}, {PwrK_CHICKEN, "POWER_CHICKEN"},
            {PwrK_LIGHTNING, "POWER_LIGHTNING"}, {PwrK_CALL2ARMS, "POWER_CALL_TO_ARMS"}, {PwrK_OBEY, "POWER_OBEY"}};
        for (auto &e : names) std::snprintf(m.power_cfgstats[e.k].code_name, COMMAND_WORD_LEN, "%s", e.n);
    }
    ~PowerNames() { kfx_config_state.conf.magic_conf = *saved; }
};

const std::string kPresets = std::string(KFX_CONFIG_TEST_REPO_ROOT) + "/config/fxdata/combat_assist_presets.toml";

} // namespace

TEST_CASE_METHOD(PowerNames, "every shipped preset is a rule set the game takes", "[kfx_config][keeper_assist]") {
    VALUE presets;
    REQUIRE(keeper_assist_load_presets(kPresets.c_str(), &presets));
    for (const char *name : {"guardian", "striker", "miser"}) {
        INFO(name);
        KeeperAssistRule rules[KEEPER_ASSIST_RULES];
        KeeperAssistGlobals g;
        int bad = -2;
        const char *err = keeper_assist_preset_set(&presets, name, rules, &g, &bad);
        CHECK(err == nullptr);
        if (err) WARN(std::string(err) + " at rule " + std::to_string(bad));
        CHECK(g.enabled == 1);
        CHECK(rules[0].used == 1);
    }
    value_fini(&presets);
}

TEST_CASE_METHOD(PowerNames, "the presets say what the plan says they do", "[kfx_config][keeper_assist]") {
    VALUE presets;
    REQUIRE(keeper_assist_load_presets(kPresets.c_str(), &presets));
    KeeperAssistRule r[KEEPER_ASSIST_RULES];
    KeeperAssistGlobals g;
    int bad = -1;
    REQUIRE(keeper_assist_preset_set(&presets, "guardian", r, &g, &bad) == nullptr);
    CHECK((r[0].power == PwrK_HEALCRTR && r[0].level == 2 && r[0].cond[0].fact == KAF_HealthPct && r[0].cond[0].value == 35));
    CHECK((r[1].power == PwrK_SPEEDCRTR && r[2].action == KAA_PowerOff && r[2].power == PwrK_CALL2ARMS && r[3].power == PwrK_OBEY));
    CHECK(g.reserve == 1000);
    REQUIRE(keeper_assist_preset_set(&presets, "miser", r, &g, &bad) == nullptr);
    for (int s = 0; s < KEEPER_ASSIST_RULES; s++) if (r[s].used) CHECK(r[s].action == KAA_PowerOff); // no casts at all
    CHECK(std::string(keeper_assist_preset_set(&presets, "berserker", r, &g, &bad)) == "UNKNOWN_PRESET");
    value_fini(&presets);
}

TEST_CASE("a missing presets file loads as none", "[kfx_config][keeper_assist]") {
    VALUE presets;
    CHECK_FALSE(keeper_assist_load_presets("/nonexistent/combat_assist_presets.toml", &presets));
    CHECK(value_dict_size(&presets) == 0);
    value_fini(&presets);
}
