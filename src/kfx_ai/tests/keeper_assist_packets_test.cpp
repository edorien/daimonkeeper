// kfx_ai: keeper_assist_packets.c -- the combat assistant's rules as packets (docs/newFeatures/llm/02-rule-language.md
// section 8): what a wanted set turns into, and what the game makes of the packets. Plan, then apply every packet to the
// state, and the live set must be exactly the wanted one; only changed slots travel; a half-sent set never goes live.
#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "keeper_assist_packets.h"
#include "keeper_assist_state.h"
#include "config_magic.h"
#include "kfx_config_state.h"
#include "packet_data.h"

namespace {

struct Fixture {
    KeeperAssistState st;
    KeeperAssistRule want[KEEPER_ASSIST_RULES];
    KeeperAssistGlobals wg;
    KeeperAssistPacket pk[KEEPER_ASSIST_MAX_PACKETS];
    int64_t saved_types;

    Fixture() : saved_types(kfx_config_state.conf.magic_conf.power_types_count) {
        kfx_config_state.conf.magic_conf.power_types_count = PwrK_MKTUNNELLER + 1;
        std::memset(&st, 0, sizeof(st));
        std::memset(want, 0, sizeof(want));
        keeper_assist_default_globals(&wg);
        wg.enabled = 1;
    }
    ~Fixture() { kfx_config_state.conf.magic_conf.power_types_count = saved_types; }

    KeeperAssistRule &heal(int slot, int32_t below) {
        KeeperAssistRule &r = want[slot];
        keeper_assist_default_rule(&r, KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight);
        r.level = 3; r.budget = 6000; r.target_cooldown = 160; r.pick = KAP_LowestHealth;
        r.cond[0] = {KAF_HealthPct, KAO_Lt, below};
        r.cond[1] = {KAF_GoldAfterWages, KAO_Gt, 2000};
        r.cond[2] = {KAF_Kind, KAO_Ne, 7};
        r.cond[3] = {KAF_Gold, KAO_Ge, -5}; // a negative value survives
        r.cond_count = 4;
        return r;
    }
    int plan() { return keeper_assist_plan_packets(&st, want, &wg, pk); }
    void apply(int n, bool skip_commit = false) {
        for (int i = 0; i < n; i++) {
            if (skip_commit && (pk[i].action == PckA_AssistCommit)) continue;
            keeper_assist_apply_packet(&st, pk[i].action, pk[i].par1, pk[i].par2);
        }
    }
    bool live_is_wanted() const {
        return (std::memcmp(st.live, want, sizeof(want)) == 0) && (std::memcmp(&st.globals, &wg, sizeof(wg)) == 0);
    }
};

int count(const KeeperAssistPacket *pk, int n, uint8_t action) {
    int c = 0;
    for (int i = 0; i < n; i++) if (pk[i].action == action) c++;
    return c;
}

} // namespace

TEST_CASE("pack and unpack a rule: every field, at its limits", "[kfx_ai][keeper_assist_packets]") {
    KeeperAssistRule r;
    keeper_assist_default_rule(&r, KAA_Cast, PwrK_CAVEIN, KAT_EnemyInFight);
    r.level = KEEPER_ASSIST_MAX_LEVEL; r.pick = KAP_MostValuable; r.cond_count = KEEPER_ASSIST_CONDS;
    r.cooldown = 10000; r.target_cooldown = 10000;
    int32_t p2 = 0;
    const int32_t p1 = keeper_assist_pack_rule(15, &r, &p2);
    int slot = -1;
    KeeperAssistRule back;
    keeper_assist_unpack_rule(p1, p2, &slot, &back);
    CHECK(slot == 15);
    CHECK((back.used == 1 && back.action == KAA_Cast && back.power == PwrK_CAVEIN && back.target == KAT_EnemyInFight));
    CHECK((back.level == KEEPER_ASSIST_MAX_LEVEL && back.pick == KAP_MostValuable && back.cond_count == KEEPER_ASSIST_CONDS));
    CHECK((back.cooldown == 10000 && back.target_cooldown == 10000));
}

TEST_CASE_METHOD(Fixture, "a full set goes live exactly as wanted, within the packet limit", "[kfx_ai][keeper_assist_packets]") {
    for (int s = 0; s < KEEPER_ASSIST_RULES; s++) heal(s, 10 + s);
    wg.reserve = 1500; wg.max_spend = 90000; wg.min_gap = 9; wg.interval = 3; wg.charge_time = 0;
    const int n = plan();
    CHECK(n == KEEPER_ASSIST_MAX_PACKETS);
    CHECK(count(pk, n, PckA_AssistCommit) == 1);
    CHECK(pk[n - 1].action == PckA_AssistCommit);
    apply(n);
    CHECK(live_is_wanted());
}

TEST_CASE_METHOD(Fixture, "nothing changed: no packets", "[kfx_ai][keeper_assist_packets]") {
    heal(0, 35);
    apply(plan());
    CHECK(live_is_wanted());
    CHECK(plan() == 0);
}

TEST_CASE_METHOD(Fixture, "one changed rule: only it travels, and the others keep their counters", "[kfx_ai][keeper_assist_packets]") {
    heal(0, 35);
    heal(1, 20);
    heal(2, 50);
    apply(plan());
    st.run[0].fired_total = 7; st.run[0].spent_this_payday = 900;
    st.run[2].fired_total = 3;
    want[1].cond[0].value = 25;
    const int n = plan();
    CHECK(n == 2 + 4 + 1);                       // the rule, its money, four conditions, the commit
    CHECK(count(pk, n, PckA_AssistStageGlobal) == 0);
    apply(n);
    CHECK(live_is_wanted());
    CHECK((st.run[0].fired_total == 7 && st.run[0].spent_this_payday == 900 && st.run[2].fired_total == 3));
    CHECK(st.run[1].fired_total == 0);          // the changed rule starts afresh
}

TEST_CASE_METHOD(Fixture, "removing a rule is a commit alone", "[kfx_ai][keeper_assist_packets]") {
    heal(0, 35);
    heal(3, 20);
    apply(plan());
    std::memset(&want[3], 0, sizeof(want[3]));
    const int n = plan();
    CHECK(n == 1);
    CHECK(pk[0].action == PckA_AssistCommit);
    apply(n);
    CHECK(live_is_wanted());
    CHECK(st.live[3].used == 0);
}

TEST_CASE_METHOD(Fixture, "a set that never reaches its commit never goes live", "[kfx_ai][keeper_assist_packets]") {
    heal(0, 35);
    apply(plan());
    want[0].cond[0].value = 99;
    heal(5, 10);
    wg.reserve = 4242;
    apply(plan(), true);                         // everything but the commit
    CHECK(st.live[0].cond[0].value == 35);
    CHECK(st.live[5].used == 0);
    CHECK(st.globals.reserve == 0);
    apply(plan());                               // sent again, whole
    CHECK(live_is_wanted());
}

TEST_CASE_METHOD(Fixture, "a rule invalid at the commit is dropped; bad globals leave the old ones", "[kfx_ai][keeper_assist_packets]") {
    heal(0, 35);
    heal(1, 20);
    want[1].target = KAT_EnemyInFight;           // Heal on an enemy: not a rule the game takes
    wg.interval = 7;
    apply(plan());
    CHECK(st.live[0].used == 1);
    CHECK(st.live[1].used == 0);
    CHECK(st.globals.interval == 7);
    keeper_assist_apply_packet(&st, PckA_AssistStageGlobal, KAG_Interval, 0);
    keeper_assist_apply_packet(&st, PckA_AssistCommit, 1, 1);
    CHECK(st.globals.interval == 7);
}

TEST_CASE_METHOD(Fixture, "a pending charged cast of a rule that changes is cancelled", "[kfx_ai][keeper_assist_packets]") {
    heal(0, 35);
    heal(1, 20);
    apply(plan());
    st.pending.used = 1; st.pending.slot = 1;
    want[0].level = 1;                           // another rule changes: the pending one stays
    apply(plan());
    CHECK(st.pending.used == 1);
    want[1].level = 1;
    apply(plan());
    CHECK(st.pending.used == 0);
}

TEST_CASE_METHOD(Fixture, "enable switches at once, without a commit", "[kfx_ai][keeper_assist_packets]") {
    heal(0, 35);
    apply(plan());
    keeper_assist_apply_packet(&st, PckA_AssistEnable, 0, 0);
    CHECK(st.globals.enabled == 0);
    CHECK(st.live[0].used == 1);
    keeper_assist_apply_packet(&st, PckA_AssistEnable, 1, 0);
    CHECK(st.globals.enabled == 1);
}
