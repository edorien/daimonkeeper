#include "pre_inc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api_seat_combatstats.h"
#include "api_seat_decision.h"
#include "bflib_basics.h"
#include "config_creature.h"
#include "config_magic.h"
#include "creature_control.h"
#include "creature_states.h"
#include "dungeon_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "magic_powers.h"
#include "player_data.h"
#include "power_process.h"
#include "slab_data.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "thing_list.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEATH_RING 64
#define FIGHT_RING 32
#define DECISION_RING 128
#define PAYDAY_RING 16
#define POWER_SLOTS 24
#define RECENT_DECISIONS_SHOWN 16
/** A decision due this many turns before a fight was first seen still counts as that fight's (the enemy_fight event
 *  and the creatures' combat state do not appear on exactly the same turn). */
#define DECISION_LEAD_TURNS 20

/** Per creature slot (creatures are synced things). Valid while creation_turn matches the thing's. */
struct CreatureTrack {
    int64_t creation_turn;
    int64_t last_fight_turn;
    int64_t fight_id;       /**< the keeper's fight this creature was last counted in */
    struct CombatLowTrack low;
};

struct DeathRecord {
    int64_t turn;
    int64_t model;
    int64_t level;          /**< 1-based */
    TbBool digger;
    int64_t window_low;     /**< turns from first under COMBATSTATS_LOW_PCT to death; -1 = never seen that low */
    int64_t window_critical;
    int64_t heal_after_low; /**< turns from first under COMBATSTATS_LOW_PCT to the first heal; -1 = no heal */
    int64_t fight_age;      /**< turns since the fight started; -1 = no fight open */
};

struct FightRecord {
    int64_t start, end;
    int64_t involved, deaths, digger_deaths;
    int64_t casts, cast_gold;
    int64_t first_cast_after, first_heal_after; /**< turns after the start; -1 = none */
    int64_t decision_after, orders_after;        /**< the first decision due for it, and the orders that answered it */
};

struct DecisionRecord {
    int64_t turn;
    TbBool combat;          /**< its reasons included a fight, an attack or a breach */
    int64_t latency;        /**< turns until the client's first order after it; -1 = none before the next decision */
};

struct PaydayRecord {
    int64_t turn, gold, wages;
};

struct PowerTally {
    int64_t pwkind, count, gold;
};

struct KeeperCombatStats {
    TbBool tracked;
    int64_t since_turn;
    // the fight now open
    TbBool fighting;
    int64_t fight_id;
    int64_t last_fighting_turn;
    struct FightRecord cur;
    int64_t cur_decision_seq; /**< the decision counted as this fight's (0 = none yet) */
    // totals
    int64_t fights, fight_turns;
    int64_t deaths, combat_deaths, combat_digger_deaths, deaths_seen_low, deaths_healed;
    int64_t casts, cast_gold;
    int64_t cta_gold, cta_paying_turns, obey_gold, obey_turns;
    // the stretch each lasting power is on for, logged when it goes off
    TbBool cta_on, obey_on;
    int64_t cta_on_turn, cta_gold_at_on, obey_on_turn, obey_gold_at_on;
    int64_t paydays, paydays_short;
    int64_t decisions, decisions_answered;
    struct PowerTally powers[POWER_SLOTS];
    // rings (medians are taken over what they hold)
    struct DeathRecord death[DEATH_RING]; int64_t death_total;
    struct FightRecord fight[FIGHT_RING]; int64_t fight_total;
    struct DecisionRecord decision[DECISION_RING]; int64_t decision_total;
    struct PaydayRecord payday[PAYDAY_RING]; int64_t payday_total;
    // decisions
    int64_t last_seq;
    TbBool decision_open;   /**< the latest decision has not been answered with an order yet */
    // pay day
    int64_t last_payday_progress;
};

static struct KeeperCombatStats s_stats[PLAYERS_COUNT];
static struct CreatureTrack s_creature[SYNCED_THINGS_COUNT];
static int64_t s_last_tick_turn = -1;

// ---- pure helpers -----------------------------------------------------------------------------------------------------

void combatstats_update_low(struct CombatLowTrack *t, int64_t health_pct, int64_t turn)
{
    if (health_pct >= COMBATSTATS_RECOVERED_PCT) {
        memset(t, 0, sizeof(*t));
        return;
    }
    if ((health_pct < COMBATSTATS_LOW_PCT) && (t->low_since == 0)) t->low_since = turn;
    if ((health_pct < COMBATSTATS_CRITICAL_PCT) && (t->critical_since == 0)) t->critical_since = turn;
}

static int cmp_i64(const void *a, const void *b)
{
    const int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

int64_t combatstats_median(int64_t *values, int64_t n)
{
    if (n <= 0) return 0;
    qsort(values, (size_t)n, sizeof(int64_t), cmp_i64);
    return values[(n - 1) / 2];
}

TbBool combatstats_upkeep_due(int64_t turns_since_start, int64_t duration)
{
    if (turns_since_start <= 0) return false; // the turn it is cast, the cast's own price is all the engine takes
    if (duration < 1) return true;
    return (turns_since_start % duration) == 0;
}

// ---- helpers over the game ------------------------------------------------------------------------------------------

static TbBool keeper_tracked(PlayerNumber p)
{
    if ((p < 0) || (p >= PLAYERS_COUNT)) return false;
    const struct PlayerInfo *player = get_player(p);
    if (!player_exists(player) || !is_active_keeper(player)) return false;
    if (flag_is_set(player->allocflags, PlaF_CompCtrl)) return false; // the built-in AI plays it
    return !dungeon_invalid(get_players_dungeon(player));
}

static struct CreatureTrack *track_of(const struct Thing *thing)
{
    if (thing_is_invalid(thing) || (thing->index <= 0) || (thing->index >= SYNCED_THINGS_COUNT)) return NULL;
    struct CreatureTrack *t = &s_creature[thing->index];
    if (t->creation_turn != (int64_t)thing->creation_turn) {
        memset(t, 0, sizeof(*t));
        t->creation_turn = (int64_t)thing->creation_turn;
    }
    return t;
}

static int64_t health_pct(const struct Thing *thing)
{
    const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
    if (creature_control_invalid(cctrl) || (cctrl->max_health <= 0)) return 100;
    const int64_t hp = (thing->health < 0) ? 0 : thing->health;
    return hp * 100 / cctrl->max_health;
}

static TbBool creature_fighting(const struct Thing *thing)
{
    const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
    if (creature_control_invalid(cctrl)) return false;
    return (cctrl->battle_id > 0) || ((cctrl->combat_flags & (CmbtF_Melee | CmbtF_Ranged)) != 0);
}

static void tally_power(struct KeeperCombatStats *s, PowerKind pwkind, int64_t gold)
{
    for (int i = 0; i < POWER_SLOTS; i++) {
        if ((s->powers[i].count > 0) && (s->powers[i].pwkind != pwkind)) continue;
        s->powers[i].pwkind = pwkind;
        s->powers[i].count++;
        s->powers[i].gold += gold;
        return;
    }
}

static void open_fight(PlayerNumber p, struct KeeperCombatStats *s, int64_t now)
{
    s->fighting = true;
    s->fight_id++;
    s->last_fighting_turn = now;
    memset(&s->cur, 0, sizeof(s->cur));
    s->cur.start = now;
    s->cur.first_cast_after = s->cur.first_heal_after = -1;
    s->cur.decision_after = s->cur.orders_after = -1;
    s->cur_decision_seq = 0;
    // A decision raised just before the creatures' combat state showed is this fight's.
    if (s->decision_open && (s->decision_total > 0)) {
        const struct DecisionRecord *d = &s->decision[(s->decision_total - 1) % DECISION_RING];
        if (d->turn >= now - DECISION_LEAD_TURNS) {
            s->cur.decision_after = 0;
            s->cur_decision_seq = s->last_seq;
        }
    }
    (void)p;
}

static void close_fight(PlayerNumber p, struct KeeperCombatStats *s)
{
    s->cur.end = s->last_fighting_turn;
    s->fighting = false;
    s->fights++;
    s->fight_turns += s->cur.end - s->cur.start + 1;
    s->fight[s->fight_total % FIGHT_RING] = s->cur;
    s->fight_total++;
    JUSTMSG("COMBATSTAT: fight player=%d start=%" PRId64 " end=%" PRId64 " involved=%" PRId64 " deaths=%" PRId64
            " digger_deaths=%" PRId64 " casts=%" PRId64 " cast_gold=%" PRId64 " first_cast_after=%" PRId64
            " first_heal_after=%" PRId64 " decision_after=%" PRId64 " orders_after=%" PRId64,
            (int)p, s->cur.start, s->cur.end, s->cur.involved, s->cur.deaths, s->cur.digger_deaths, s->cur.casts,
            s->cur.cast_gold, s->cur.first_cast_after, s->cur.first_heal_after, s->cur.decision_after, s->cur.orders_after);
}

static void note_power_stretch(PlayerNumber p, const char *power, TbBool on, TbBool *was_on, int64_t *on_turn,
    int64_t *gold_at_on, int64_t gold_total, int64_t now)
{
    if (on && !*was_on) {
        *on_turn = now;
        *gold_at_on = gold_total;
    } else if (!on && *was_on) {
        JUSTMSG("COMBATSTAT: upkeep player=%d power=%s from=%" PRId64 " to=%" PRId64 " gold=%" PRId64,
                (int)p, power, *on_turn, now, gold_total - *gold_at_on);
    }
    *was_on = on;
}

static void tick_upkeep(PlayerNumber p, struct KeeperCombatStats *s, int64_t now)
{
    const struct Dungeon *dungeon = get_players_num_dungeon(p);
    note_power_stretch(p, "POWER_CALL_TO_ARMS", player_uses_power_call_to_arms(p), &s->cta_on, &s->cta_on_turn,
                       &s->cta_gold_at_on, s->cta_gold, now);
    note_power_stretch(p, "POWER_OBEY", player_uses_power_obey(p), &s->obey_on, &s->obey_on_turn,
                       &s->obey_gold_at_on, s->obey_gold, now);
    if (player_call_to_arms_is_charged(p)) {
        s->cta_paying_turns++;
        const struct PowerConfigStats *powerst = get_power_model_stats(PwrK_CALL2ARMS);
        if (combatstats_upkeep_due(now - (int64_t)dungeon->cta_start_turn, powerst->duration)) {
            s->cta_gold += compute_power_price(p, PwrK_CALL2ARMS, dungeon->cta_power_level);
        }
    }
    if (player_uses_power_obey(p)) {
        s->obey_turns++;
        const struct PowerConfigStats *powerst = get_power_model_stats(PwrK_OBEY);
        if (combatstats_upkeep_due(now - (int64_t)dungeon->must_obey_turn, powerst->duration)) {
            s->obey_gold += compute_power_price(p, PwrK_OBEY, 0);
        }
    }
}

static void tick_payday(PlayerNumber p, struct KeeperCombatStats *s, int64_t now)
{
    const int64_t progress = (int64_t)kfx_config_state.pay_day_progress[p];
    if (progress < s->last_payday_progress) {
        // The progress wrapped: pay day came.
        const struct Dungeon *dungeon = get_players_num_dungeon(p);
        struct PaydayRecord *r = &s->payday[s->payday_total % PAYDAY_RING];
        r->turn = now;
        r->gold = dungeon->total_money_owned;
        r->wages = dungeon->creatures_total_pay;
        s->payday_total++;
        s->paydays++;
        if (r->gold < r->wages) s->paydays_short++;
        JUSTMSG("COMBATSTAT: payday player=%d turn=%" PRId64 " gold=%" PRId64 " wages=%" PRId64, (int)p, now, r->gold, r->wages);
    }
    s->last_payday_progress = progress;
}

static void tick_decisions(PlayerNumber p, struct KeeperCombatStats *s)
{
    int64_t seq = 0, turn = 0;
    TbBool combat = false;
    if (!api_seat_decision_latest(p, &seq, &turn, &combat) || (seq == s->last_seq)) return;
    s->last_seq = seq;
    struct DecisionRecord *d = &s->decision[s->decision_total % DECISION_RING];
    d->turn = turn;
    d->combat = combat;
    d->latency = -1;
    s->decision_total++;
    s->decisions++;
    s->decision_open = true;
    if (s->fighting && (s->cur.decision_after < 0)) {
        s->cur.decision_after = turn - s->cur.start;
        if (s->cur.decision_after < 0) s->cur.decision_after = 0;
        s->cur_decision_seq = seq;
    }
}

static void tick_creatures(PlayerNumber p, struct KeeperCombatStats *s, int64_t now)
{
    const struct Dungeon *dungeon = get_players_num_dungeon(p);
    TbBool any_fighting = false;
    for (int pass = 0; pass < 2; pass++) {
        int64_t idx = (pass == 0) ? dungeon->creatr_list_start : dungeon->digger_list_start;
        int64_t guard = 0;
        while ((idx > 0) && (guard++ < THINGS_COUNT)) {
            const struct Thing *thing = thing_get(idx);
            const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
            if (thing_is_invalid(thing) || creature_control_invalid(cctrl)) break;
            idx = cctrl->players_next_creature_idx;
            struct CreatureTrack *t = track_of(thing);
            if (t == NULL) continue;
            combatstats_update_low(&t->low, health_pct(thing), now);
            if (!creature_fighting(thing)) continue;
            any_fighting = true;
            t->last_fight_turn = now;
            if (!s->fighting) open_fight(p, s, now);
            if (t->fight_id != s->fight_id) {
                t->fight_id = s->fight_id;
                s->cur.involved++;
            }
        }
    }
    if (!s->fighting) return;
    if (any_fighting) s->last_fighting_turn = now;
    else if (now - s->last_fighting_turn > COMBATSTATS_FIGHT_GAP_TURNS) close_fight(p, s);
}

static void start_tracking(PlayerNumber p, struct KeeperCombatStats *s, int64_t now)
{
    memset(s, 0, sizeof(*s));
    s->tracked = true;
    s->since_turn = now;
    s->last_payday_progress = (int64_t)kfx_config_state.pay_day_progress[p];
    int64_t seq = 0, turn = 0;
    TbBool combat = false;
    if (api_seat_decision_latest(p, &seq, &turn, &combat)) s->last_seq = seq; // only decisions from now on
}

void api_seat_combatstats_reset(void)
{
    memset(s_stats, 0, sizeof(s_stats));
    memset(s_creature, 0, sizeof(s_creature));
    s_last_tick_turn = -1;
}

void api_seat_combatstats_tick(void)
{
    if (kfx_sim_state.game_kind != GKind_LocalGame) return;
    const int64_t now = (int64_t)get_gameturn();
    if (now == s_last_tick_turn) return; // called more than once in a turn
    if (now < s_last_tick_turn) api_seat_combatstats_reset(); // a load, or a new level
    s_last_tick_turn = now;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        struct KeeperCombatStats *s = &s_stats[p];
        if (!keeper_tracked(p)) continue; // what was gathered stays readable (a defeated keeper)
        if (!s->tracked) start_tracking(p, s, now);
        tick_creatures(p, s, now);
        tick_upkeep(p, s, now);
        tick_payday(p, s, now);
        tick_decisions(p, s);
    }
}

// ---- port notifications -----------------------------------------------------------------------------------------------

void api_seat_combatstats_note_cast(PlayerNumber plyr_idx, PowerKind pwkind, int64_t level, struct Thing *target)
{
    if (kfx_sim_state.game_kind != GKind_LocalGame) return;
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT) || !s_stats[plyr_idx].tracked) return;
    struct KeeperCombatStats *s = &s_stats[plyr_idx];
    const GoldAmount price = compute_power_price(plyr_idx, pwkind, (KeepPwrLevel)level);
    // Free powers (hand, slap, pick-ups) are not casts in this sense, and a cast the keeper cannot pay for fails.
    if (price <= 0) return;
    if (get_players_num_dungeon(plyr_idx)->total_money_owned < price) return;
    // Must Obey is switched off by casting it again (as the panel does), which costs nothing.
    if ((pwkind == PwrK_OBEY) && player_uses_power_obey(plyr_idx)) return;
    const int64_t now = (int64_t)get_gameturn();
    s->casts++;
    s->cast_gold += price;
    tally_power(s, pwkind, price);
    if (s->fighting) {
        s->cur.casts++;
        s->cur.cast_gold += price;
        if (s->cur.first_cast_after < 0) s->cur.first_cast_after = now - s->cur.start;
    }
    ThingIndex target_idx = 0;
    if (!thing_is_invalid(target) && thing_is_creature(target)) {
        target_idx = target->index;
        if ((pwkind == PwrK_HEALCRTR) && (target->owner == plyr_idx)) {
            struct CreatureTrack *t = track_of(target);
            if ((t != NULL) && (t->low.low_since > 0) && (t->low.healed_turn == 0)) t->low.healed_turn = now;
            if (s->fighting && (s->cur.first_heal_after < 0)) s->cur.first_heal_after = now - s->cur.start;
        }
    }
    JUSTMSG("COMBATSTAT: cast player=%d turn=%" PRId64 " power=%s level=%" PRId64 " target=%d gold=%" PRId64 " in_fight=%d",
            (int)plyr_idx, now, power_code_name(pwkind), level, (int)target_idx, (int64_t)price, (int)s->fighting);
}

void api_seat_combatstats_note_death(struct Thing *thing)
{
    if (kfx_sim_state.game_kind != GKind_LocalGame) return;
    if (thing_is_invalid(thing) || !thing_is_creature(thing)) return;
    const PlayerNumber p = thing->owner;
    if ((p < 0) || (p >= PLAYERS_COUNT) || !s_stats[p].tracked) return;
    struct KeeperCombatStats *s = &s_stats[p];
    struct CreatureTrack *t = track_of(thing);
    if (t == NULL) return;
    const int64_t now = (int64_t)get_gameturn();
    s->deaths++;
    const TbBool combat = creature_fighting(thing)
        || ((t->last_fight_turn > 0) && (now - t->last_fight_turn <= COMBATSTATS_COMBAT_DEATH_TURNS));
    if (!combat) return;
    const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
    struct DeathRecord *d = &s->death[s->death_total % DEATH_RING];
    d->turn = now;
    d->model = thing->model;
    d->level = creature_control_invalid(cctrl) ? 0 : (int64_t)cctrl->exp_level + 1;
    d->digger = thing_is_creature_digger(thing);
    d->window_low = (t->low.low_since > 0) ? now - t->low.low_since : -1;
    d->window_critical = (t->low.critical_since > 0) ? now - t->low.critical_since : -1;
    d->heal_after_low = ((t->low.low_since > 0) && (t->low.healed_turn > 0)) ? t->low.healed_turn - t->low.low_since : -1;
    d->fight_age = s->fighting ? now - s->cur.start : -1;
    s->death_total++;
    s->combat_deaths++;
    if (d->digger) s->combat_digger_deaths++;
    if (d->window_low >= 0) s->deaths_seen_low++;
    if (d->heal_after_low >= 0) s->deaths_healed++;
    if (s->fighting) {
        s->cur.deaths++;
        if (d->digger) s->cur.digger_deaths++;
    }
    JUSTMSG("COMBATSTAT: death player=%d turn=%" PRId64 " kind=%s level=%" PRId64 " digger=%d window_low=%" PRId64
            " window_critical=%" PRId64 " heal_after_low=%" PRId64 " fight_age=%" PRId64,
            (int)p, now, creature_code_name(thing->model), d->level, (int)d->digger, d->window_low, d->window_critical,
            d->heal_after_low, d->fight_age);
}

void api_seat_combatstats_note_order(PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT) || !s_stats[plyr_idx].tracked) return;
    struct KeeperCombatStats *s = &s_stats[plyr_idx];
    if (!s->decision_open || (s->decision_total == 0)) return;
    const int64_t now = (int64_t)get_gameturn();
    struct DecisionRecord *d = &s->decision[(s->decision_total - 1) % DECISION_RING];
    d->latency = now - d->turn;
    s->decisions_answered++;
    s->decision_open = false;
    JUSTMSG("COMBATSTAT: orders player=%d decision_turn=%" PRId64 " latency=%" PRId64 " combat=%d",
            (int)plyr_idx, d->turn, d->latency, (int)d->combat);
    if (s->fighting && (s->cur_decision_seq == s->last_seq) && (s->cur.orders_after < 0)) {
        s->cur.orders_after = now - s->cur.start;
    }
}

// ---- output -----------------------------------------------------------------------------------------------------------

/** A median for the API: -1 when there is nothing to take it over (0 would read as "at once"). */
static int64_t median_or_none(int64_t *values, int64_t n)
{
    return (n > 0) ? combatstats_median(values, n) : -1;
}

static void add_fight(VALUE *e, const struct FightRecord *f)
{
    value_init_dict(e);
    value_init_int64(value_dict_add(e, "start"), f->start);
    value_init_int64(value_dict_add(e, "end"), f->end);
    value_init_int64(value_dict_add(e, "involved"), f->involved);
    value_init_int64(value_dict_add(e, "deaths"), f->deaths);
    value_init_int64(value_dict_add(e, "digger_deaths"), f->digger_deaths);
    value_init_int64(value_dict_add(e, "casts"), f->casts);
    value_init_int64(value_dict_add(e, "cast_gold"), f->cast_gold);
    value_init_int64(value_dict_add(e, "first_cast_after"), f->first_cast_after);
    value_init_int64(value_dict_add(e, "first_heal_after"), f->first_heal_after);
    value_init_int64(value_dict_add(e, "decision_after"), f->decision_after);
    value_init_int64(value_dict_add(e, "orders_after"), f->orders_after);
}

TbBool api_seat_combatstats_build(VALUE *out, PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT) || !s_stats[plyr_idx].tracked) return false;
    struct KeeperCombatStats *s = &s_stats[plyr_idx];
    value_init_dict(out);
    value_init_int32(value_dict_add(out, "player"), plyr_idx);
    value_init_int64(value_dict_add(out, "since_turn"), s->since_turn);
    value_init_int64(value_dict_add(out, "turn"), (int64_t)get_gameturn());
    value_init_int32(value_dict_add(out, "low_pct"), COMBATSTATS_LOW_PCT);
    value_init_int32(value_dict_add(out, "critical_pct"), COMBATSTATS_CRITICAL_PCT);

    VALUE *fights = value_dict_add(out, "fights");
    value_init_dict(fights);
    value_init_int64(value_dict_add(fights, "count"), s->fights);
    value_init_int64(value_dict_add(fights, "turns"), s->fight_turns);
    {
        const int64_t held = (s->fight_total < FIGHT_RING) ? s->fight_total : FIGHT_RING;
        int64_t n_cast = 0, n_dec = 0, n_ord = 0;
        int64_t c[FIGHT_RING], dd[FIGHT_RING], o[FIGHT_RING];
        VALUE *recent = value_dict_add(fights, "recent");
        value_init_array(recent);
        for (int64_t i = s->fight_total - held; i < s->fight_total; i++) {
            const struct FightRecord *f = &s->fight[i % FIGHT_RING];
            add_fight(value_array_append(recent), f);
            if (f->first_cast_after >= 0) c[n_cast++] = f->first_cast_after;
            if (f->decision_after >= 0) dd[n_dec++] = f->decision_after;
            if (f->orders_after >= 0) o[n_ord++] = f->orders_after;
        }
        value_init_int64(value_dict_add(fights, "with_cast"), n_cast);
        value_init_int64(value_dict_add(fights, "median_first_cast_after"), median_or_none(c, n_cast));
        value_init_int64(value_dict_add(fights, "median_decision_after"), median_or_none(dd, n_dec));
        value_init_int64(value_dict_add(fights, "median_orders_after"), median_or_none(o, n_ord));
        if (s->fighting) {
            struct FightRecord open = s->cur;
            open.end = s->last_fighting_turn;
            add_fight(value_dict_add(fights, "open"), &open);
        }
    }

    VALUE *deaths = value_dict_add(out, "deaths");
    value_init_dict(deaths);
    value_init_int64(value_dict_add(deaths, "total"), s->deaths);
    value_init_int64(value_dict_add(deaths, "combat"), s->combat_deaths);
    value_init_int64(value_dict_add(deaths, "combat_diggers"), s->combat_digger_deaths);
    value_init_int64(value_dict_add(deaths, "seen_low"), s->deaths_seen_low);
    value_init_int64(value_dict_add(deaths, "healed"), s->deaths_healed);
    {
        const int64_t held = (s->death_total < DEATH_RING) ? s->death_total : DEATH_RING;
        int64_t wl[DEATH_RING], wc[DEATH_RING];
        int64_t n_wl = 0, n_wc = 0;
        VALUE *recent = value_dict_add(deaths, "recent");
        value_init_array(recent);
        for (int64_t i = s->death_total - held; i < s->death_total; i++) {
            const struct DeathRecord *d = &s->death[i % DEATH_RING];
            VALUE *e = value_array_append(recent);
            value_init_dict(e);
            value_init_int64(value_dict_add(e, "turn"), d->turn);
            value_init_string(value_dict_add(e, "kind"), creature_code_name((ThingModel)d->model));
            value_init_int64(value_dict_add(e, "level"), d->level);
            value_init_bool(value_dict_add(e, "digger"), d->digger);
            value_init_int64(value_dict_add(e, "window_low"), d->window_low);
            value_init_int64(value_dict_add(e, "window_critical"), d->window_critical);
            value_init_int64(value_dict_add(e, "heal_after_low"), d->heal_after_low);
            value_init_int64(value_dict_add(e, "fight_age"), d->fight_age);
            if (d->window_low >= 0) wl[n_wl++] = d->window_low;
            if (d->window_critical >= 0) wc[n_wc++] = d->window_critical;
        }
        value_init_int64(value_dict_add(deaths, "median_window_low"), median_or_none(wl, n_wl));
        value_init_int64(value_dict_add(deaths, "median_window_critical"), median_or_none(wc, n_wc));
    }

    VALUE *casts = value_dict_add(out, "casts");
    value_init_dict(casts);
    value_init_int64(value_dict_add(casts, "count"), s->casts);
    value_init_int64(value_dict_add(casts, "gold"), s->cast_gold);
    VALUE *by = value_dict_add(casts, "by_power");
    value_init_dict(by);
    for (int i = 0; i < POWER_SLOTS; i++) {
        if (s->powers[i].count == 0) break;
        VALUE *e = value_dict_add(by, power_code_name((PowerKind)s->powers[i].pwkind));
        value_init_dict(e);
        value_init_int64(value_dict_add(e, "count"), s->powers[i].count);
        value_init_int64(value_dict_add(e, "gold"), s->powers[i].gold);
    }

    VALUE *upkeep = value_dict_add(out, "upkeep");
    value_init_dict(upkeep);
    value_init_int64(value_dict_add(upkeep, "cta_gold"), s->cta_gold);
    value_init_int64(value_dict_add(upkeep, "cta_paying_turns"), s->cta_paying_turns);
    value_init_int64(value_dict_add(upkeep, "obey_gold"), s->obey_gold);
    value_init_int64(value_dict_add(upkeep, "obey_turns"), s->obey_turns);

    VALUE *paydays = value_dict_add(out, "paydays");
    value_init_dict(paydays);
    value_init_int64(value_dict_add(paydays, "count"), s->paydays);
    value_init_int64(value_dict_add(paydays, "short"), s->paydays_short);
    {
        const int64_t held = (s->payday_total < PAYDAY_RING) ? s->payday_total : PAYDAY_RING;
        VALUE *recent = value_dict_add(paydays, "recent");
        value_init_array(recent);
        for (int64_t i = s->payday_total - held; i < s->payday_total; i++) {
            const struct PaydayRecord *r = &s->payday[i % PAYDAY_RING];
            VALUE *e = value_array_append(recent);
            value_init_dict(e);
            value_init_int64(value_dict_add(e, "turn"), r->turn);
            value_init_int64(value_dict_add(e, "gold"), r->gold);
            value_init_int64(value_dict_add(e, "wages"), r->wages);
        }
    }

    VALUE *decisions = value_dict_add(out, "decisions");
    value_init_dict(decisions);
    value_init_int64(value_dict_add(decisions, "count"), s->decisions);
    value_init_int64(value_dict_add(decisions, "answered"), s->decisions_answered);
    {
        const int64_t held = (s->decision_total < DECISION_RING) ? s->decision_total : DECISION_RING;
        int64_t all[DECISION_RING], cmb[DECISION_RING];
        int64_t n_all = 0, n_cmb = 0;
        for (int64_t i = s->decision_total - held; i < s->decision_total; i++) {
            const struct DecisionRecord *d = &s->decision[i % DECISION_RING];
            if (d->latency < 0) continue;
            all[n_all++] = d->latency;
            if (d->combat) cmb[n_cmb++] = d->latency;
        }
        value_init_int64(value_dict_add(decisions, "median_latency"), median_or_none(all, n_all));
        value_init_int64(value_dict_add(decisions, "combat_answered"), n_cmb);
        value_init_int64(value_dict_add(decisions, "median_latency_combat"), median_or_none(cmb, n_cmb));
        VALUE *recent = value_dict_add(decisions, "recent");
        value_init_array(recent);
        const int64_t shown = (held < RECENT_DECISIONS_SHOWN) ? held : RECENT_DECISIONS_SHOWN;
        for (int64_t i = s->decision_total - shown; i < s->decision_total; i++) {
            const struct DecisionRecord *d = &s->decision[i % DECISION_RING];
            VALUE *e = value_array_append(recent);
            value_init_dict(e);
            value_init_int64(value_dict_add(e, "turn"), d->turn);
            value_init_bool(value_dict_add(e, "combat"), d->combat);
            value_init_int64(value_dict_add(e, "latency"), d->latency);
        }
    }
    return true;
}

#ifdef __cplusplus
}
#endif
