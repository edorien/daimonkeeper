// The combat assistant's hand rules, carried out by the External seat (seat_reflexes.h; docs/plans/llm-bridge-realtime.md
// Phase 5), against real fights.
//  1. evacuate_diggers: a hero knight walks up to three of the seat's imps; they are lifted into its sealed safe pocket.
//  2. lift_wounded: a bile demon of the seat at a quarter of its health fights a knight. While a heal rule could heal it,
//     it stays (HEAL_READY); with no heal rule it is lifted out to the seat's lair.
//  3. reinforce: a bile demon losing to a level-10 knight. With only level-1 flies free, nothing is sent (NO_MATCH: no
//     fly is a match for that knight); once two level-9 dragons are free, they are dropped beside the fight -- and no fly.
#include "ftest_ai_seat_reflexes.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "keeper_assist_state.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "player_data.h"
#include "power_hand.h"
#include "seat_reflexes.h"
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
static MapSlabCoord hsx, hsy, pocket_x;
static ThingIndex s_imp[3], s_wounded, s_fighter, s_fly[3], s_dragon[2], s_knight;
static int64_t s_since = 0, s_log_seen = 0;

static struct Thing* make(const char* kind, PlayerNumber owner, MapSlabCoord sx, MapSlabCoord sy, int level)
{
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, sx, sy);
    struct Thing* t = ftest_util_create_creature(pos.x.val, pos.y.val, owner, 1, (ThingModel)creature_model_id(kind));
    if (!thing_is_invalid(t)) set_creature_level(t, (CrtrExpLevel)level);
    return t;
}

static void rule_heal(struct KeeperAssistRule* r)
{
    keeper_assist_default_rule(r, KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight);
    r->cond[0].fact = KAF_HealthPct; r->cond[0].op = KAO_Lt; r->cond[0].value = 50; r->cond_count = 1;
}
static void rule_hand(struct KeeperAssistRule* r, uint8_t action, int64_t dx, int64_t dy)
{
    memset(r, 0, sizeof(*r));
    r->used = 1; r->action = action; r->target = (action == KAA_EvacuateDiggers) ? KAT_AllyAny : KAT_AllyInFight;
    r->count = 4; r->radius = 6; r->min_match = 50; r->cooldown = 20;
    r->dest_x = (uint16_t)dx; r->dest_y = (uint16_t)dy;
}

/** Sends a whole rule set (enabled, every 2 turns) as the agent's set_assist does. */
static void send_rules(const struct KeeperAssistRule* rules, int n)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_SetAssist;
    keeper_assist_default_globals(&v.assist_globals);
    v.assist_globals.enabled = 1; v.assist_globals.charge_time = 0;
    for (int i = 0; i < n; i++) v.assist_rules[i] = rules[i];
    const char* e = extseat_submit_verb_ex(U, P, &v, true, NULL);
    if (e && strcmp(e, "UNCHANGED") != 0) SOFT_FAIL("set_assist refused: %s", e);
}

/** A new firing of the given action since the last look, or NULL. */
static const struct SeatReflexAction* new_firing(uint8_t action)
{
    static struct SeatReflexAction log[SEAT_REFLEX_LOG];
    const int64_t n = seat_reflexes_log(U, log, SEAT_REFLEX_LOG);
    for (int64_t i = 0; i < n; i++) {
        if ((log[i].turn > s_log_seen) && (log[i].action == action)) { s_log_seen = log[i].turn; return &log[i]; }
    }
    return NULL;
}
static TbBool moved(const struct SeatReflexAction* a, ThingIndex id)
{
    for (int i = 0; i < a->count; i++) if (a->things[i] == id) return true;
    return false;
}
static int64_t stl_dist(const struct Thing* t, int64_t x, int64_t y)
{
    const int64_t dx = (int64_t)t->mappos.x.stl.num - x, dy = (int64_t)t->mappos.y.stl.num - y;
    const int64_t ax = (dx < 0) ? -dx : dx, ay = (dy < 0) ? -dy : dy;
    return (ax > ay) ? ax : ay;
}
static TbBool timed_out(int64_t turns) { return ((int64_t)get_gameturn() - s_since) > turns; }

FTestActionResult rx01_setup(struct FTestActionArgs* const args);
FTestActionResult rx02_raid_the_imps(struct FTestActionArgs* const args);
FTestActionResult rx03_imps_lifted(struct FTestActionArgs* const args);
FTestActionResult rx04_imps_in_the_pocket(struct FTestActionArgs* const args);
FTestActionResult rx05_wounded_with_heal(struct FTestActionArgs* const args);
FTestActionResult rx06_heal_ready_holds_it(struct FTestActionArgs* const args);
FTestActionResult rx07_wounded_lifted(struct FTestActionArgs* const args);
FTestActionResult rx08_losing_fight(struct FTestActionArgs* const args);
FTestActionResult rx09_no_fly_sent(struct FTestActionArgs* const args);
FTestActionResult rx10_dragons_sent(struct FTestActionArgs* const args);

void ftest_ai_seat_reflexes_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_reflexes_init()
{
    s_failures = 0; s_log_seen = 0;
    ftest_append_action(rx01_setup, 0, NULL);
    ftest_append_action(rx02_raid_the_imps, 10, NULL);
    ftest_append_action(rx03_imps_lifted, 1, NULL);
    ftest_append_action(rx04_imps_in_the_pocket, 1, NULL);
    ftest_append_action(rx05_wounded_with_heal, 1, NULL);
    ftest_append_action(rx06_heal_ready_holds_it, 1, NULL);
    ftest_append_action(rx07_wounded_lifted, 1, NULL);
    ftest_append_action(rx08_losing_fight, 1, NULL);
    ftest_append_action(rx09_no_fly_sent, 1, NULL);
    ftest_append_action(rx10_dragons_sent, 1, NULL);
    return true;
}

FTestActionResult rx01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if ((P < 0) || ((U = net_add_external_seat(P)) < 1)) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    struct Dungeon* d = get_players_dungeon(get_player(P));
    d->total_money_owned = 100000;
    d->magic_level[PwrK_HEALCRTR] = 8;
    // Claimed ground east of the heart (the fights), a lair south of it, a sealed safe pocket west.
    ftest_util_replace_slabs(hsx + 2, hsy - 3, hsx + 12, hsy + 3, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(hsx - 1, hsy + 3, hsx + 1, hsy + 5, SlbT_LAIR, P);
    pocket_x = (hsx - 12 >= 1) ? (hsx - 10) : (hsx + 16);
    ftest_util_replace_slabs(pocket_x - 2, hsy - 2, pocket_x + 2, hsy + 2, SlbT_EARTH, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(pocket_x - 1, hsy - 1, pocket_x + 1, hsy + 1, SlbT_CLAIMED, P);
    for (int i = 0; i < 3; i++) {
        struct Thing* t = make("IMP", P, hsx + 9, hsy - 1 + i, 0);
        if (thing_is_invalid(t)) { FTEST_FAIL_TEST("no imp"); return FTRs_Go_To_Next_Action; }
        s_imp[i] = t->index;
    }
    struct KeeperAssistRule rules[1];
    rule_hand(&rules[0], KAA_EvacuateDiggers, slab_subtile_center(pocket_x), slab_subtile_center(hsy));
    send_rules(rules, 1);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rx02_raid_the_imps(struct FTestActionArgs* const args)
{
    const struct KeeperAssistState* st = &kfx_sim_state.keeper_assist[P];
    CHECK_TRUE("the evacuate rule is live", st->live[0].used && (st->live[0].action == KAA_EvacuateDiggers) && st->globals.enabled);
    struct Thing* k = make("KNIGHT", PLAYER_GOOD, hsx + 12, hsy, 4);
    if (thing_is_invalid(k)) { FTEST_FAIL_TEST("no knight"); return FTRs_Go_To_Next_Action; }
    s_knight = k->index;
    s_since = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rx03_imps_lifted(struct FTestActionArgs* const args)
{
    const struct SeatReflexAction* a = new_firing(KAA_EvacuateDiggers);
    if (a == NULL) {
        if (timed_out(200)) { SOFT_FAIL("evacuate_diggers never fired"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    FTESTLOG("evacuate_diggers fired at turn %d (%d turns after the knight), %d imp(s), verb #%d, to (%d,%d)",
        (int)a->turn, (int)(a->turn - s_since), (int)a->count, (int)a->verb_id, (int)a->x, (int)a->y);
    CHECK_TRUE("it lifts our imps", moved(a, s_imp[0]) || moved(a, s_imp[1]) || moved(a, s_imp[2]));
    CHECK_TRUE("to the safe pocket", subtile_slab(a->x) >= pocket_x - 1 && subtile_slab(a->x) <= pocket_x + 1);
    s_since = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rx04_imps_in_the_pocket(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U) && !timed_out(120)) return FTRs_Repeat_Current_Action;
    int in = 0;
    for (int i = 0; i < 3; i++) {
        const struct Thing* t = thing_get(s_imp[i]);
        if (!thing_is_invalid(t) && !thing_is_picked_up(t) && (stl_dist(t, slab_subtile_center(pocket_x), slab_subtile_center(hsy)) <= 4)) in++;
    }
    FTESTLOG("imps in the pocket: %d of 3", in);
    CHECK_TRUE("the imps are in the pocket", in >= 2);
    // Done with the knight.
    struct Thing* k = thing_get(s_knight);
    if (!thing_is_invalid(k)) kill_creature(k, INVALID_THING, -1, CrDed_NoEffects | CrDed_NoUnconscious);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rx05_wounded_with_heal(struct FTestActionArgs* const args)
{
    // A heal rule and lift_wounded: the heal can act, so the lift holds back.
    struct KeeperAssistRule rules[2];
    rule_heal(&rules[0]);
    rule_hand(&rules[1], KAA_LiftWounded, 0, 0);
    send_rules(rules, 2);
    struct Thing* b = make("BILE_DEMON", P, hsx + 6, hsy, 2);
    struct Thing* k = make("KNIGHT", PLAYER_GOOD, hsx + 7, hsy, 2);
    if (thing_is_invalid(b) || thing_is_invalid(k)) { FTEST_FAIL_TEST("no fighters"); return FTRs_Go_To_Next_Action; }
    b->health = creature_control_get_from_thing(b)->max_health / 4;
    s_wounded = b->index; s_knight = k->index;
    s_since = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rx06_heal_ready_holds_it(struct FTestActionArgs* const args)
{
    const char* why = seat_reflexes_refusal(U, 1);
    if ((why == NULL) || (strcmp(why, "HEAL_READY") != 0)) {
        if (timed_out(150)) { SOFT_FAIL("lift_wounded never reported HEAL_READY (%s)", why ? why : "nothing"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    CHECK_TRUE("and did not lift it", new_firing(KAA_LiftWounded) == NULL);
    FTESTLOG("lift_wounded held back: %s", why);
    // Now no heal rule: the lift goes ahead.
    struct KeeperAssistRule rules[1];
    rule_hand(&rules[0], KAA_LiftWounded, 0, 0);
    send_rules(rules, 1);
    struct Thing* b = thing_get(s_wounded);
    if (!thing_is_invalid(b)) b->health = creature_control_get_from_thing(b)->max_health / 4; // still hurt
    s_since = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rx07_wounded_lifted(struct FTestActionArgs* const args)
{
    const struct SeatReflexAction* a = new_firing(KAA_LiftWounded);
    if (a == NULL) {
        if (timed_out(200)) { SOFT_FAIL("lift_wounded never fired (%s)", seat_reflexes_refusal(U, 0) ? seat_reflexes_refusal(U, 0) : "nothing"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    FTESTLOG("lift_wounded fired at turn %d: %d creature(s), to room %d", (int)a->turn, (int)a->count, (int)a->x);
    CHECK_TRUE("it lifts the wounded demon", moved(a, s_wounded));
    CHECK_TRUE("into a lair", a->y == -1);
    struct Thing* k = thing_get(s_knight);
    if (!thing_is_invalid(k)) kill_creature(k, INVALID_THING, -1, CrDed_NoEffects | CrDed_NoUnconscious);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rx08_losing_fight(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    struct KeeperAssistRule rules[1];
    rule_hand(&rules[0], KAA_Reinforce, 0, 0);
    send_rules(rules, 1);
    struct Thing* b = make("BILE_DEMON", P, hsx + 6, hsy - 2, 0);
    struct Thing* k = make("KNIGHT", PLAYER_GOOD, hsx + 7, hsy - 2, 9);
    if (thing_is_invalid(b) || thing_is_invalid(k)) { FTEST_FAIL_TEST("no fighters"); return FTRs_Go_To_Next_Action; }
    s_fighter = b->index; s_knight = k->index;
    for (int i = 0; i < 3; i++) {
        struct Thing* f = make("FLY", P, pocket_x - 1 + i, hsy - 1, 0);   // in the safe pocket, far from the fight
        s_fly[i] = thing_is_invalid(f) ? 0 : f->index;
    }
    s_since = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rx09_no_fly_sent(struct FTestActionArgs* const args)
{
    const struct SeatReflexAction* a = new_firing(KAA_Reinforce);
    if (a != NULL) { SOFT_FAIL("reinforce sent %d creature(s) with only level-1 flies free", (int)a->count); }
    const char* why = seat_reflexes_refusal(U, 0);
    if (((why == NULL) || (strcmp(why, "NO_MATCH") != 0)) && !timed_out(150)) return FTRs_Repeat_Current_Action;
    FTESTLOG("reinforce with only flies free: %s", why ? why : "nothing");
    CHECK_TRUE("no fly is a match for a level-10 knight: NO_MATCH", why && strcmp(why, "NO_MATCH") == 0);
    struct Thing* bd = thing_get(s_fighter);
    if (!thing_is_invalid(bd)) bd->health = creature_control_get_from_thing(bd)->max_health; // keep the fight going
    for (int i = 0; i < 2; i++) {
        struct Thing* d = make("DRAGON", P, pocket_x - 1 + 2 * i, hsy + 1, 8);
        s_dragon[i] = thing_is_invalid(d) ? 0 : d->index;
    }
    s_since = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rx10_dragons_sent(struct FTestActionArgs* const args)
{
    const struct SeatReflexAction* a = new_firing(KAA_Reinforce);
    if (a == NULL) {
        if (timed_out(200)) { SOFT_FAIL("reinforce never fired with the dragons free (%s)", seat_reflexes_refusal(U, 0) ? seat_reflexes_refusal(U, 0) : "nothing"); }
        else return FTRs_Repeat_Current_Action;
    } else {
        FTESTLOG("reinforce fired at turn %d: %d creature(s) to (%d,%d)", (int)a->turn, (int)a->count, (int)a->x, (int)a->y);
        CHECK_TRUE("it sends a dragon", moved(a, s_dragon[0]) || moved(a, s_dragon[1]));
        CHECK_TRUE("and no fly", !moved(a, s_fly[0]) && !moved(a, s_fly[1]) && !moved(a, s_fly[2]));
        CHECK_TRUE("beside the fight", (a->x > 0) && (subtile_slab(a->x) >= hsx + 2));
    }
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " reflex check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: the hand rules evacuate imps, lift the wounded only when no heal can, and reinforce only with a match");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
