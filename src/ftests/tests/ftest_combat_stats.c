// docs/newFeatures/llm/03 P0: the combat statistics (api_seat_combatstats.c) that the combat assistant's baseline is
// measured with. A real fight between one of an External seat's creatures and a hero, driven through the states the
// statistics care about: the creature falls under 35% health, a decision becomes due, the seat's orders heal it, it
// falls under 20% and dies, and the fight ends a while later. Then Call to Arms on another keeper's ground and Must
// Obey run for a while, and a pay day comes. The observables are what the engine itself did: the gold a cast or an
// upkeep took, the turns things happened on.
#include "ftest_combat_stats.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_combatstats.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "config_magic.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
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

static PlayerNumber P = -1;
static NetUserId U = -1;
static ThingIndex own_crtr = 0, enemy_crtr = 0;
static int64_t s_wait_start = 0;
static int64_t low_turn = 0, critical_turn = 0, death_turn = 0;
static GoldAmount gold_mark = 0;
static int64_t upkeep_mark = 0;
static int64_t s_mark_turn = 0;

static struct Dungeon* dg(void) { return get_players_dungeon(get_player(P)); }
static int64_t now(void) { return (int64_t)get_gameturn(); }
static GoldAmount price(const char* power, int64_t level) { return compute_power_price(P, (PowerKind)power_model_id(power), (KeepPwrLevel)level); }

/** One number out of get_combat_stats, by a dotted path ("fights.open.casts"); -999 when it is not there. */
static int64_t stat(const char* path)
{
    VALUE root;
    if (!api_seat_combatstats_build(&root, P)) return -999;
    VALUE* v = &root;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char* tok = strtok(buf, "."); tok != NULL && v != NULL; tok = strtok(NULL, ".")) v = value_dict_get(v, tok);
    const int64_t out = (v == NULL) ? -999 : ((value_type(v) == VALUE_BOOL) ? (int64_t)value_bool(v) : value_int64(v));
    value_fini(&root);
    return out;
}
/** A field of the latest entry of a `recent` list ("deaths", "window_low"). */
static int64_t latest(const char* section, const char* field)
{
    VALUE root;
    if (!api_seat_combatstats_build(&root, P)) return -999;
    VALUE* sec = value_dict_get(&root, section);
    VALUE* rec = sec ? value_dict_get(sec, "recent") : NULL;
    int64_t out = -999;
    if (rec && value_array_size(rec) > 0) {
        VALUE* e = value_dict_get(value_array_get(rec, value_array_size(rec) - 1), field);
        if (e) out = (value_type(e) == VALUE_BOOL) ? (int64_t)value_bool(e) : value_int64(e);
    }
    value_fini(&root);
    return out;
}

static struct Thing* own(void) { struct Thing* t = thing_get(own_crtr); return (thing_is_invalid(t) || !thing_is_creature(t)) ? NULL : t; }
static struct Thing* enemy(void) { struct Thing* t = thing_get(enemy_crtr); return (thing_is_invalid(t) || !thing_is_creature(t)) ? NULL : t; }
static void set_health_pct(struct Thing* t, int64_t pct)
{
    const struct CreatureControl* cctrl = creature_control_get_from_thing(t);
    t->health = (HitPoints)(cctrl->max_health * pct / 100);
}
static int64_t pct_of(struct Thing* t)
{
    const struct CreatureControl* cctrl = creature_control_get_from_thing(t);
    return (cctrl->max_health > 0) ? (int64_t)t->health * 100 / cctrl->max_health : 100;
}
/** Keeps the fight going and the creature where the test wants it: the hero at full health, ours no higher than `cap`. */
static void hold_fight(int64_t cap)
{
    struct Thing* e = enemy();
    if (e) e->health = creature_control_get_from_thing(e)->max_health;
    struct Thing* a = own();
    if (a && pct_of(a) > cap) set_health_pct(a, cap);
}
static TbBool waited_too_long(int64_t turns, const char* what)
{
    if (s_wait_start == 0) s_wait_start = now();
    if (now() - s_wait_start <= turns) return false;
    SOFT_FAIL("%s did not happen within %" PRId64 " turns", what, turns);
    s_wait_start = 0;
    return true;
}
static FTestActionResult done_waiting(void) { s_wait_start = 0; return FTRs_Go_To_Next_Action; }
/** True once `turns` have passed since s_mark_turn (the ftest action delays only space actions that do not wait). */
static TbBool turns_passed(int64_t turns) { return now() - s_mark_turn >= turns; }
static TbBool submit(struct ExtSeatVerb v, const char* what)
{
    const char* e = extseat_submit_verb_ex(U, P, &v, true, NULL); // queued: behind whatever gesture is still running
    if (e) { SOFT_FAIL("%s: rejected with %s", what, e); return false; }
    // What api.c's submit_action does after a submit the seat accepted (the TCP path is covered by the bridge tests).
    api_seat_combatstats_note_order(P);
    return true;
}
static struct ExtSeatVerb verb_cast(const char* power) { struct ExtSeatVerb v; memset(&v, 0, sizeof(v)); v.kind = ESV_CastPower; snprintf(v.name, sizeof(v.name), "%s", power); return v; }
static struct ExtSeatVerb verb_off(const char* power) { struct ExtSeatVerb v = verb_cast(power); v.kind = ESV_PowerOff; return v; }

FTestActionResult c01_setup(struct FTestActionArgs* const args);
FTestActionResult c02_wait_fight(struct FTestActionArgs* const args);
FTestActionResult c03_low_then_wait_decision(struct FTestActionArgs* const args);
FTestActionResult c04_heal(struct FTestActionArgs* const args);
FTestActionResult c05_check_heal_go_critical(struct FTestActionArgs* const args);
FTestActionResult c06_kill(struct FTestActionArgs* const args);
FTestActionResult c07_check_death_end_fight(struct FTestActionArgs* const args);
FTestActionResult c08_wait_fight_closed(struct FTestActionArgs* const args);
FTestActionResult c09_cta(struct FTestActionArgs* const args);
FTestActionResult c10_cta_mark(struct FTestActionArgs* const args);
FTestActionResult c11_cta_check_obey(struct FTestActionArgs* const args);
FTestActionResult c12_obey_mark(struct FTestActionArgs* const args);
FTestActionResult c13_obey_check_payday(struct FTestActionArgs* const args);
FTestActionResult c14_check_payday(struct FTestActionArgs* const args);

void ftest_combat_stats_pre_start() { fe_computer_players = 1; }

TbBool ftest_combat_stats_init()
{
    s_failures = 0; s_wait_start = 0;
    ftest_append_action(c01_setup, 0, NULL);
    ftest_append_action(c02_wait_fight, 1, NULL);
    ftest_append_action(c03_low_then_wait_decision, 1, NULL);
    ftest_append_action(c04_heal, 1, NULL);
    ftest_append_action(c05_check_heal_go_critical, 1, NULL);
    ftest_append_action(c06_kill, 1, NULL);
    ftest_append_action(c07_check_death_end_fight, 1, NULL);
    ftest_append_action(c08_wait_fight_closed, 1, NULL);
    ftest_append_action(c09_cta, 1, NULL);
    ftest_append_action(c10_cta_mark, 1, NULL);
    ftest_append_action(c11_cta_check_obey, 1, NULL);
    ftest_append_action(c12_obey_mark, 1, NULL);
    ftest_append_action(c13_obey_check_payday, 1, NULL);
    ftest_append_action(c14_check_payday, 1, NULL);
    return true;
}

FTestActionResult c01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0) { FTEST_FAIL_TEST("no second keeper"); return FTRs_Go_To_Next_Action; }
    U = net_add_external_seat(P);
    if (U < 1) { FTEST_FAIL_TEST("net_add_external_seat failed"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    struct Dungeon* d = dg();
    d->total_money_owned = 1000000;
    const char* names[] = { "POWER_HEAL_CREATURE", "POWER_CALL_TO_ARMS", "POWER_OBEY" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) d->magic_level[power_model_id(names[i])] = 8;
    kfx_config_state.pay_day_progress[P] = 0; // pay day far off until the test wants one
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(hx) - 5, subtile_slab(hy) + 3, subtile_slab(hx) + 5, subtile_slab(hy) + 6, SlbT_CLAIMED, P);
    struct Thing* a = ftest_util_create_creature(subtile_coord_center(hx - 1), subtile_coord_center(hy + 4), P, 9, (ThingModel)creature_model_id("ORC"));
    struct Thing* e = ftest_util_create_creature(subtile_coord_center(hx + 1), subtile_coord_center(hy + 4), PLAYER_GOOD, 1, (ThingModel)creature_model_id("BARBARIAN"));
    if (thing_is_invalid(a) || thing_is_invalid(e)) { FTEST_FAIL_TEST("could not create the two creatures"); return FTRs_Go_To_Next_Action; }
    own_crtr = a->index; enemy_crtr = e->index;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c02_wait_fight(struct FTestActionArgs* const args)
{
    hold_fight(100);
    if (stat("fights.open.involved") >= 1) {
        FTESTLOG("fight open at turn %" PRId64 " (started %" PRId64 ")", now(), stat("fights.open.start"));
        CHECK_TRUE("no fight was counted as closed yet", stat("fights.count") == 0);
        return done_waiting();
    }
    if (waited_too_long(400, "a fight between the seat's orc and the hero")) { FTEST_FAIL_TEST("no fight"); return FTRs_Go_To_Next_Action; }
    return FTRs_Repeat_Current_Action;
}

FTestActionResult c03_low_then_wait_decision(struct FTestActionArgs* const args)
{
    struct Thing* a = own();
    if (a == NULL) { FTEST_FAIL_TEST("the orc died too early"); return FTRs_Go_To_Next_Action; }
    if (low_turn == 0) { set_health_pct(a, 30); low_turn = now(); }
    hold_fight(30);
    if (stat("decisions.count") >= 1) {
        FTESTLOG("decision due at turn %" PRId64 " (combat %" PRId64 ")", latest("decisions", "turn"), latest("decisions", "combat"));
        CHECK_TRUE("the decision is counted as the fight's", stat("fights.open.decision_after") >= 0);
        CHECK_TRUE("it is not answered yet", stat("decisions.answered") == 0);
        return done_waiting();
    }
    if (waited_too_long(400, "a decision due")) return FTRs_Go_To_Next_Action;
    return FTRs_Repeat_Current_Action;
}

FTestActionResult c04_heal(struct FTestActionArgs* const args)
{
    struct Thing* a = own();
    if (a == NULL) { FTEST_FAIL_TEST("the orc died too early"); return FTRs_Go_To_Next_Action; }
    set_health_pct(a, 25); // low enough that a heal leaves it under the recovered threshold
    gold_mark = dg()->total_money_owned;
    struct ExtSeatVerb v = verb_cast("POWER_HEAL_CREATURE");
    v.has_thing = true; v.thing_id = own_crtr;
    submit(v, "heal the orc");
    CHECK_TRUE("the order answered the decision", stat("decisions.answered") == 1);
    CHECK_TRUE("the decision's latency is recorded", latest("decisions", "latency") >= 0);
    CHECK_TRUE("the fight's orders_after is recorded", stat("fights.open.orders_after") >= stat("fights.open.decision_after"));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c05_check_heal_go_critical(struct FTestActionArgs* const args)
{
    hold_fight(100);
    if (!extseat_idle(U)) {
        if (waited_too_long(100, "the heal gesture")) return FTRs_Go_To_Next_Action;
        return FTRs_Repeat_Current_Action;
    }
    s_wait_start = 0;
    const GoldAmount spent = gold_mark - dg()->total_money_owned;
    FTESTLOG("heal: spent %" PRId64 ", casts %" PRId64 ", cast gold %" PRId64, (int64_t)spent, stat("casts.count"), stat("casts.gold"));
    CHECK_TRUE("one cast counted", stat("casts.count") == 1);
    CHECK_TRUE("its gold is what the engine charged", stat("casts.gold") == spent && spent == price("POWER_HEAL_CREATURE", 0));
    CHECK_TRUE("it is listed under the heal power", stat("casts.by_power.POWER_HEAL_CREATURE.count") == 1);
    CHECK_TRUE("the fight counts the cast", stat("fights.open.casts") == 1);
    CHECK_TRUE("the fight has its first heal", stat("fights.open.first_heal_after") >= 0);
    struct Thing* a = own();
    if (a == NULL) { FTEST_FAIL_TEST("the orc died too early"); return FTRs_Go_To_Next_Action; }
    FTESTLOG("orc health after the heal: %" PRId64 "%%", pct_of(a));
    CHECK_TRUE("the heal left the orc in its low episode (the test needs that)", pct_of(a) < COMBATSTATS_RECOVERED_PCT);
    set_health_pct(a, 10);
    critical_turn = now();
    s_mark_turn = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c06_kill(struct FTestActionArgs* const args)
{
    hold_fight(10);
    if (!turns_passed(5)) return FTRs_Repeat_Current_Action; // a few turns at 10% before it dies
    struct Thing* a = own();
    struct Thing* e = enemy();
    if (a == NULL) { FTEST_FAIL_TEST("the orc died too early"); return FTRs_Go_To_Next_Action; }
    death_turn = now();
    kill_creature(a, e ? e : INVALID_THING, PLAYER_GOOD, CrDed_Default);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c07_check_death_end_fight(struct FTestActionArgs* const args)
{
    FTESTLOG("death: window_low %" PRId64 " (low since %" PRId64 "), window_critical %" PRId64 ", heal_after_low %" PRId64,
             latest("deaths", "window_low"), low_turn, latest("deaths", "window_critical"), latest("deaths", "heal_after_low"));
    CHECK_TRUE("one combat death", stat("deaths.combat") == 1);
    CHECK_TRUE("not a digger", stat("deaths.combat_diggers") == 0);
    // The episode started the turn after the health was set (the statistics look once per turn).
    const int64_t wl = latest("deaths", "window_low"), wc = latest("deaths", "window_critical");
    CHECK_TRUE("window from under 35% to death", (wl >= death_turn - low_turn - 2) && (wl <= death_turn - low_turn));
    CHECK_TRUE("window from under 20% to death", (wc >= death_turn - critical_turn - 2) && (wc <= death_turn - critical_turn));
    CHECK_TRUE("the heal is recorded against the episode", latest("deaths", "heal_after_low") >= 0);
    CHECK_TRUE("the death counts as seen low and healed", stat("deaths.seen_low") == 1 && stat("deaths.healed") == 1);
    CHECK_TRUE("the fight counts the death", stat("fights.open.deaths") == 1);
    struct Thing* e = enemy();
    if (e) kill_creature(e, INVALID_THING, -1, CrDed_Default);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c08_wait_fight_closed(struct FTestActionArgs* const args)
{
    if (stat("fights.count") >= 1) {
        CHECK_TRUE("the closed fight is the one with the death and the heal", latest("fights", "deaths") == 1 && latest("fights", "casts") == 1);
        CHECK_TRUE("the closed fight lasted from its start to the last fighting turn", latest("fights", "end") >= latest("fights", "start"));
        CHECK_TRUE("no fight open any more", stat("fights.open.start") == -999);
        return done_waiting();
    }
    if (waited_too_long(COMBATSTATS_FIGHT_GAP_TURNS + 50, "the fight closing")) return FTRs_Go_To_Next_Action;
    return FTRs_Repeat_Current_Action;
}

FTestActionResult c09_cta(struct FTestActionArgs* const args)
{
    // Call to Arms on another keeper's ground, where it is paid for every turn.
    const struct Thing* other = find_players_dungeon_heart(my_player_number);
    if (thing_is_invalid(other)) { FTEST_FAIL_TEST("no other heart for the banner"); return FTRs_Go_To_Next_Action; }
    struct ExtSeatVerb v = verb_cast("POWER_CALL_TO_ARMS");
    v.has_pos = true; v.stl_x = other->mappos.x.stl.num; v.stl_y = other->mappos.y.stl.num + 3;
    const struct SlabMap* slb = get_slabmap_for_subtile(v.stl_x, v.stl_y);
    FTESTLOG("banner at (%" PRId64 ",%" PRId64 "), slab owner %" PRId64, v.stl_x, v.stl_y, (int64_t)slabmap_owner(slb));
    submit(v, "call to arms on the other keeper's ground");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c10_cta_mark(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("call to arms is on", player_uses_power_call_to_arms(P));
    gold_mark = dg()->total_money_owned;
    upkeep_mark = stat("upkeep.cta_gold");
    s_mark_turn = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c11_cta_check_obey(struct FTestActionArgs* const args)
{
    if (!turns_passed(60)) return FTRs_Repeat_Current_Action;
    const int64_t spent = gold_mark - dg()->total_money_owned, counted = stat("upkeep.cta_gold") - upkeep_mark;
    const int64_t one = price("POWER_CALL_TO_ARMS", 0);
    FTESTLOG("call to arms over 60 turns: gold %" PRId64 ", counted %" PRId64 " (one charge %" PRId64 ")", spent, counted, one);
    CHECK_TRUE("call to arms cost something", spent > 0);
    CHECK_TRUE("the counted upkeep matches the gold taken (within one charge)", (counted >= spent - one) && (counted <= spent + one));
    CHECK_TRUE("paying turns are counted", stat("upkeep.cta_paying_turns") >= 60);
    submit(verb_off("POWER_CALL_TO_ARMS"), "call to arms off");
    submit(verb_cast("POWER_OBEY"), "must obey on");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c12_obey_mark(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("call to arms is off", !player_uses_power_call_to_arms(P));
    CHECK_TRUE("must obey is on", player_uses_power_obey(P));
    gold_mark = dg()->total_money_owned;
    upkeep_mark = stat("upkeep.obey_gold");
    s_mark_turn = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c13_obey_check_payday(struct FTestActionArgs* const args)
{
    if (!turns_passed(72)) return FTRs_Repeat_Current_Action;
    const int64_t spent = gold_mark - dg()->total_money_owned, counted = stat("upkeep.obey_gold") - upkeep_mark;
    const int64_t one = price("POWER_OBEY", 0);
    FTESTLOG("must obey over 72 turns: gold %" PRId64 ", counted %" PRId64 " (one charge %" PRId64 ")", spent, counted, one);
    CHECK_TRUE("must obey cost something", spent > 0);
    CHECK_TRUE("the counted upkeep matches the gold taken (within one charge)", (counted >= spent - one) && (counted <= spent + one));
    CHECK_TRUE("the casts include call to arms and must obey", stat("casts.by_power.POWER_CALL_TO_ARMS.count") == 1 && stat("casts.by_power.POWER_OBEY.count") >= 1);
    submit(verb_off("POWER_OBEY"), "must obey off");
    // A pay day in a few turns.
    const int64_t gap = kfx_config_state.conf.rules[P].gameplay.pay_day_gap;
    const int64_t per_turn = kfx_config_state.conf.rules[P].gameplay.pay_day_speed / 100;
    kfx_config_state.pay_day_progress[P] = gap - 3 * per_turn;
    gold_mark = dg()->total_money_owned;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult c14_check_payday(struct FTestActionArgs* const args)
{
    if (stat("paydays.count") >= 1) {
        FTESTLOG("pay day at turn %" PRId64 ": gold %" PRId64 ", wages %" PRId64, latest("paydays", "turn"), latest("paydays", "gold"), latest("paydays", "wages"));
        CHECK_TRUE("one pay day", stat("paydays.count") == 1);
        CHECK_TRUE("not short (the seat has a million)", stat("paydays.short") == 0);
        CHECK_TRUE("the gold at pay day is recorded", latest("paydays", "gold") > 0);
        // Everything the seat paid for since the test gave it a million: casts plus upkeep, nothing missed or doubled
        // (switching Must Obey off is a free cast of it, and must not count). The seat has no wages to pay.
        const int64_t engine_spent = 1000000 - (int64_t)latest("paydays", "gold");
        const int64_t counted = stat("casts.gold") + stat("upkeep.cta_gold") + stat("upkeep.obey_gold");
        FTESTLOG("spent by the engine %" PRId64 ", counted %" PRId64, engine_spent, counted);
        CHECK_TRUE("casts and upkeep add up to the gold the engine took", counted == engine_spent);
        CHECK_TRUE("switching must obey off is not a cast", stat("casts.by_power.POWER_OBEY.count") == 1);
        s_wait_start = 0;
        if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " combat statistics check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
        FTESTLOG("Test passed: fight, low-health windows, heal, death, decision latency, upkeep and pay day recorded");
        return FTRs_Go_To_Next_Action;
    }
    if (waited_too_long(60, "a pay day")) { FTEST_FAIL_TEST("%" PRId64 " combat statistics check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    return FTRs_Repeat_Current_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
