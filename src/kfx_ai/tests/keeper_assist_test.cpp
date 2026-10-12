// kfx_ai: keeper_assist.c -- the combat assistant's decision (keeper_assist_decide), driven through a stub world so each
// rule of the language (docs/newFeatures/llm/02-rule-language.md) can be checked on its own: conditions, priority, pick
// orders, cooldowns, pacing, and the spending limits. The world-reading side is tested by ftest keeper_assist.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <map>
#include <set>

#include "keeper_assist.h"
#include "keeper_assist_state.h"
#include "config_magic.h"

namespace {

struct StubWorld {
    std::set<ThingIndex> not_castable;
    std::map<ThingIndex, int64_t> allies_near;
    std::set<PowerKind> unavailable;
    int64_t allies_near_calls = 0;

    static StubWorld &of(void *ctx) { return *static_cast<StubWorld *>(ctx); }
    static TbBool can_target(void *ctx, const KeeperAssistRule *, const KeeperAssistCandidate *c) { return of(ctx).not_castable.count(c->thing) == 0; }
    static int64_t near(void *ctx, const KeeperAssistRule *, const KeeperAssistCandidate *c) {
        of(ctx).allies_near_calls++;
        auto it = of(ctx).allies_near.find(c->thing);
        return (it == of(ctx).allies_near.end()) ? 0 : it->second;
    }
    static TbBool available(void *ctx, PowerKind p) { return of(ctx).unavailable.count(p) == 0; }
    static int64_t price(void *, PowerKind p, int64_t level) { return 100 * (p == PwrK_HEALCRTR ? 3 : 1) + 100 * level; }
    KeeperAssistWorld world() { return KeeperAssistWorld{this, can_target, near, available, price}; }
};

struct Fixture {
    KeeperAssistState st;
    StubWorld stub;
    KeeperAssistWorld world;
    int64_t facts[KAF_Count];
    KeeperAssistCandidate cands[8];
    int n = 0;

    Fixture() {
        std::memset(&st, 0, sizeof(st));
        keeper_assist_default_globals(&st.globals);
        st.globals.enabled = 1;
        std::memset(facts, 0, sizeof(facts));
        facts[KAF_Gold] = 10000;
        world = stub.world();
    }
    KeeperAssistRule &rule(int slot, PowerKind power, KeeperAssistTarget target, KeeperAssistAction a = KAA_Cast) {
        keeper_assist_default_rule(&st.live[slot], a, power, target);
        return st.live[slot];
    }
    static void cond(KeeperAssistRule &r, KeeperAssistFact f, KeeperAssistOp op, int32_t v) {
        r.cond[r.cond_count].fact = (uint8_t)f; r.cond[r.cond_count].op = (uint8_t)op; r.cond[r.cond_count].value = v; r.cond_count++;
    }
    KeeperAssistCandidate &ally(ThingIndex t, int64_t health_pct, int64_t force = 100, int64_t level = 1) {
        KeeperAssistCandidate &c = cands[n++];
        std::memset(&c, 0, sizeof(c));
        c.thing = t;
        c.sets = KEEPER_ASSIST_SET(KAT_AllyInFight) | KEEPER_ASSIST_SET(KAT_AllyAny);
        c.facts[KAF_HealthPct] = health_pct; c.facts[KAF_Force] = force; c.facts[KAF_Level] = level;
        c.score = force;
        return c;
    }
    KeeperAssistCandidate &enemy(ThingIndex t, int64_t force, int64_t health_pct = 100) {
        KeeperAssistCandidate &c = ally(t, health_pct, force);
        c.sets = KEEPER_ASSIST_SET(KAT_EnemyInFight);
        return c;
    }
    KeeperAssistDecision decide(GameTurn now = 1000, bool casts = true) { return keeper_assist_decide(&st, &world, facts, cands, n, now, casts); }
};

} // namespace

TEST_CASE_METHOD(Fixture, "a rule fires when all its conditions hold, and not when any one fails", "[kfx_ai][keeper_assist]") {
    KeeperAssistRule &r = rule(0, PwrK_HEALCRTR, KAT_AllyInFight);
    cond(r, KAF_HealthPct, KAO_Lt, 35);
    cond(r, KAF_Gold, KAO_Gt, 2000);
    ally(11, 20);
    KeeperAssistDecision d = decide();
    CHECK((d.slot == 0 && d.cand == 0 && d.price == 300));
    facts[KAF_Gold] = 1500;                // the second condition fails
    CHECK(decide().slot == -1);
    facts[KAF_Gold] = 10000;
    cands[0].facts[KAF_HealthPct] = 40;    // the first fails
    CHECK(decide().slot == -1);
}

TEST_CASE_METHOD(Fixture, "slot order is priority: the first rule that can act wins", "[kfx_ai][keeper_assist]") {
    KeeperAssistRule &speed = rule(0, PwrK_SPEEDCRTR, KAT_AllyInFight);
    cond(speed, KAF_HealthPct, KAO_Lt, 10);   // does not match
    rule(1, PwrK_HEALCRTR, KAT_AllyInFight);
    rule(2, PwrK_PROTECT, KAT_AllyInFight);
    ally(5, 30);
    CHECK(decide().slot == 1);
}

TEST_CASE_METHOD(Fixture, "pick orders, and a tie goes to the lower thing index", "[kfx_ai][keeper_assist]") {
    KeeperAssistRule &r = rule(0, PwrK_HEALCRTR, KAT_AllyInFight);
    ally(30, 50, 200, 2);
    ally(20, 10, 100, 5);
    ally(10, 10, 300, 1);
    CHECK(decide().cand == 2);            // default for allies: lowest health; 10 and 20 tie at 10%, 10 wins
    r.pick = KAP_Strongest;
    CHECK(decide().cand == 2);            // force 300
    r.pick = KAP_HighestLevel;
    CHECK(decide().cand == 1);            // level 5
    r.pick = KAP_MostValuable;
    cands[0].score = 999;
    CHECK(decide().cand == 0);
}

TEST_CASE_METHOD(Fixture, "enemies: the default pick is the strongest; allies are not in the enemy set", "[kfx_ai][keeper_assist]") {
    rule(0, PwrK_CHICKEN, KAT_EnemyInFight);
    ally(1, 10, 900);
    enemy(2, 100);
    enemy(3, 400);
    CHECK(decide().cand == 2);
}

TEST_CASE_METHOD(Fixture, "a candidate the engine says cannot be targeted is passed over for the next best", "[kfx_ai][keeper_assist]") {
    rule(0, PwrK_HEALCRTR, KAT_AllyInFight);
    ally(1, 10);
    ally(2, 20);
    stub.not_castable.insert(1);           // e.g. already under the spell, or not castable there
    CHECK(decide().cand == 1);
    stub.not_castable.insert(2);
    CHECK(decide().slot == -1);
}

TEST_CASE_METHOD(Fixture, "area powers: a target with more of our creatures in reach than allowed is skipped", "[kfx_ai][keeper_assist]") {
    KeeperAssistRule &r = rule(0, PwrK_CAVEIN, KAT_EnemyInFight);
    enemy(1, 500);
    enemy(2, 100);
    stub.allies_near[1] = 2;
    CHECK(decide().cand == 1);             // the stronger one would hit two of ours
    r.max_allies_hit = 2;
    CHECK(decide().cand == 0);
    // A non-area power never asks, unless a condition uses allies_near.
    KeeperAssistRule &h = rule(0, PwrK_CHICKEN, KAT_EnemyInFight);
    stub.allies_near_calls = 0;
    decide();
    CHECK(stub.allies_near_calls == 0);
    cond(h, KAF_AlliesNear, KAO_Eq, 0);
    CHECK(decide().cand == 1);
    CHECK(stub.allies_near_calls > 0);
}

TEST_CASE_METHOD(Fixture, "the rule's cooldown and the target's cooldown", "[kfx_ai][keeper_assist]") {
    KeeperAssistRule &r = rule(0, PwrK_HEALCRTR, KAT_AllyInFight);
    r.cooldown = 50;
    r.target_cooldown = 200;
    ally(7, 10);
    ally(8, 30);
    keeper_assist_note_cast(&st, 0, 7, 300, 1000);
    st.last_cast_turn = 0;                 // only the rule's own pacing here
    CHECK(decide(1049).slot == -1);        // rule cooldown
    KeeperAssistDecision d = decide(1050);
    CHECK((d.slot == 0 && d.cand == 1));   // 7 is still on its target cooldown, so 8
    CHECK(decide(1200).cand == 0);         // 7 again once 200 turns have passed
}

TEST_CASE_METHOD(Fixture, "min_gap between any two casts, and a pending charged cast, hold casts but not power_off", "[kfx_ai][keeper_assist]") {
    rule(0, PwrK_HEALCRTR, KAT_AllyInFight);
    KeeperAssistRule &off = rule(1, PwrK_CALL2ARMS, KAT_None, KAA_PowerOff);
    cond(off, KAF_Gold, KAO_Lt, 5000);
    ally(1, 10);
    st.last_cast_turn = 998;
    st.globals.min_gap = 4;
    CHECK(decide(1000).slot == -1);        // gap not passed; Call to Arms is not on
    facts[KAF_CtaActive] = 1;
    facts[KAF_Gold] = 4000;
    KeeperAssistDecision d = decide(1000);
    CHECK((d.slot == 1 && d.cand == -1));  // the gold guard still acts
    facts[KAF_CtaActive] = 0;
    CHECK(decide(1002).slot == 0);
    CHECK(decide(1002, false).slot == -1); // a charged cast waiting: no other cast
}

TEST_CASE_METHOD(Fixture, "power_off only while the power is on, by dungeon facts alone", "[kfx_ai][keeper_assist]") {
    KeeperAssistRule &off = rule(0, PwrK_OBEY, KAT_None, KAA_PowerOff);
    cond(off, KAF_Gold, KAO_Lt, 800);
    facts[KAF_Gold] = 500;
    CHECK(decide().slot == -1);            // Must Obey is off
    facts[KAF_ObeyActive] = 1;
    CHECK(decide().slot == 0);
    facts[KAF_Gold] = 900;
    CHECK(decide().slot == -1);
}

TEST_CASE_METHOD(Fixture, "money: unavailable, cannot afford, reserve, rule budget, keeper cap -- each recorded", "[kfx_ai][keeper_assist]") {
    KeeperAssistRule &r = rule(0, PwrK_HEALCRTR, KAT_AllyInFight);
    cond(r, KAF_Gold, KAO_Gt, 100);        // the rule itself would allow it
    ally(1, 10);
    stub.unavailable.insert(PwrK_HEALCRTR);
    CHECK(decide().slot == -1);
    CHECK(st.run[0].last_refusal == KAR_NotAvailable);
    stub.unavailable.clear();
    facts[KAF_Gold] = 250;                 // the price is 300
    CHECK(decide().slot == -1);
    CHECK(st.run[0].last_refusal == KAR_CannotAfford);
    facts[KAF_Gold] = 1000;
    st.globals.reserve = 800;              // 1000 - 300 would leave 700
    CHECK(decide().slot == -1);
    CHECK(st.run[0].last_refusal == KAR_Reserve);
    st.globals.reserve = 700;              // exactly the reserve is allowed
    CHECK(decide().slot == 0);
    CHECK(st.run[0].last_refusal == KAR_None);
    r.budget = 500;
    st.run[0].spent_this_payday = 300;
    CHECK(decide().slot == -1);
    CHECK(st.run[0].last_refusal == KAR_Budget);
    r.budget = 0;
    st.globals.max_spend = 400;
    st.spent_this_payday = 200;
    CHECK(decide().slot == -1);
    CHECK(st.run[0].last_refusal == KAR_MaxSpend);
}

TEST_CASE_METHOD(Fixture, "the price follows the rule's level", "[kfx_ai][keeper_assist]") {
    KeeperAssistRule &r = rule(0, PwrK_SPEEDCRTR, KAT_AllyInFight);
    r.level = 3;
    ally(1, 50);
    CHECK(decide().price == 400);
}

TEST_CASE_METHOD(Fixture, "a recorded cast updates the counters, the budgets, the target memory and the log", "[kfx_ai][keeper_assist]") {
    rule(2, PwrK_HEALCRTR, KAT_AllyInFight).level = 2;
    keeper_assist_note_cast(&st, 2, 41, 500, 1234);
    keeper_assist_note_cast(&st, 2, 42, 500, 1300);
    CHECK((st.run[2].fired_total == 2 && st.run[2].spent_total == 1000 && st.run[2].spent_this_payday == 1000));
    CHECK((st.spent_this_payday == 1000 && st.last_cast_turn == 1300 && st.run[2].last_cast_turn == 1300));
    CHECK((st.run[2].recent_target[0] == 41 && st.run[2].recent_target[1] == 42));
    CHECK(st.log_total == 2);
    CHECK((st.log[1].turn == 1300 && st.log[1].target == 42 && st.log[1].cost == 500 && st.log[1].power == PwrK_HEALCRTR
           && st.log[1].slot == 2 && st.log[1].level == 2));
    for (int i = 0; i < 20; i++) keeper_assist_note_cast(&st, 2, 100 + i, 1, 2000 + i);
    CHECK(st.log_total == 22);
    CHECK(st.log[21 % KEEPER_ASSIST_LOG].target == 119); // a ring
}
