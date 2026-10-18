// docs/newFeatures/llm/03 P2: the combat assistant's rules set through an External seat (set_assist's verb, ESV_SetAssist):
// planned as packets, written one a turn, live only on the commit's turn; then acting in a fight; reported in own.assist;
// a one-rule change sending only that rule; and the decision-due reasons for a spent budget, the gold guard and the
// whole set's cap for the pay day (max_spend).
#include "ftest_ai_seat_assist.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_decision.h"
#include "api_seat_view.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_players.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "keeper_assist_packets.h"
#include "keeper_assist_state.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "magic_powers.h"
#include "net_game.h"
#include "player_data.h"
#include "power_process.h"
#include "slab_data.h"
#include "thing_creature.h"
#include "thing_list.h"
#include "thing_navigate.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber P = -1;
static NetUserId U = -1;
static ThingIndex orc = 0, hero = 0;
static int64_t s_phase_start = 0, s_planned = 0, s_live_turn = 0;
static TbBool s_live_early = false;
static int64_t s_seq_before = 0;
static struct ExtSeatVerb s_verb;

static int64_t now(void) { return (int64_t)get_gameturn(); }
static struct KeeperAssistState* st(void) { return &kfx_sim_state.keeper_assist[P]; }
static struct Dungeon* dg(void) { return get_players_dungeon(get_player(P)); }
static struct Thing* creature(ThingIndex i) { struct Thing* t = thing_get(i); return (thing_is_invalid(t) || !thing_is_creature(t)) ? NULL : t; }
static void hold(int64_t pct)
{
    struct Thing* h = creature(hero);
    if (h) h->health = creature_control_get_from_thing(h)->max_health;
    struct Thing* a = creature(orc);
    if (a) a->health = (HitPoints)(creature_control_get_from_thing(a)->max_health * pct / 100);
}
static TbBool phase_over(int64_t turns) { return now() - s_phase_start >= turns; }
static void cond(struct KeeperAssistRule* r, enum KeeperAssistFact f, enum KeeperAssistOp op, int32_t v)
{
    r->cond[r->cond_count].fact = (uint8_t)f; r->cond[r->cond_count].op = (uint8_t)op; r->cond[r->cond_count].value = v; r->cond_count++;
}
static const char* submit(void)
{
    struct ExtSeatSubmitInfo info; memset(&info, 0, sizeof(info));
    const char* e = extseat_submit_verb_ex(U, P, &s_verb, true, &info);
    s_planned = info.steps;
    return e;
}
static TbBool reasons_have(const char* what)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* arr = value_dict_get(value_dict_get(value_dict_get(&v, "seat"), "decision"), "reasons");
    TbBool found = false;
    for (size_t i = 0; arr && i < value_array_size(arr); i++) if (strcmp(value_string(value_array_get(arr, i)), what) == 0) found = true;
    value_fini(&v);
    return found;
}

FTestActionResult sa01_setup(struct FTestActionArgs* const args);
FTestActionResult sa02_submit_rules(struct FTestActionArgs* const args);
FTestActionResult sa03_wait_live(struct FTestActionArgs* const args);
FTestActionResult sa04_check_heal_and_view(struct FTestActionArgs* const args);
FTestActionResult sa05_check_change_then_budget(struct FTestActionArgs* const args);
FTestActionResult sa06_check_budget_reason_then_cta(struct FTestActionArgs* const args);
FTestActionResult sa07_guard(struct FTestActionArgs* const args);
FTestActionResult sa08_check_guard_reason(struct FTestActionArgs* const args);
FTestActionResult sa09_check_cap_reason(struct FTestActionArgs* const args);

void ftest_ai_seat_assist_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_assist_init()
{
    s_failures = 0;
    ftest_append_action(sa01_setup, 0, NULL);
    ftest_append_action(sa02_submit_rules, 1, NULL);
    ftest_append_action(sa03_wait_live, 1, NULL);
    ftest_append_action(sa04_check_heal_and_view, 1, NULL);
    ftest_append_action(sa05_check_change_then_budget, 1, NULL);
    ftest_append_action(sa06_check_budget_reason_then_cta, 1, NULL);
    ftest_append_action(sa07_guard, 1, NULL);
    ftest_append_action(sa08_check_guard_reason, 1, NULL);
    ftest_append_action(sa09_check_cap_reason, 1, NULL);
    return true;
}

FTestActionResult sa01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0) { FTEST_FAIL_TEST("no second keeper"); return FTRs_Go_To_Next_Action; }
    U = net_add_external_seat(P);
    if (U < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    struct Dungeon* d = dg();
    d->total_money_owned = 1000000;
    const PowerKind powers[] = { PwrK_HEALCRTR, PwrK_SPEEDCRTR, PwrK_CALL2ARMS };
    for (size_t i = 0; i < sizeof(powers) / sizeof(powers[0]); i++) d->magic_level[powers[i]] = 8;
    kfx_config_state.pay_day_progress[P] = 0;
    api_seat_decision_set_min_interval(5);
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(hx) - 5, subtile_slab(hy) + 3, subtile_slab(hx) + 5, subtile_slab(hy) + 6, SlbT_CLAIMED, P);
    struct Thing* a = ftest_util_create_creature(subtile_coord_center(hx - 1), subtile_coord_center(hy + 4), P, 9, (ThingModel)creature_model_id("ORC"));
    struct Thing* h = ftest_util_create_creature(subtile_coord_center(hx + 1), subtile_coord_center(hy + 4), PLAYER_GOOD, 9, (ThingModel)creature_model_id("BARBARIAN"));
    if (thing_is_invalid(a) || thing_is_invalid(h)) { FTEST_FAIL_TEST("could not create the creatures"); return FTRs_Go_To_Next_Action; }
    orc = a->index; hero = h->index;
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sa02_submit_rules(struct FTestActionArgs* const args)
{
    hold(100);
    const struct Thing* a = creature(orc);
    if (!a || creature_control_get_from_thing(a)->battle_id == 0) {
        if (phase_over(400)) { FTEST_FAIL_TEST("no fight"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    // Two rules: heal under 35%, and speed for a creature in a fight while the enemy is stronger (never here).
    memset(&s_verb, 0, sizeof(s_verb));
    s_verb.kind = ESV_SetAssist;
    keeper_assist_default_globals(&s_verb.assist_globals);
    s_verb.assist_globals.enabled = 1;
    s_verb.assist_globals.charge_time = 0;
    struct KeeperAssistRule* heal = &s_verb.assist_rules[0];
    keeper_assist_default_rule(heal, KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight);
    cond(heal, KAF_HealthPct, KAO_Lt, 35);
    cond(heal, KAF_GoldAfterWages, KAO_Gt, 1000);
    struct KeeperAssistRule* speed = &s_verb.assist_rules[1];
    keeper_assist_default_rule(speed, KAA_Cast, PwrK_SPEEDCRTR, KAT_AllyInFight);
    cond(speed, KAF_ForceRatio, KAO_Gt, 100000);
    const char* e = submit();
    if (e) { FTEST_FAIL_TEST("set_assist refused: %s", e); return FTRs_Go_To_Next_Action; }
    FTESTLOG("rule set: %" PRId64 " packet(s)", s_planned);
    CHECK_TRUE("two rules (2 + 2 conditions, 2 + 1), the six globals and the commit", s_planned == (2 + 2) + (2 + 1) + 6 + 1);
    s_phase_start = now();
    s_live_turn = 0;
    s_live_early = false;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sa03_wait_live(struct FTestActionArgs* const args)
{
    hold(100);
    if (!extseat_idle(U)) {
        if (st()->live[0].used || st()->globals.enabled) s_live_early = true; // a staged packet must not change what is live
        if (phase_over(100)) { FTEST_FAIL_TEST("the rule packets never finished"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    if (!st()->live[0].used) {
        if (phase_over(110)) { FTEST_FAIL_TEST("the rules never went live"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action; // the commit packet is processed the turn after it is written
    }
    s_live_turn = now();
    FTESTLOG("rules live %" PRId64 " turns after the submit", s_live_turn - s_phase_start);
    CHECK_TRUE("nothing went live before the commit", !s_live_early);
    CHECK_TRUE("the live rules are exactly the ones asked for",
               memcmp(st()->live, s_verb.assist_rules, sizeof(s_verb.assist_rules)) == 0
               && memcmp(&st()->globals, &s_verb.assist_globals, sizeof(s_verb.assist_globals)) == 0);
    CHECK_TRUE("they took about one turn a packet", s_live_turn - s_phase_start >= s_planned);
    CHECK_TRUE("the same set again would send nothing",
               keeper_assist_plan_packets(st(), s_verb.assist_rules, &s_verb.assist_globals, (struct KeeperAssistPacket[KEEPER_ASSIST_MAX_PACKETS]){0}) == 0);
    hold(30);
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sa04_check_heal_and_view(struct FTestActionArgs* const args)
{
    hold(30);
    if (st()->run[0].fired_total == 0) {
        if (phase_over(20)) { SOFT_FAIL("the rule set through the seat never healed"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    VALUE v; api_seat_build_view(&v, P);
    VALUE* a = value_dict_get(value_dict_get(&v, "own"), "assist");
    VALUE* rules = a ? value_dict_get(a, "rules") : NULL;
    VALUE* casts = a ? value_dict_get(a, "casts") : NULL;
    CHECK_TRUE("own.assist is in the view, enabled", a && value_bool(value_dict_get(a, "enabled")));
    CHECK_TRUE("it lists both rules", rules && value_array_size(rules) == 2);
    if (rules && value_array_size(rules) == 2) {
        VALUE* r0 = value_array_get(rules, 0);
        CHECK_TRUE("rule 0 by name, fired once", strcmp(value_string(value_dict_get(r0, "power")), "POWER_HEAL_CREATURE") == 0
                   && value_int64(value_dict_get(r0, "fired")) == 1 && strcmp(value_string(value_dict_get(r0, "state")), "ok") == 0);
        VALUE* w = value_array_get(value_dict_get(r0, "when"), 0);
        CHECK_TRUE("its condition in the agent's words", w && strcmp(value_string(value_array_get(w, 0)), "health_pct") == 0
                   && strcmp(value_string(value_array_get(w, 1)), "<") == 0 && value_int64(value_array_get(w, 2)) == 35);
    }
    CHECK_TRUE("the cast is in own.assist.casts", casts && value_array_size(casts) == 1
               && value_int64(value_dict_get(value_array_get(casts, 0), "target")) == orc);
    value_fini(&v);
    // Change the heal rule only, giving it a budget of one heal: one rule's packets and the commit travel.
    st()->run[1].fired_total = 5; // a counter the change must leave alone
    s_verb.assist_rules[0].budget = (int32_t)compute_power_price(P, PwrK_HEALCRTR, 0);
    s_verb.assist_rules[0].target_cooldown = 0;
    s_verb.assist_rules[0].cooldown = 0;
    const char* e = submit();
    if (e) SOFT_FAIL("the change was refused: %s", e);
    FTESTLOG("one-rule change: %" PRId64 " packet(s)", s_planned);
    CHECK_TRUE("only the changed rule and the commit", s_planned == 2 + 2 + 1);
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sa05_check_change_then_budget(struct FTestActionArgs* const args)
{
    hold(30);
    if (st()->live[0].budget == 0) {
        if (phase_over(30)) { SOFT_FAIL("the change never went live"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    CHECK_TRUE("the unchanged rule kept its counter", st()->run[1].fired_total == 5);
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sa06_check_budget_reason_then_cta(struct FTestActionArgs* const args)
{
    hold(30);
    if (!reasons_have("assist_budget")) {
        if (phase_over(40)) { SOFT_FAIL("no assist_budget decision after the budget was spent (refusal %d)", (int)st()->run[0].last_refusal); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    FTESTLOG("assist_budget raised %" PRId64 " turns after the change went live", now() - s_phase_start);
    CHECK_TRUE("the heal rule spent its budget", st()->run[0].last_refusal == KAR_Budget);
    // Call to Arms on another keeper's ground, and a gold guard for it.
    const struct Thing* other = find_players_dungeon_heart(my_player_number);
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_CastPower; snprintf(v.name, sizeof(v.name), "POWER_CALL_TO_ARMS");
    v.has_pos = true; v.stl_x = other->mappos.x.stl.num; v.stl_y = other->mappos.y.stl.num + 3;
    if (extseat_submit_verb_ex(U, P, &v, true, NULL)) SOFT_FAIL("could not cast call to arms");
    memset(&s_verb.assist_rules[1], 0, sizeof(s_verb.assist_rules[1]));
    struct KeeperAssistRule* guard = &s_verb.assist_rules[1];
    keeper_assist_default_rule(guard, KAA_PowerOff, PwrK_CALL2ARMS, KAT_None);
    cond(guard, KAF_Gold, KAO_Lt, 500000);
    cond(guard, KAF_CtaPaying, KAO_Eq, 1);
    if (submit()) SOFT_FAIL("the guard was refused");
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sa07_guard(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U) || (st()->live[1].action != KAA_PowerOff) || !player_uses_power_call_to_arms(P)) {
        if (phase_over(60)) { SOFT_FAIL("the guard or the banner never came"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    dg()->total_money_owned = 400000;
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sa08_check_guard_reason(struct FTestActionArgs* const args)
{
    if (!reasons_have("assist_gold_guard")) {
        if (phase_over(30)) SOFT_FAIL("no assist_gold_guard decision (call to arms on %d)", (int)player_uses_power_call_to_arms(P));
        else return FTRs_Repeat_Current_Action;
    }
    CHECK_TRUE("the guard switched call to arms off", !player_uses_power_call_to_arms(P));
    // The whole set's cap for the pay day (the live bug: 14,300 gold in one pay day with none): the heal rule's own
    // budget off, a cap below one heal, and the fight brought back (Call to Arms took the orc away).
    s_verb.assist_rules[0].budget = 0;
    s_verb.assist_globals.max_spend = 1;
    if (submit()) SOFT_FAIL("the cap was refused");
    struct Thing* a = creature(orc);
    struct Thing* h = creature(hero);
    if (a && h) {
        struct Coord3d pos = a->mappos;
        pos.x.val += COORD_PER_STL;
        move_thing_in_map(h, &pos);
    }
    TbBool combat;
    int64_t turn;
    api_seat_decision_latest(P, &s_seq_before, &turn, &combat);
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sa09_check_cap_reason(struct FTestActionArgs* const args)
{
    hold(30);
    int64_t seq = 0, turn = 0;
    TbBool combat = false;
    api_seat_decision_latest(P, &seq, &turn, &combat);
    if ((st()->globals.max_spend != 1) || (st()->run[0].last_refusal != KAR_MaxSpend) || (seq <= s_seq_before) || !reasons_have("assist_budget")) {
        if (phase_over(300)) {
            SOFT_FAIL("no assist_budget decision once the cap held a heal back (cap %d, refusal %d, decisions %" PRId64 " -> %" PRId64 ")",
                      (int)st()->globals.max_spend, (int)st()->run[0].last_refusal, s_seq_before, seq);
        } else {
            return FTRs_Repeat_Current_Action;
        }
    } else {
        FTESTLOG("assist_budget raised for the set's cap %" PRId64 " turns after it was sent", now() - s_phase_start);
    }
    api_seat_decision_set_min_interval(100);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " set_assist check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: rules set through the seat, live on the commit, acting, in the view, minimal changes, decision reasons");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
