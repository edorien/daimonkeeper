// docs/newFeatures/llm/03 P1: the combat assistant casting in a real fight (kfx_ai/keeper_assist.c), with its rules put
// straight into the sim state (the packets that carry them are P2's). An External seat's orc fights a hero beside a
// second, healthy orc. In turn: a heal at a health threshold, charged at the price the engine asks, and only on the
// creature under it; a charged heal that waits the hold time a human needs; a buff not cast again while it lasts; the
// enemies it may target -- not under fog, not while concealed and not attacking, as a human sees them; Chicken only
// while the enemy stands on the keeper's own ground; a Cave-in held back while one of our creatures is within reach.
//
// Found while writing it: the keeper's creatures reveal the ground around them every turn, so an enemy fighting one is
// almost never under fog; and a concealed creature is visible while it attacks (force_visible, set by its attack
// instances) -- to a human as much as to the assistant. Both filters are therefore checked on the candidate list at one
// instant (keeper_assist_candidates), not by waiting for a cast that should not come.
#include "ftest_keeper_assist_casts.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_players.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "keeper_assist.h"
#include "keeper_assist_state.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "magic_powers.h"
#include "map_data.h"
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
static ThingIndex orc_a = 0, orc_b = 0, hero = 0;
static int64_t s_phase_start = 0;
static GameTurn s_due = 0;
static uint64_t s_log_mark = 0;
static GoldAmount s_gold = 0;

static int64_t now(void) { return (int64_t)get_gameturn(); }
static struct KeeperAssistState* st(void) { return &kfx_sim_state.keeper_assist[P]; }
static struct Dungeon* dg(void) { return get_players_dungeon(get_player(P)); }
static GoldAmount price(PowerKind p, int64_t level) { return compute_power_price(P, p, (KeepPwrLevel)level); }
static struct Thing* creature(ThingIndex i) { struct Thing* t = thing_get(i); return (thing_is_invalid(t) || !thing_is_creature(t)) ? NULL : t; }
static void set_pct(ThingIndex i, int64_t pct)
{
    struct Thing* t = creature(i);
    if (t) t->health = (HitPoints)(creature_control_get_from_thing(t)->max_health * pct / 100);
}
/** The hero at full health, orc A held at `pct` (so it neither recovers nor dies), orc B healthy. */
static void hold(int64_t pct)
{
    struct Thing* h = creature(hero);
    if (h) h->health = creature_control_get_from_thing(h)->max_health;
    set_pct(orc_a, pct);
    set_pct(orc_b, 90);
}
/** One rule in slot 0, all other slots empty, counters cleared. */
static struct KeeperAssistRule* only_rule(enum KeeperAssistAction a, PowerKind power, enum KeeperAssistTarget target)
{
    memset(st()->live, 0, sizeof(st()->live));
    memset(st()->run, 0, sizeof(st()->run));
    st()->pending.used = 0;
    st()->last_cast_turn = 0;
    keeper_assist_default_rule(&st()->live[0], a, power, target);
    s_log_mark = st()->log_total;
    s_gold = dg()->total_money_owned;
    s_phase_start = now();
    return &st()->live[0];
}
static void no_rules(void) { only_rule(KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight)->used = 0; }
static void cond(struct KeeperAssistRule* r, enum KeeperAssistFact f, enum KeeperAssistOp op, int32_t v)
{
    r->cond[r->cond_count].fact = (uint8_t)f; r->cond[r->cond_count].op = (uint8_t)op; r->cond[r->cond_count].value = v; r->cond_count++;
}
static int64_t casts_since_mark(void) { return (int64_t)(st()->log_total - s_log_mark); }
static const struct KeeperAssistCast* last_cast(void) { return &st()->log[(st()->log_total - 1) % KEEPER_ASSIST_LOG]; }
static TbBool phase_over(int64_t turns) { return now() - s_phase_start >= turns; }
/** Whether the hero is among the enemies the assistant would consider right now. */
static TbBool hero_is_candidate(void)
{
    static struct KeeperAssistCandidate c[KEEPER_ASSIST_MAX_CANDIDATES];
    const int n = keeper_assist_candidates(P, true, false, c);
    for (int i = 0; i < n; i++) if ((c[i].thing == hero) && (c[i].sets & KEEPER_ASSIST_SET(KAT_EnemyInFight))) return true;
    return false;
}

FTestActionResult kac01_setup(struct FTestActionArgs* const args);
FTestActionResult kac02_wait_fight_then_heal_rule(struct FTestActionArgs* const args);
FTestActionResult kac03_check_heal_then_charged(struct FTestActionArgs* const args);
FTestActionResult kac04_check_charged_then_speed(struct FTestActionArgs* const args);
FTestActionResult kac05_check_speed_then_visibility(struct FTestActionArgs* const args);
FTestActionResult kac06_chicken_off_ground(struct FTestActionArgs* const args);
FTestActionResult kac07_check_no_chicken_then_own_ground(struct FTestActionArgs* const args);
FTestActionResult kac08_check_chicken_then_cavein(struct FTestActionArgs* const args);
FTestActionResult kac09_check_cavein_held_then_allowed(struct FTestActionArgs* const args);
FTestActionResult kac10_check_cavein(struct FTestActionArgs* const args);

void ftest_keeper_assist_casts_pre_start() { fe_computer_players = 1; }

TbBool ftest_keeper_assist_casts_init()
{
    s_failures = 0;
    ftest_append_action(kac01_setup, 0, NULL);
    ftest_append_action(kac02_wait_fight_then_heal_rule, 1, NULL);
    ftest_append_action(kac03_check_heal_then_charged, 1, NULL);
    ftest_append_action(kac04_check_charged_then_speed, 1, NULL);
    ftest_append_action(kac05_check_speed_then_visibility, 1, NULL);
    ftest_append_action(kac06_chicken_off_ground, 1, NULL);
    ftest_append_action(kac07_check_no_chicken_then_own_ground, 1, NULL);
    ftest_append_action(kac08_check_chicken_then_cavein, 1, NULL);
    ftest_append_action(kac09_check_cavein_held_then_allowed, 1, NULL);
    ftest_append_action(kac10_check_cavein, 1, NULL);
    return true;
}

FTestActionResult kac01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    struct Dungeon* d = dg();
    d->total_money_owned = 1000000;
    const PowerKind powers[] = { PwrK_HEALCRTR, PwrK_SPEEDCRTR, PwrK_CAVEIN, PwrK_CHICKEN };
    for (size_t i = 0; i < sizeof(powers) / sizeof(powers[0]); i++) d->magic_level[powers[i]] = 8;
    kfx_config_state.pay_day_progress[P] = 0;
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(hx) - 5, subtile_slab(hy) + 3, subtile_slab(hx) + 5, subtile_slab(hy) + 6, SlbT_CLAIMED, P);
    struct Thing* a = ftest_util_create_creature(subtile_coord_center(hx - 1), subtile_coord_center(hy + 4), P, 9, (ThingModel)creature_model_id("ORC"));
    struct Thing* b = ftest_util_create_creature(subtile_coord_center(hx - 2), subtile_coord_center(hy + 5), P, 9, (ThingModel)creature_model_id("ORC"));
    struct Thing* h = ftest_util_create_creature(subtile_coord_center(hx + 1), subtile_coord_center(hy + 4), PLAYER_GOOD, 9, (ThingModel)creature_model_id("BARBARIAN"));
    if (thing_is_invalid(a) || thing_is_invalid(b) || thing_is_invalid(h)) { FTEST_FAIL_TEST("could not create the creatures"); return FTRs_Go_To_Next_Action; }
    orc_a = a->index; orc_b = b->index; hero = h->index;
    keeper_assist_default_globals(&st()->globals);
    st()->globals.enabled = 1;
    st()->globals.charge_time = 0;
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kac02_wait_fight_then_heal_rule(struct FTestActionArgs* const args)
{
    hold(100);
    const struct Thing* a = creature(orc_a);
    if (!a || creature_control_get_from_thing(a)->battle_id == 0) {
        if (phase_over(400)) { FTEST_FAIL_TEST("no fight"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    struct KeeperAssistRule* r = only_rule(KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight);
    cond(r, KAF_HealthPct, KAO_Lt, 35);
    cond(r, KAF_GoldAfterWages, KAO_Gt, 1000);
    set_pct(orc_a, 30);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kac03_check_heal_then_charged(struct FTestActionArgs* const args)
{
    hold(30);
    if (!phase_over(40)) return FTRs_Repeat_Current_Action; // long enough to see a second heal if one were wrong
    const struct KeeperAssistCast* c = &st()->log[s_log_mark % KEEPER_ASSIST_LOG];
    FTESTLOG("heal: %" PRId64 " cast(s), first on %d, %" PRId64 " turn(s) after the rule, cost %" PRId64,
             casts_since_mark(), (int)c->target, (int64_t)c->turn - s_phase_start, c->cost);
    CHECK_TRUE("one heal: the creature under the threshold, once (target cooldown), the healthy one never", casts_since_mark() == 1);
    CHECK_TRUE("on orc A", c->target == orc_a);
    CHECK_TRUE("within two evaluations", (int64_t)c->turn - s_phase_start <= 2 * KEEPER_ASSIST_DEFAULT_INTERVAL + 1);
    CHECK_TRUE("charged the engine's price at level 0", c->cost == price(PwrK_HEALCRTR, 0) && (s_gold - dg()->total_money_owned) == c->cost);
    CHECK_TRUE("the rule's counters", st()->run[0].fired_total == 1 && st()->run[0].spent_total == c->cost);
    // Charged: level 2, the human hold time first.
    st()->globals.charge_time = 1;
    struct KeeperAssistRule* r = only_rule(KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight);
    r->level = 2;
    cond(r, KAF_HealthPct, KAO_Lt, 35);
    s_due = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kac04_check_charged_then_speed(struct FTestActionArgs* const args)
{
    hold(20);
    if ((s_due == 0) && st()->pending.used) s_due = st()->pending.due;
    if (casts_since_mark() == 0) {
        if (phase_over(100)) { SOFT_FAIL("no charged heal within 100 turns"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    FTESTLOG("charged heal: due at %" PRId64 " (decided %d turns before), cast at %" PRId64 ", cost %" PRId64,
             (int64_t)s_due, KEEPER_ASSIST_TURNS_PER_LEVEL * 2, (int64_t)last_cast()->turn, last_cast()->cost);
    CHECK_TRUE("the decision waited for the charge", s_due > 0);
    CHECK_TRUE("the cast came when the charge was done (within one evaluation)",
               (last_cast()->turn >= s_due) && (last_cast()->turn <= s_due + KEEPER_ASSIST_DEFAULT_INTERVAL));
    CHECK_TRUE("at the level-2 price", last_cast()->cost == price(PwrK_HEALCRTR, 2));
    st()->globals.charge_time = 0;
    struct KeeperAssistRule* r = only_rule(KAA_Cast, PwrK_SPEEDCRTR, KAT_AllyInFight);
    r->target_cooldown = 0; // only the spell's own effect may stop a second cast
    r->cooldown = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kac05_check_speed_then_visibility(struct FTestActionArgs* const args)
{
    hold(60);
    if (!phase_over(80)) return FTRs_Repeat_Current_Action;
    int64_t on_a = 0;
    for (uint64_t i = s_log_mark; i < st()->log_total; i++) if (st()->log[i % KEEPER_ASSIST_LOG].target == orc_a) on_a++;
    FTESTLOG("speed: %" PRId64 " cast(s), %" PRId64 " on orc A", casts_since_mark(), on_a);
    CHECK_TRUE("speed reached orc A once, and not again while it lasts", on_a == 1);
    CHECK_TRUE("orc A is sped up", creature(orc_a) && creature_under_spell_effect(creature(orc_a), CSAfF_Speed));
    no_rules();
    // What the assistant may target, at one instant, as a human would see it.
    struct Thing* h = creature(hero);
    if (!h) { FTEST_FAIL_TEST("the hero is gone"); return FTRs_Go_To_Next_Action; }
    CHECK_TRUE("the hero fighting in view is a candidate", hero_is_candidate());
    conceal_map_area(P, h->mappos.x.stl.num, h->mappos.x.stl.num, h->mappos.y.stl.num, h->mappos.y.stl.num, true);
    CHECK_TRUE("under fog it is not", !hero_is_candidate());
    ftest_util_reveal_map(P);
    CHECK_TRUE("revealed again, it is", hero_is_candidate());
    apply_spell_effect_to_thing(h, get_power_model_stats(PwrK_CONCEAL)->spell_idx, 0, PLAYER_GOOD);
    struct CreatureControl* hc = creature_control_get_from_thing(h);
    hc->force_visible = 0;
    CHECK_TRUE("concealed and not attacking, it is not", !hero_is_candidate());
    hc->force_visible = 5;
    CHECK_TRUE("concealed but attacking (shown, as to a human), it is", hero_is_candidate());
    terminate_thing_spell_effect(h, get_power_model_stats(PwrK_CONCEAL)->spell_idx);
    CHECK_TRUE("Conceal ended", !creature_is_invisible(h));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kac06_chicken_off_ground(struct FTestActionArgs* const args)
{
    // Chicken while the hero stands on ground that is not the keeper's: the engine does not allow it there.
    hold(60);
    const struct Thing* h = creature(hero);
    if (h) ftest_util_replace_slabs(subtile_slab(h->mappos.x.stl.num) - 1, subtile_slab(h->mappos.y.stl.num) - 1,
                                    subtile_slab(h->mappos.x.stl.num) + 1, subtile_slab(h->mappos.y.stl.num) + 1, SlbT_CLAIMED, my_player_number);
    only_rule(KAA_Cast, PwrK_CHICKEN, KAT_EnemyInFight);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kac07_check_no_chicken_then_own_ground(struct FTestActionArgs* const args)
{
    hold(60);
    if (!phase_over(20)) return FTRs_Repeat_Current_Action;
    const struct Thing* h = creature(hero);
    const PlayerNumber ground = h ? (PlayerNumber)slabmap_owner(get_slabmap_for_subtile(h->mappos.x.stl.num, h->mappos.y.stl.num)) : -1;
    FTESTLOG("chicken with the hero on player %d's ground: %" PRId64 " cast(s)", (int)ground, casts_since_mark());
    CHECK_TRUE("the hero stayed off the keeper's ground (the test needs that)", ground != P);
    CHECK_TRUE("no chicken on an enemy off the keeper's ground", casts_since_mark() == 0);
    if (h) ftest_util_replace_slabs(subtile_slab(h->mappos.x.stl.num) - 1, subtile_slab(h->mappos.y.stl.num) - 1,
                                    subtile_slab(h->mappos.x.stl.num) + 1, subtile_slab(h->mappos.y.stl.num) + 1, SlbT_CLAIMED, P);
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kac08_check_chicken_then_cavein(struct FTestActionArgs* const args)
{
    hold(60);
    if (casts_since_mark() == 0) {
        if (!phase_over(20)) return FTRs_Repeat_Current_Action;
        SOFT_FAIL("no chicken once the hero stood on the keeper's ground");
    } else {
        CHECK_TRUE("chicken on the hero, on the keeper's ground", last_cast()->target == hero && last_cast()->cost == price(PwrK_CHICKEN, 0));
        CHECK_TRUE("the hero is a chicken", creature(hero) && creature_under_spell_effect(creature(hero), CSAfF_Chicken));
    }
    // Cave-in falls on our creatures too: with orc A beside the hero, it is held back.
    only_rule(KAA_Cast, PwrK_CAVEIN, KAT_EnemyInFight);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kac09_check_cavein_held_then_allowed(struct FTestActionArgs* const args)
{
    hold(60);
    if (!phase_over(30)) return FTRs_Repeat_Current_Action;
    FTESTLOG("cave-in with orc A beside the hero: %" PRId64 " cast(s)", casts_since_mark());
    CHECK_TRUE("a cave-in that would fall on our creature is held back", casts_since_mark() == 0);
    only_rule(KAA_Cast, PwrK_CAVEIN, KAT_EnemyInFight)->max_allies_hit = 15;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult kac10_check_cavein(struct FTestActionArgs* const args)
{
    hold(60);
    if (casts_since_mark() == 0) {
        if (!phase_over(20)) return FTRs_Repeat_Current_Action;
        SOFT_FAIL("no cave-in once allowed to hit our creatures");
    } else {
        CHECK_TRUE("with max_allies_hit raised, the cave-in falls", last_cast()->target == hero && last_cast()->cost == price(PwrK_CAVEIN, 0));
    }
    no_rules();
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " combat assistant check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: heal at a threshold, charge time, no re-buff, fog and concealment as a human sees them, chicken castability, cave-in guard");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
