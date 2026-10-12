// docs/newFeatures/llm/03 P1: the combat assistant's spending discipline in a real game (kfx_ai/keeper_assist.c), rules
// put straight into the sim state. A reserve the casts may not cross, to the gold piece; a rule budget that runs out and
// comes back at pay day; the gold guard switching off Call to Arms when it is being charged for (and leaving it alone
// on the keeper's own ground, where it is free), and Must Obey under a floor; nothing at all in a multiplayer game.
#include "ftest_keeper_assist_money.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "cheat_mode.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_players.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
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

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

#define RESERVE 5000

static PlayerNumber P = -1;
static NetUserId U = -1;
static ThingIndex orc = 0, hero = 0;
static MapSubtlCoord s_hx, s_hy;
static int64_t s_phase_start = 0;
static uint64_t s_log_mark = 0;

static int64_t now(void) { return (int64_t)get_gameturn(); }
static struct KeeperAssistState* st(void) { return &kfx_sim_state.keeper_assist[P]; }
static struct Dungeon* dg(void) { return get_players_dungeon(get_player(P)); }
static GoldAmount heal_price(void) { return compute_power_price(P, PwrK_HEALCRTR, 0); }
static struct Thing* creature(ThingIndex i) { struct Thing* t = thing_get(i); return (thing_is_invalid(t) || !thing_is_creature(t)) ? NULL : t; }
static void hold(void)
{
    struct Thing* h = creature(hero);
    if (h) h->health = creature_control_get_from_thing(h)->max_health;
    struct Thing* a = creature(orc);
    if (a) a->health = (HitPoints)(creature_control_get_from_thing(a)->max_health * 30 / 100);
}
static struct KeeperAssistRule* only_rule(enum KeeperAssistAction a, PowerKind power, enum KeeperAssistTarget target)
{
    memset(st()->live, 0, sizeof(st()->live));
    memset(st()->run, 0, sizeof(st()->run));
    st()->pending.used = 0;
    st()->last_cast_turn = 0;
    keeper_assist_default_rule(&st()->live[0], a, power, target);
    s_log_mark = st()->log_total;
    s_phase_start = now();
    return &st()->live[0];
}
static void cond(struct KeeperAssistRule* r, enum KeeperAssistFact f, enum KeeperAssistOp op, int32_t v)
{
    r->cond[r->cond_count].fact = (uint8_t)f; r->cond[r->cond_count].op = (uint8_t)op; r->cond[r->cond_count].value = v; r->cond_count++;
}
static struct KeeperAssistRule* heal_rule(void)
{
    struct KeeperAssistRule* r = only_rule(KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight);
    r->target_cooldown = 0;
    r->cooldown = 0;
    cond(r, KAF_HealthPct, KAO_Lt, 35);
    return r;
}
static int64_t casts_since_mark(void) { return (int64_t)(st()->log_total - s_log_mark); }
static TbBool phase_over(int64_t turns) { return now() - s_phase_start >= turns; }
static void seat_cast_at(PowerKind power, MapSubtlCoord x, MapSubtlCoord y)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_CastPower; snprintf(v.name, sizeof(v.name), "%s", power_code_name(power));
    if (x >= 0) { v.has_pos = true; v.stl_x = x; v.stl_y = y; }
    const char* e = extseat_submit_verb_ex(U, P, &v, true, NULL);
    if (e) SOFT_FAIL("casting %s through the seat: %s", v.name, e);
}

FTestActionResult kam01_setup(struct FTestActionArgs* const args);
FTestActionResult kam02_wait_fight_then_reserve(struct FTestActionArgs* const args);
FTestActionResult kam03_check_held_then_exact(struct FTestActionArgs* const args);
FTestActionResult kam04_check_exact_then_budget(struct FTestActionArgs* const args);
FTestActionResult kam05_check_budget_then_payday(struct FTestActionArgs* const args);
FTestActionResult kam06_check_payday_then_cta(struct FTestActionArgs* const args);
FTestActionResult kam07_cta_guard(struct FTestActionArgs* const args);
FTestActionResult kam08_check_cta_off_then_free(struct FTestActionArgs* const args);
FTestActionResult kam09_check_free_cta_then_obey(struct FTestActionArgs* const args);
FTestActionResult kam10_check_obey_then_multiplayer(struct FTestActionArgs* const args);
FTestActionResult kam11_check_multiplayer_then_local(struct FTestActionArgs* const args);
FTestActionResult kam12_check_local(struct FTestActionArgs* const args);

void ftest_keeper_assist_money_pre_start() { fe_computer_players = 1; }

TbBool ftest_keeper_assist_money_init()
{
    s_failures = 0;
    ftest_append_action(kam01_setup, 0, NULL);
    ftest_append_action(kam02_wait_fight_then_reserve, 1, NULL);
    ftest_append_action(kam03_check_held_then_exact, 1, NULL);
    ftest_append_action(kam04_check_exact_then_budget, 1, NULL);
    ftest_append_action(kam05_check_budget_then_payday, 1, NULL);
    ftest_append_action(kam06_check_payday_then_cta, 1, NULL);
    ftest_append_action(kam07_cta_guard, 1, NULL);
    ftest_append_action(kam08_check_cta_off_then_free, 1, NULL);
    ftest_append_action(kam09_check_free_cta_then_obey, 1, NULL);
    ftest_append_action(kam10_check_obey_then_multiplayer, 1, NULL);
    ftest_append_action(kam11_check_multiplayer_then_local, 1, NULL);
    ftest_append_action(kam12_check_local, 1, NULL);
    return true;
}

FTestActionResult kam01_setup(struct FTestActionArgs* const args)
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
    const PowerKind powers[] = { PwrK_HEALCRTR, PwrK_CALL2ARMS, PwrK_OBEY };
    for (size_t i = 0; i < sizeof(powers) / sizeof(powers[0]); i++) d->magic_level[powers[i]] = 8;
    kfx_config_state.pay_day_progress[P] = 0;
    const struct Thing* heart = find_players_dungeon_heart(P);
    s_hx = heart->mappos.x.stl.num; s_hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(s_hx) - 5, subtile_slab(s_hy) + 3, subtile_slab(s_hx) + 5, subtile_slab(s_hy) + 6, SlbT_CLAIMED, P);
    struct Thing* a = ftest_util_create_creature(subtile_coord_center(s_hx - 1), subtile_coord_center(s_hy + 4), P, 9, (ThingModel)creature_model_id("ORC"));
    struct Thing* h = ftest_util_create_creature(subtile_coord_center(s_hx + 1), subtile_coord_center(s_hy + 4), PLAYER_GOOD, 9, (ThingModel)creature_model_id("BARBARIAN"));
    if (thing_is_invalid(a) || thing_is_invalid(h)) { FTEST_FAIL_TEST("could not create the creatures"); return FTRs_Go_To_Next_Action; }
    orc = a->index; hero = h->index;
    keeper_assist_default_globals(&st()->globals);
    st()->globals.enabled = 1;
    st()->globals.charge_time = 0;
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam02_wait_fight_then_reserve(struct FTestActionArgs* const args)
{
    hold();
    const struct Thing* a = creature(orc);
    if (!a || creature_control_get_from_thing(a)->battle_id == 0) {
        if (phase_over(400)) { FTEST_FAIL_TEST("no fight"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    heal_rule();
    st()->globals.reserve = RESERVE;
    dg()->total_money_owned = heal_price() + RESERVE - 1; // one gold piece short
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam03_check_held_then_exact(struct FTestActionArgs* const args)
{
    hold();
    if (!phase_over(20)) return FTRs_Repeat_Current_Action;
    FTESTLOG("one gold piece short of reserve + price: %" PRId64 " cast(s), refusal %d", casts_since_mark(), (int)st()->run[0].last_refusal);
    CHECK_TRUE("no cast that would cross the reserve", casts_since_mark() == 0);
    CHECK_TRUE("the rule says why", st()->run[0].last_refusal == KAR_Reserve);
    dg()->total_money_owned = heal_price() + RESERVE;
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam04_check_exact_then_budget(struct FTestActionArgs* const args)
{
    hold();
    if (casts_since_mark() == 0) {
        if (!phase_over(20)) return FTRs_Repeat_Current_Action;
        SOFT_FAIL("no cast with exactly reserve + price");
    }
    CHECK_TRUE("the cast left exactly the reserve", dg()->total_money_owned == RESERVE);
    // A budget of one heal per pay period.
    st()->globals.reserve = 0;
    dg()->total_money_owned = 1000000;
    heal_rule()->budget = (int32_t)heal_price();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam05_check_budget_then_payday(struct FTestActionArgs* const args)
{
    hold();
    if (!phase_over(30)) return FTRs_Repeat_Current_Action;
    FTESTLOG("budget of one heal: %" PRId64 " cast(s), refusal %d", casts_since_mark(), (int)st()->run[0].last_refusal);
    CHECK_TRUE("one heal, then the budget is spent", casts_since_mark() == 1 && st()->run[0].last_refusal == KAR_Budget);
    // Pay day in a few turns: a new pay period.
    const int64_t gap = kfx_config_state.conf.rules[P].gameplay.pay_day_gap;
    const int64_t per_turn = kfx_config_state.conf.rules[P].gameplay.pay_day_speed / 100;
    kfx_config_state.pay_day_progress[P] = gap - 2 * per_turn;
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam06_check_payday_then_cta(struct FTestActionArgs* const args)
{
    hold();
    if (casts_since_mark() < 2) {
        if (!phase_over(30)) return FTRs_Repeat_Current_Action;
        SOFT_FAIL("no second heal after pay day");
    }
    CHECK_TRUE("after pay day the budget is back: a second heal", casts_since_mark() == 2);
    // Call to Arms on another keeper's ground, where it is charged for every turn.
    only_rule(KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight)->used = 0;
    const struct Thing* other = find_players_dungeon_heart(my_player_number);
    if (thing_is_invalid(other)) { FTEST_FAIL_TEST("no other heart for the banner"); return FTRs_Go_To_Next_Action; }
    seat_cast_at(PwrK_CALL2ARMS, other->mappos.x.stl.num, other->mappos.y.stl.num + 3);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam07_cta_guard(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("call to arms is on, and charged for", player_uses_power_call_to_arms(P) && player_call_to_arms_is_charged(P));
    struct KeeperAssistRule* r = only_rule(KAA_PowerOff, PwrK_CALL2ARMS, KAT_None);
    cond(r, KAF_Gold, KAO_Lt, 500000);
    cond(r, KAF_CtaPaying, KAO_Eq, 1);
    dg()->total_money_owned = 400000;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam08_check_cta_off_then_free(struct FTestActionArgs* const args)
{
    if (player_uses_power_call_to_arms(P)) {
        if (!phase_over(10)) return FTRs_Repeat_Current_Action;
        SOFT_FAIL("the gold guard did not switch call to arms off");
    }
    FTESTLOG("call to arms off after %" PRId64 " turn(s)", now() - s_phase_start);
    CHECK_TRUE("the guard's action is in the log, free", casts_since_mark() == 1 && st()->log[s_log_mark % KEEPER_ASSIST_LOG].cost == 0);
    // On the keeper's own ground it costs nothing: the same rule leaves it alone.
    seat_cast_at(PwrK_CALL2ARMS, s_hx, s_hy + 4);
    s_phase_start = now();
    s_log_mark = st()->log_total;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam09_check_free_cta_then_obey(struct FTestActionArgs* const args)
{
    if (!phase_over(25)) return FTRs_Repeat_Current_Action;
    FTESTLOG("call to arms on own ground: on %d, charged %d, guard actions %" PRId64, (int)player_uses_power_call_to_arms(P),
             (int)player_call_to_arms_is_charged(P), casts_since_mark());
    CHECK_TRUE("a free call to arms is left on", player_uses_power_call_to_arms(P) && !player_call_to_arms_is_charged(P) && casts_since_mark() == 0);
    turn_off_power_call_to_arms(P);
    // Must Obey under a floor.
    seat_cast_at(PwrK_OBEY, -1, -1);
    struct KeeperAssistRule* r = only_rule(KAA_PowerOff, PwrK_OBEY, KAT_None);
    cond(r, KAF_Gold, KAO_Lt, 500000);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam10_check_obey_then_multiplayer(struct FTestActionArgs* const args)
{
    if (!phase_over(30)) return FTRs_Repeat_Current_Action;
    FTESTLOG("must obey: on %d, guard actions %" PRId64, (int)player_uses_power_obey(P), casts_since_mark());
    CHECK_TRUE("must obey was switched on and the guard switched it off", casts_since_mark() == 1 && !player_uses_power_obey(P));
    // In a multiplayer game the assistant does nothing (the same test as cheats).
    heal_rule();
    dg()->total_money_owned = 1000000;
    kfx_sim_state.game_kind = GKind_MultiGame;
    CHECK_TRUE("the game now refuses cheats", game_refuses_cheats());
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam11_check_multiplayer_then_local(struct FTestActionArgs* const args)
{
    hold();
    if (!phase_over(20)) return FTRs_Repeat_Current_Action;
    kfx_sim_state.game_kind = GKind_LocalGame;
    FTESTLOG("multiplayer: %" PRId64 " cast(s)", casts_since_mark());
    CHECK_TRUE("nothing is cast in a multiplayer game", casts_since_mark() == 0);
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kam12_check_local(struct FTestActionArgs* const args)
{
    hold();
    if (casts_since_mark() == 0) {
        if (!phase_over(20)) return FTRs_Repeat_Current_Action;
        SOFT_FAIL("no cast back in a local game");
    }
    only_rule(KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight)->used = 0;
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " combat assistant money check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: reserve to the gold piece, budget and pay day, call to arms and must obey guards, no casts in multiplayer");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
