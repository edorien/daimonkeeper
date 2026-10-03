// M6c safety net: a creature sent somewhere with move_creature is handed back automatically, so an order the agent forgets
// never costs it pay or food. Conditions: the order's hold time (max_hold), pay day close (payday), pay owed (owed_pay),
// hunger at 3/4 of its limit (hungry), an enemy attacking it (attacked: a held creature does not fight back on its own),
// the agent's connection lost (agent_lost). The pay and hunger conditions also refuse a new order up front
// (PAYDAY_TOO_CLOSE, OWED_PAY, HUNGRY), and an oversized hold is BAD_HOLD. The view reports what was released and why.
#include "ftest_ai_seat_order_autorelease.h"
#include "thing_stats.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <stdlib.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_creature.h"
#include "config_crtrstates.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "creature_control.h"
#include "creature_states.h"
#include "creature_states_combt.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_data.h"
#include "slab_data.h"
#include "thing_creature.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber P = -1;
static NetUserId U = -1;
#define NC 5
static ThingIndex c[NC];
static MapSubtlCoord hx, hy;
static int64_t s_mark = 0;

static struct Thing* T(int i) { return thing_get(c[i]); }
static TbBool manual(int i) { const struct Thing* t = T(i); return t->active_state == CrSt_ManualControl || t->continue_state == CrSt_ManualControl; }
static struct ExtSeatVerb mv(int i, int64_t dx, int64_t hold)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_MoveCreature; v.has_thing = true; v.thing_id = c[i]; v.has_pos = true;
    v.stl_x = hx + dx; v.stl_y = hy + 10; v.hold_turns = hold;
    return v;
}
static const char* submit(struct ExtSeatVerb v) { return extseat_submit_verb_ex(U, P, &v, true, NULL); }
static void expect(const char* what, struct ExtSeatVerb v, const char* code)
{
    const char* e = submit(v);
    if (e == NULL || strcmp(e, code) != 0) SOFT_FAIL("%s: expected %s, got %s", what, code, e ? e : "success");
}
static TbBool released_for(int i, const char* reason)
{
    struct ExtSeatAutoRelease ar[EXTSEAT_AUTO_RELEASE_RING];
    const int64_t n = extseat_auto_releases(U, ar, EXTSEAT_AUTO_RELEASE_RING);
    for (int64_t k = 0; k < n; k++) if (ar[k].thing_id == c[i] && strcmp(ar[k].reason, reason) == 0) return true;
    return false;
}
static struct CreatureControl* cc(int i) { return creature_control_get_from_thing(T(i)); }

FTestActionResult ar01_setup(struct FTestActionArgs* const args);
FTestActionResult ar02_refusals_and_order_short_hold(struct FTestActionArgs* const args);
FTestActionResult ar03_wait_arrive(struct FTestActionArgs* const args);
FTestActionResult ar04_wait_hold(struct FTestActionArgs* const args);
FTestActionResult ar05_check_max_hold_order_more(struct FTestActionArgs* const args);
FTestActionResult ar06_wait_arrive2(struct FTestActionArgs* const args);
FTestActionResult ar07_trigger_conditions(struct FTestActionArgs* const args);
FTestActionResult ar08_check_conditions(struct FTestActionArgs* const args);
FTestActionResult ar09_check_payday_order_last(struct FTestActionArgs* const args);
FTestActionResult ar10_wait_arrive3(struct FTestActionArgs* const args);
FTestActionResult ar10b_order_for_attack(struct FTestActionArgs* const args);
FTestActionResult ar10c_attack_held_creature(struct FTestActionArgs* const args);
FTestActionResult ar10d_check_attacked(struct FTestActionArgs* const args);
FTestActionResult ar11_lost_agent_and_view(struct FTestActionArgs* const args);

void ftest_ai_seat_order_autorelease_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_order_autorelease_init()
{
    s_failures = 0;
    ftest_append_action(ar01_setup, 0, NULL);
    ftest_append_action(ar02_refusals_and_order_short_hold, 2, NULL);
    ftest_append_action(ar03_wait_arrive, 1, NULL);
    ftest_append_action(ar04_wait_hold, 1, NULL);
    ftest_append_action(ar05_check_max_hold_order_more, 1, NULL);
    ftest_append_action(ar06_wait_arrive2, 1, NULL);
    ftest_append_action(ar07_trigger_conditions, 1, NULL);
    ftest_append_action(ar08_check_conditions, 2, NULL);
    ftest_append_action(ar09_check_payday_order_last, 1, NULL);
    ftest_append_action(ar10_wait_arrive3, 1, NULL);
    ftest_append_action(ar10b_order_for_attack, 1, NULL);
    ftest_append_action(ar10c_attack_held_creature, 1, NULL);
    ftest_append_action(ar10d_check_attacked, 1, NULL);
    ftest_append_action(ar11_lost_agent_and_view, 1, NULL);
    return true;
}

FTestActionResult ar01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hx = heart->mappos.x.stl.num; hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(hx) - 4, subtile_slab(hy) + 3, subtile_slab(hx) + 9, subtile_slab(hy) + 5, SlbT_CLAIMED, P);
    const char* kinds[NC] = { "ORC", "TROLL", "ORC", "TROLL", "ORC" };
    for (int i = 0; i < NC; i++) {
        struct Thing* t = ftest_util_create_creature(subtile_coord_center(hx - 6 + 2 * i), subtile_coord_center(hy + 10), P, 9, (ThingModel)creature_model_id(kinds[i]));
        if (thing_is_invalid(t)) { FTEST_FAIL_TEST("no creature %d", i); return FTRs_Go_To_Next_Action; }
        c[i] = t->index;
    }
    FTESTLOG("pay day: gap %" PRId64 ", progress %" PRId64, (int64_t)kfx_config_state.conf.rules[P].gameplay.pay_day_gap, (int64_t)kfx_config_state.pay_day_progress[P]);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ar02_refusals_and_order_short_hold(struct FTestActionArgs* const args)
{
    const int64_t gap = kfx_config_state.conf.rules[P].gameplay.pay_day_gap;
    expect("a hold beyond half the pay day gap", mv(0, 6, gap), "BAD_HOLD");
    expect("a negative hold", mv(0, 6, -5), "BAD_HOLD");
    // Hunger at 3/4 of the limit refuses the order.
    const struct CreatureModelConfig* cf = creature_stats_get_from_thing(T(4));
    if (cf->hunger_rate > 0) {
        cc(4)->hunger_level = (cf->hunger_rate * 3) / 4 + 1;
        expect("a creature that is nearly hungry", mv(4, 8, 0), "HUNGRY");
        cc(4)->hunger_level = 0;
    } else SOFT_FAIL("the test creature has no hunger rate");
    cc(3)->paydays_owed = 1;
    expect("a creature owed pay", mv(3, 8, 0), "OWED_PAY");
    cc(3)->paydays_owed = 0;
    const int64_t saved = kfx_config_state.pay_day_progress[P];
    kfx_config_state.pay_day_progress[P] = gap - 50;
    expect("an order just before pay day", mv(2, 8, 0), "PAYDAY_TOO_CLOSE");
    kfx_config_state.pay_day_progress[P] = saved;
    CHECK_TRUE("refused orders left nothing queued or tracked", extseat_idle(U) && extseat_ordered_count(U) == 0);

    CHECK_TRUE("an order with the shortest allowed hold is accepted", submit(mv(0, 6, EXTSEAT_ORDER_MIN_HOLD_TURNS)) == NULL);
    s_mark = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}
FTestActionResult ar03_wait_arrive(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("the ordered creature is held and tracked", manual(0) && extseat_ordered_count(U) == 1);
    return FTRs_Go_To_Next_Action;
}
FTestActionResult ar04_wait_hold(struct FTestActionArgs* const args)
{
    return ((int64_t)get_gameturn() < s_mark + EXTSEAT_ORDER_MIN_HOLD_TURNS + 20) ? FTRs_Repeat_Current_Action : FTRs_Go_To_Next_Action;
}

FTestActionResult ar05_check_max_hold_order_more(struct FTestActionArgs* const args)
{
    CHECK_TRUE("the hold time ran out: the creature was handed back", !manual(0));
    CHECK_TRUE("the view's record says max_hold", released_for(0, "max_hold"));
    CHECK_TRUE("and it is no longer tracked", extseat_ordered_count(U) == 0);
    // Order three creatures (default hold) for the condition checks.
    CHECK_TRUE("order 1", submit(mv(1, 4, 0)) == NULL);
    CHECK_TRUE("order 2", submit(mv(2, 8, 0)) == NULL);
    CHECK_TRUE("order 3", submit(mv(3, 10, 0)) == NULL);
    s_mark = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}
FTestActionResult ar06_wait_arrive2(struct FTestActionArgs* const args)
{
    if (manual(1) && manual(2) && manual(3)) return FTRs_Go_To_Next_Action;
    if (args->intended_start_at_game_turn > 0 && (int64_t)get_gameturn() > args->intended_start_at_game_turn + 300) {
        struct ExtSeatResult res[EXTSEAT_RESULT_RING];
        const int64_t n = extseat_results(U, res, EXTSEAT_RESULT_RING);
        for (int64_t k = 0; k < n; k++) FTESTLOG("result id %" PRId64 ": %s %s", res[k].id, res[k].rejected ? "rejected" : "done", res[k].rejected ? res[k].error : "");
        struct ExtSeatAutoRelease ar[EXTSEAT_AUTO_RELEASE_RING];
        const int64_t na = extseat_auto_releases(U, ar, EXTSEAT_AUTO_RELEASE_RING);
        for (int64_t k = 0; k < na; k++) FTESTLOG("auto release: thing %" PRId64 " %s at turn %" PRId64, ar[k].thing_id, ar[k].reason, ar[k].turn);
        for (int i = 1; i < 4; i++) FTESTLOG("creature %d thing %" PRId64 " state %s continue %s at (%" PRId64 ",%" PRId64 ")", i, (int64_t)c[i], creature_state_code_name(T(i)->active_state), creature_state_code_name(T(i)->continue_state), (int64_t)T(i)->mappos.x.stl.num, (int64_t)T(i)->mappos.y.stl.num);
        SOFT_FAIL("creatures not all held: %d %d %d, idle %d, queued verbs %" PRId64, (int)manual(1), (int)manual(2), (int)manual(3), (int)extseat_idle(U), extseat_queued_verbs(U));
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Repeat_Current_Action;
}

FTestActionResult ar07_trigger_conditions(struct FTestActionArgs* const args)
{
    CHECK_TRUE("three creatures tracked", extseat_ordered_count(U) == 3);
    const struct CreatureModelConfig* cf = creature_stats_get_from_thing(T(1));
    cc(1)->hunger_level = (cf->hunger_rate * 3) / 4 + 1;      // creature 1 grows hungry
    cc(2)->paydays_owed = 1;                                  // creature 2 is owed pay
    return FTRs_Go_To_Next_Action;                            // creature 3 is left for the lost-agent case
}

static int64_t s_saved_progress = 0;

FTestActionResult ar08_check_conditions(struct FTestActionArgs* const args)
{
    CHECK_TRUE("hungry creature handed back", !manual(1) && released_for(1, "hungry"));
    CHECK_TRUE("creature owed pay handed back", !manual(2) && released_for(2, "owed_pay"));
    CHECK_TRUE("the untouched creature is still held", manual(3));
    // Pay day close releases the remaining one on the next turn.
    s_saved_progress = kfx_config_state.pay_day_progress[P];
    kfx_config_state.pay_day_progress[P] = kfx_config_state.conf.rules[P].gameplay.pay_day_gap - 50;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ar09_check_payday_order_last(struct FTestActionArgs* const args)
{
    CHECK_TRUE("pay day close: the last creature handed back", !manual(3) && released_for(3, "payday"));
    kfx_config_state.pay_day_progress[P] = s_saved_progress;
    CHECK_TRUE("order for the lost-agent case", submit(mv(4, 6, 0)) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ar10_wait_arrive3(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    return manual(4) ? FTRs_Go_To_Next_Action : FTRs_Repeat_Current_Action;
}

static ThingIndex s_hero = 0;
static GameTurn s_hero_creation = 0;

FTestActionResult ar10b_order_for_attack(struct FTestActionArgs* const args)
{
    // Creature 0 (free since its max_hold release) is held well away from creature 4, which stays held for ar11.
    CHECK_TRUE("order for the attacked case", submit(mv(0, -8, 0)) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ar10c_attack_held_creature(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U) || !manual(0)) return FTRs_Repeat_Current_Action;
    struct Thing* hero = ftest_util_create_creature(subtile_coord_center(T(0)->mappos.x.stl.num - 2), subtile_coord_center(T(0)->mappos.y.stl.num),
        PLAYER_GOOD, 1, (ThingModel)creature_model_id("THIEF"));
    if (thing_is_invalid(hero)) { SOFT_FAIL("no hero to attack with"); return FTRs_Go_To_Next_Action; }
    s_hero = hero->index; s_hero_creation = hero->creation_turn;
    // The same entry point a hero uses when it picks a fight: registers it in the held creature's opponent list.
    CHECK_TRUE("the hero engages the held creature", set_creature_in_combat_to_the_death(hero, T(0), AttckT_Melee));
    s_mark = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ar10d_check_attacked(struct FTestActionArgs* const args)
{
    struct Thing* hero = thing_get(s_hero);
    const TbBool hero_alive = !thing_is_invalid(hero) && thing_is_creature(hero) && (hero->creation_turn == s_hero_creation);
    const TbBool released = !manual(0) && released_for(0, "attacked");
    // Handed back, and then actually fighting: in combat, or it has already killed the hero.
    if (released && (!hero_alive || T(0)->active_state == CrSt_CreatureInCombat)) {
        if (hero_alive) kill_creature(hero, INVALID_THING, P, CrDed_NoEffects);
        return FTRs_Go_To_Next_Action;
    }
    if ((int64_t)get_gameturn() < s_mark + 200) return FTRs_Repeat_Current_Action;
    SOFT_FAIL("an attacked held creature was not handed back to fight: manual %d, released(attacked) %d, state %s, hero alive %d",
        (int)manual(0), (int)released_for(0, "attacked"), creature_state_code_name(T(0)->active_state), (int)hero_alive);
    if (hero_alive) kill_creature(hero, INVALID_THING, P, CrDed_NoEffects);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ar11_lost_agent_and_view(struct FTestActionArgs* const args)
{
    CHECK_TRUE("held before the connection drops", manual(4) && extseat_ordered_count(U) == 1);
    VALUE before; api_seat_build_view(&before, P);
    CHECK_TRUE("the view counts the held creature", value_int64(value_dict_get(value_dict_get(&before, "seat"), "ordered_creatures")) == 1);
    value_fini(&before);
    extseat_on_client_lost();
    CHECK_TRUE("a lost agent connection hands the creature back", !manual(4) && released_for(4, "agent_lost"));
    VALUE v; api_seat_build_view(&v, P);
    VALUE* seat = value_dict_get(&v, "seat");
    CHECK_TRUE("the view counts none held now", value_int64(value_dict_get(seat, "ordered_creatures")) == 0);
    VALUE* ar = value_dict_get(seat, "auto_released");
    CHECK_TRUE("the view lists the six releases with their reasons", ar && value_array_size(ar) == 6);
    TbBool has_reason[6] = {0};
    static const char* reasons[6] = { "max_hold", "hungry", "owed_pay", "payday", "attacked", "agent_lost" };
    for (size_t k = 0; ar && k < value_array_size(ar); k++)
        for (int r = 0; r < 6; r++) if (strcmp(value_string(value_dict_get(value_array_get(ar, k), "reason")), reasons[r]) == 0) has_reason[r] = true;
    for (int r = 0; r < 6; r++) if (!has_reason[r]) SOFT_FAIL("the view does not list a release for %s", reasons[r]);
    value_fini(&v);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " safety-net check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: forgotten orders are handed back on hold time, pay day, owed pay, hunger, an attack and a lost agent");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
