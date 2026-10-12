// kfx_config: keeper_assist_rules.c -- the combat assistant's rule vocabulary and the one validation every way in goes
// through (docs/newFeatures/llm/02-rule-language.md sections 2-6).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

#include "keeper_assist_rules.h"
#include "config_magic.h"
#include "kfx_config_state.h"

namespace {

struct PowerTypes {
    int64_t saved;
    PowerTypes() : saved(kfx_config_state.conf.magic_conf.power_types_count) { kfx_config_state.conf.magic_conf.power_types_count = PwrK_MKTUNNELLER + 1; }
    ~PowerTypes() { kfx_config_state.conf.magic_conf.power_types_count = saved; }
};

KeeperAssistRule cast(PowerKind power, KeeperAssistTarget target)
{
    KeeperAssistRule r;
    keeper_assist_default_rule(&r, KAA_Cast, power, target);
    return r;
}

void add_cond(KeeperAssistRule &r, KeeperAssistFact f, KeeperAssistOp op, int32_t v)
{
    r.cond[r.cond_count].fact = (uint8_t)f;
    r.cond[r.cond_count].op = (uint8_t)op;
    r.cond[r.cond_count].value = v;
    r.cond_count++;
}

std::string err(const KeeperAssistRule &r)
{
    const char *e = keeper_assist_rule_validate(&r);
    return e ? e : "";
}

} // namespace

TEST_CASE_METHOD(PowerTypes, "every supported power is accepted against its own kind of target", "[kfx_config][keeper_assist]") {
    for (PowerKind p : {PwrK_HEALCRTR, PwrK_SPEEDCRTR, PwrK_PROTECT, PwrK_CONCEAL}) {
        CHECK(err(cast(p, KAT_AllyInFight)) == "");
        CHECK(err(cast(p, KAT_AllyAny)) == "");
        CHECK(err(cast(p, KAT_EnemyInFight)) == "BAD_TARGET_FOR_POWER");
    }
    for (PowerKind p : {PwrK_CHICKEN, PwrK_DISEASE, PwrK_LIGHTNING, PwrK_CAVEIN}) {
        CHECK(err(cast(p, KAT_EnemyInFight)) == "");
        CHECK(err(cast(p, KAT_AllyInFight)) == "BAD_TARGET_FOR_POWER");
    }
}

TEST_CASE_METHOD(PowerTypes, "power_off is for Call to Arms and Must Obey, with no target", "[kfx_config][keeper_assist]") {
    KeeperAssistRule r;
    keeper_assist_default_rule(&r, KAA_PowerOff, PwrK_CALL2ARMS, KAT_None);
    CHECK(err(r) == "");
    r.power = PwrK_OBEY;
    CHECK(err(r) == "");
    r.target = KAT_AllyInFight;
    CHECK(err(r) == "BAD_TARGET_FOR_POWER");
    keeper_assist_default_rule(&r, KAA_PowerOff, PwrK_HEALCRTR, KAT_None);
    CHECK(err(r) == "POWER_NOT_SUPPORTED");
    // and they cannot be cast by a rule
    CHECK(err(cast(PwrK_CALL2ARMS, KAT_AllyInFight)) == "POWER_NOT_SUPPORTED");
}

TEST_CASE_METHOD(PowerTypes, "unknown and unsupported powers", "[kfx_config][keeper_assist]") {
    CHECK(err(cast(0, KAT_AllyInFight)) == "UNKNOWN_POWER");
    CHECK(err(cast(PwrK_MKTUNNELLER + 1, KAT_AllyInFight)) == "UNKNOWN_POWER");
    for (PowerKind p : {PwrK_HAND, PwrK_MKDIGGER, PwrK_POSSESS, PwrK_ARMAGEDDON, PwrK_SIGHT}) {
        CHECK(err(cast(p, KAT_AllyInFight)) == "POWER_NOT_SUPPORTED");
    }
}

TEST_CASE_METHOD(PowerTypes, "conditions: vocabulary, target facts need a target, kind compares only for equality", "[kfx_config][keeper_assist]") {
    KeeperAssistRule r = cast(PwrK_HEALCRTR, KAT_AllyInFight);
    add_cond(r, KAF_HealthPct, KAO_Lt, 35);
    add_cond(r, KAF_GoldAfterWages, KAO_Gt, 2000);
    CHECK(err(r) == "");
    r.cond[1].fact = KAF_Count;
    CHECK(err(r) == "UNKNOWN_FACT");
    r.cond[1].fact = KAF_Gold;
    r.cond[1].op = KAO_Count;
    CHECK(err(r) == "BAD_OP");
    r.cond[1].op = KAO_Lt;
    r.cond[1].fact = KAF_Kind;
    CHECK(err(r) == "BAD_OP");
    r.cond[1].op = KAO_Eq;
    CHECK(err(r) == "");

    KeeperAssistRule off;
    keeper_assist_default_rule(&off, KAA_PowerOff, PwrK_CALL2ARMS, KAT_None);
    add_cond(off, KAF_Gold, KAO_Lt, 1500);
    CHECK(err(off) == "");
    add_cond(off, KAF_HealthPct, KAO_Lt, 10);
    CHECK(err(off) == "FACT_NEEDS_TARGET");
}

TEST_CASE_METHOD(PowerTypes, "ranges: level, condition count, budget", "[kfx_config][keeper_assist]") {
    KeeperAssistRule r = cast(PwrK_HEALCRTR, KAT_AllyInFight);
    r.level = KEEPER_ASSIST_MAX_LEVEL;
    CHECK(err(r) == "");
    r.level = KEEPER_ASSIST_MAX_LEVEL + 1;
    CHECK(err(r) == "BAD_LEVEL");
    r.level = 0;
    r.cond_count = KEEPER_ASSIST_CONDS + 1;
    CHECK(err(r) == "TOO_MANY_CONDITIONS");
    r.cond_count = 0;
    r.budget = -1;
    CHECK(err(r) == "VALUE_OUT_OF_RANGE");
}

TEST_CASE("defaults: rule cooldowns by target, globals off with charge time on", "[kfx_config][keeper_assist]") {
    KeeperAssistRule ally, enemy;
    keeper_assist_default_rule(&ally, KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight);
    keeper_assist_default_rule(&enemy, KAA_Cast, PwrK_CHICKEN, KAT_EnemyInFight);
    CHECK((ally.used == 1 && ally.cooldown == KEEPER_ASSIST_DEFAULT_COOLDOWN));
    CHECK(ally.target_cooldown == KEEPER_ASSIST_DEFAULT_ALLY_TARGET_COOLDOWN);
    CHECK(enemy.target_cooldown == 0);
    KeeperAssistGlobals g;
    keeper_assist_default_globals(&g);
    CHECK((g.enabled == 0 && g.charge_time == 1 && g.interval == KEEPER_ASSIST_DEFAULT_INTERVAL && g.min_gap == KEEPER_ASSIST_DEFAULT_MIN_GAP));
    CHECK(keeper_assist_globals_validate(&g) == nullptr);
    g.interval = 0;
    CHECK(std::string(keeper_assist_globals_validate(&g)) == "VALUE_OUT_OF_RANGE");
    g.interval = 2;
    g.min_gap = 201;
    CHECK(std::string(keeper_assist_globals_validate(&g)) == "VALUE_OUT_OF_RANGE");
    g.min_gap = 4;
    g.reserve = -5;
    CHECK(std::string(keeper_assist_globals_validate(&g)) == "VALUE_OUT_OF_RANGE");
}

TEST_CASE("names round-trip, case-insensitively; unknown names are 0", "[kfx_config][keeper_assist]") {
    for (int f = 1; f < KAF_Count; f++) {
        CHECK(keeper_assist_fact_by_name(keeper_assist_fact_name((KeeperAssistFact)f)) == f);
    }
    CHECK(keeper_assist_fact_by_name("HEALTH_PCT") == KAF_HealthPct);
    CHECK(keeper_assist_fact_by_name("nonsense") == KAF_None);
    CHECK(keeper_assist_target_by_name("enemy_in_fight") == KAT_EnemyInFight);
    CHECK(keeper_assist_pick_by_name("most_valuable") == KAP_MostValuable);
    CHECK(keeper_assist_op_by_name("<=") == KAO_Le);
    CHECK(keeper_assist_op_by_name("=<") == KAO_None);
    CHECK(keeper_assist_fact_needs_target(KAF_AlliesNear));
    CHECK_FALSE(keeper_assist_fact_needs_target(KAF_Gold));
}

TEST_CASE("compare: each op", "[kfx_config][keeper_assist]") {
    CHECK(keeper_assist_compare(KAO_Lt, 1, 2));
    CHECK_FALSE(keeper_assist_compare(KAO_Lt, 2, 2));
    CHECK(keeper_assist_compare(KAO_Le, 2, 2));
    CHECK(keeper_assist_compare(KAO_Gt, 3, 2));
    CHECK(keeper_assist_compare(KAO_Ge, 2, 2));
    CHECK(keeper_assist_compare(KAO_Eq, -4, -4));
    CHECK(keeper_assist_compare(KAO_Ne, 1, 2));
    CHECK_FALSE(keeper_assist_compare(KAO_None, 1, 1));
}
