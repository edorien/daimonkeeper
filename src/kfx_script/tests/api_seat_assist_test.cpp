// kfx_script: api_seat_assist.c -- set_assist's JSON (docs/newFeatures/llm/02-rule-language.md section 1) read into the
// rule set the game stores: names to numbers, defaults, what is kept when left out, and each error code with the rule at
// fault.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <json.h>
#include <json-dom.h>

#include "api_seat_assist.h"
#include "config_creature.h"
#include "config_magic.h"
#include "kfx_config_state.h"

namespace {

struct Names {
    std::unique_ptr<MagicConfig> magic;
    int64_t model_count;
    char dragon[COMMAND_WORD_LEN];
    Names() : magic(new MagicConfig(kfx_config_state.conf.magic_conf)), model_count(kfx_config_state.conf.crtr_conf.model_count) {
        MagicConfig &m = kfx_config_state.conf.magic_conf;
        m.power_types_count = PwrK_MKTUNNELLER + 1;
        const struct { PowerKind k; const char *n; } names[] = {
            {PwrK_HEALCRTR, "POWER_HEAL_CREATURE"}, {PwrK_SPEEDCRTR, "POWER_SPEED"}, {PwrK_CHICKEN, "POWER_CHICKEN"},
            {PwrK_CAVEIN, "POWER_CAVE_IN"}, {PwrK_CALL2ARMS, "POWER_CALL_TO_ARMS"}, {PwrK_OBEY, "POWER_OBEY"}, {PwrK_HAND, "POWER_HAND"}};
        for (auto &e : names) std::snprintf(m.power_cfgstats[e.k].code_name, COMMAND_WORD_LEN, "%s", e.n);
        std::memcpy(dragon, kfx_config_state.conf.crtr_conf.model[7].name, COMMAND_WORD_LEN);
        std::snprintf(kfx_config_state.conf.crtr_conf.model[7].name, COMMAND_WORD_LEN, "DRAGON");
        if (kfx_config_state.conf.crtr_conf.model_count < 8) kfx_config_state.conf.crtr_conf.model_count = 8;
    }
    ~Names() {
        kfx_config_state.conf.magic_conf = *magic;
        std::memcpy(kfx_config_state.conf.crtr_conf.model[7].name, dragon, COMMAND_WORD_LEN);
        kfx_config_state.conf.crtr_conf.model_count = model_count;
    }
};

struct Parsed {
    KeeperAssistRule rules[KEEPER_ASSIST_RULES];
    KeeperAssistGlobals g;
    std::string err;
    int bad = -2;
};

Parsed parse(const char *json, const KeeperAssistRule *cur_rules = nullptr, const KeeperAssistGlobals *cur = nullptr) {
    VALUE v;
    JSON_INPUT_POS pos;
    REQUIRE(json_dom_parse(json, std::strlen(json), nullptr, 0, &v, &pos) == 0);
    static KeeperAssistRule none[KEEPER_ASSIST_RULES];
    KeeperAssistGlobals zero{};
    Parsed p;
    const char *e = keeper_assist_parse_set(&v, cur_rules ? cur_rules : none, cur ? cur : &zero, p.rules, &p.g, &p.bad);
    p.err = e ? e : "";
    value_fini(&v);
    return p;
}

} // namespace

TEST_CASE_METHOD(Names, "the example from the rule language reads into three rules", "[kfx_script][assist]") {
    Parsed p = parse(R"({"enabled": true, "interval": 2, "reserve": 1500, "min_gap": 4, "max_spend": 0, "rules": [
        {"name": "save-the-dying", "power": "POWER_HEAL_CREATURE", "level": 3, "target": "ally_in_fight", "pick": "lowest_health",
         "when": [["health_pct", "<", 35], ["gold_after_wages", ">", 2000], ["level", ">=", 3]],
         "cooldown": 20, "target_cooldown": 160, "budget": 6000},
        {"power": "POWER_CHICKEN", "level": 0, "target": "enemy_in_fight", "pick": "strongest",
         "when": [["force_ratio", ">", 130], ["is_digger", "==", 0], ["gold", ">", 5000]], "cooldown": 60, "budget": 4000},
        {"power_off": "POWER_CALL_TO_ARMS", "when": [["gold", "<", 1500], ["cta_paying", "==", 1]]}]})");
    REQUIRE(p.err == "");
    CHECK((p.g.enabled == 1 && p.g.interval == 2 && p.g.reserve == 1500 && p.g.min_gap == 4 && p.g.charge_time == 1));
    const KeeperAssistRule &h = p.rules[0];
    CHECK((h.used && h.action == KAA_Cast && h.power == PwrK_HEALCRTR && h.level == 3 && h.target == KAT_AllyInFight));
    CHECK((h.pick == KAP_LowestHealth && h.cooldown == 20 && h.target_cooldown == 160 && h.budget == 6000 && h.cond_count == 3));
    CHECK((h.cond[0].fact == KAF_HealthPct && h.cond[0].op == KAO_Lt && h.cond[0].value == 35));
    CHECK((h.cond[2].fact == KAF_Level && h.cond[2].op == KAO_Ge && h.cond[2].value == 3));
    CHECK((p.rules[1].power == PwrK_CHICKEN && p.rules[1].target == KAT_EnemyInFight && p.rules[1].pick == KAP_Strongest));
    CHECK((p.rules[2].action == KAA_PowerOff && p.rules[2].power == PwrK_CALL2ARMS && p.rules[2].target == KAT_None));
    CHECK(p.rules[3].used == 0);
}

TEST_CASE_METHOD(Names, "defaults: the target from the power, cooldowns from the target", "[kfx_script][assist]") {
    Parsed p = parse(R"({"rules": [{"power": "POWER_SPEED"}, {"power": "POWER_CAVE_IN"}]})");
    REQUIRE(p.err == "");
    CHECK((p.rules[0].target == KAT_AllyInFight && p.rules[0].target_cooldown == KEEPER_ASSIST_DEFAULT_ALLY_TARGET_COOLDOWN));
    CHECK((p.rules[1].target == KAT_EnemyInFight && p.rules[1].target_cooldown == 0 && p.rules[1].cooldown == KEEPER_ASSIST_DEFAULT_COOLDOWN));
    CHECK((p.g.interval == KEEPER_ASSIST_DEFAULT_INTERVAL && p.g.min_gap == KEEPER_ASSIST_DEFAULT_MIN_GAP && p.g.enabled == 0));
}

TEST_CASE_METHOD(Names, "left out: the current globals and rules stay; an empty list clears", "[kfx_script][assist]") {
    KeeperAssistRule cur[KEEPER_ASSIST_RULES]{};
    keeper_assist_default_rule(&cur[4], KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight);
    KeeperAssistGlobals g{};
    keeper_assist_default_globals(&g);
    g.enabled = 1; g.reserve = 777;
    Parsed p = parse(R"({"max_spend": 5000})", cur, &g);
    REQUIRE(p.err == "");
    CHECK((p.g.reserve == 777 && p.g.enabled == 1 && p.g.max_spend == 5000));
    CHECK(std::memcmp(p.rules, cur, sizeof(cur)) == 0);
    Parsed q = parse(R"({"rules": []})", cur, &g);
    CHECK(q.rules[4].used == 0);
}

TEST_CASE_METHOD(Names, "kind takes a creature's name", "[kfx_script][assist]") {
    Parsed p = parse(R"({"rules": [{"power": "POWER_CHICKEN", "when": [["kind", "==", "DRAGON"]]}]})");
    REQUIRE(p.err == "");
    CHECK(p.rules[0].cond[0].value == 7);
    CHECK(parse(R"({"rules": [{"power": "POWER_CHICKEN", "when": [["kind", "==", "NOBODY"]]}]})").err == "UNKNOWN_KIND");
}

TEST_CASE_METHOD(Names, "each error code, with the rule at fault", "[kfx_script][assist]") {
    struct { const char *json; const char *err; int bad; } cases[] = {
        {R"({"rules": [{"power": "POWER_SPEED"}, {"level": 2}]})", "MISSING_POWER", 1},
        {R"({"rules": [{"power": "POWER_SPEED", "power_off": "POWER_OBEY"}]})", "MISSING_POWER", 0},
        {R"({"rules": [{"power": "POWER_NOPE"}]})", "UNKNOWN_POWER", 0},
        {R"({"rules": [{"power": "POWER_HAND"}]})", "POWER_NOT_SUPPORTED", 0},
        {R"({"rules": [{"power": "POWER_HEAL_CREATURE", "target": "enemy_in_fight"}]})", "BAD_TARGET_FOR_POWER", 0},
        {R"({"rules": [{"power_off": "POWER_OBEY", "target": "ally_any"}]})", "BAD_TARGET_FOR_POWER", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "target": "everyone"}]})", "UNKNOWN_TARGET", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "pick": "nicest"}]})", "UNKNOWN_PICK", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "level": 9}]})", "BAD_LEVEL", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "cooldown": 10001}]})", "VALUE_OUT_OF_RANGE", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "budget": -1}]})", "VALUE_OUT_OF_RANGE", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "when": [["mood", "<", 3]]}]})", "UNKNOWN_FACT", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "when": [["level", "=", 3]]}]})", "BAD_OP", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "when": [["level", "<"]]}]})", "BAD_CONDITION", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "when": [["level", "<", "x"]]}]})", "BAD_VALUE", 0},
        {R"({"rules": [{"power": "POWER_SPEED", "when": [["gold", "<", 1], ["gold", "<", 1], ["gold", "<", 1], ["gold", "<", 1], ["gold", "<", 1]]}]})", "TOO_MANY_CONDITIONS", 0},
        {R"({"rules": [{"power_off": "POWER_OBEY", "when": [["health_pct", "<", 30]]}]})", "FACT_NEEDS_TARGET", 0},
        {R"({"rules": [{"power": "POWER_CHICKEN", "when": [["kind", "<", 3]]}]})", "BAD_OP", 0},
        {R"({"rules": "all of them"})", "BAD_RULES", -1},
        {R"({"interval": 0})", "VALUE_OUT_OF_RANGE", -1},
        {R"({"min_gap": "soon"})", "BAD_VALUE", -1},
    };
    for (auto &c : cases) {
        INFO(c.json);
        Parsed p = parse(c.json);
        CHECK(p.err == c.err);
        CHECK(p.bad == c.bad);
    }
    std::string many = R"({"rules": [)";
    for (int i = 0; i <= KEEPER_ASSIST_RULES; i++) many += std::string(i ? "," : "") + R"({"power": "POWER_SPEED"})";
    many += "]}";
    CHECK(parse(many.c_str()).err == "TOO_MANY_RULES");
}
