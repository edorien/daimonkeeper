/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file keeper_assist.c
 *     The combat assistant (keeper_assist.h): each keeper's standing orders for fights, evaluated in the simulation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "keeper_assist.h"

#include <string.h>

#include "bflib_basics.h"
#include "bflib_planar.h"
#include "cheat_mode.h"
#include "config_magic.h"
#include "config_objects.h"
#include "creature_battle.h"
#include "creature_control.h"
#include "creature_states.h"
#include "creature_states_combt.h"
#include "dungeon_data.h"
#include "keeper_assist_state.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "list_walk.h"
#include "magic_powers.h"
#include "map_data.h"
#include "player_availability.h"
#include "player_data.h"
#include "power_hand.h"
#include "power_process.h"
#include "slab_data.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "thing_list.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most creatures looked at in one battle. */
#define BATTLE_MEMBERS_MAX 64

// ---- the decision (no world access but through the callbacks) ---------------------------------------------------------

static TbBool area_power(PowerKind power)
{
    return (power == PwrK_LIGHTNING) || (power == PwrK_CAVEIN);
}

static TbBool rule_uses_fact(const struct KeeperAssistRule *r, enum KeeperAssistFact fact)
{
    for (int i = 0; i < r->cond_count; i++) {
        if (r->cond[i].fact == fact) return true;
    }
    return false;
}

static TbBool conds_hold(const struct KeeperAssistRule *r, const int64_t *dungeon_facts,
    const struct KeeperAssistCandidate *c, int64_t allies_near)
{
    for (int i = 0; i < r->cond_count; i++) {
        const struct KeeperAssistCond *cond = &r->cond[i];
        int64_t value;
        if (keeper_assist_fact_needs_target((enum KeeperAssistFact)cond->fact)) {
            if (c == NULL) return false;
            value = (cond->fact == KAF_AlliesNear) ? allies_near : c->facts[cond->fact];
        } else if ((cond->fact > KAF_None) && (cond->fact < KAF_Count)) {
            value = dungeon_facts[cond->fact];
        } else {
            return false;
        }
        if (!keeper_assist_compare((enum KeeperAssistOp)cond->op, value, cond->value)) return false;
    }
    return true;
}

static enum KeeperAssistPick effective_pick(const struct KeeperAssistRule *r)
{
    if (r->pick != KAP_Default) return (enum KeeperAssistPick)r->pick;
    return (r->target == KAT_EnemyInFight) ? KAP_Strongest : KAP_LowestHealth;
}

/** Whether `a` is a better pick than `b`; a tie goes to the lower thing index, so the choice never depends on order. */
static TbBool better_pick(enum KeeperAssistPick pick, const struct KeeperAssistCandidate *a, const struct KeeperAssistCandidate *b)
{
    int64_t ka = 0, kb = 0, ka2 = 0, kb2 = 0;
    switch (pick) {
    case KAP_LowestHealth: ka = -a->facts[KAF_HealthPct]; kb = -b->facts[KAF_HealthPct]; break;
    case KAP_Strongest:    ka = a->facts[KAF_Force]; kb = b->facts[KAF_Force]; break;
    case KAP_HighestLevel: ka = a->facts[KAF_Level]; kb = b->facts[KAF_Level]; ka2 = a->facts[KAF_Force]; kb2 = b->facts[KAF_Force]; break;
    case KAP_MostValuable: ka = a->score; kb = b->score; break;
    default: break;
    }
    if (ka != kb) return ka > kb;
    if (ka2 != kb2) return ka2 > kb2;
    return a->thing < b->thing;
}

static TbBool recently_targeted(const struct KeeperAssistRuleRun *run, ThingIndex thing, int64_t cooldown, GameTurn now)
{
    if (cooldown <= 0) return false;
    for (int i = 0; i < KEEPER_ASSIST_RECENT_TARGETS; i++) {
        if ((run->recent_target[i] == thing) && (run->recent_target_turn[i] != 0)
         && ((int64_t)(now - run->recent_target_turn[i]) < cooldown)) {
            return true;
        }
    }
    return false;
}

/** Whether the rule would act on this candidate, money aside. */
static TbBool candidate_ok(const struct KeeperAssistRule *r, const struct KeeperAssistRuleRun *run,
    const struct KeeperAssistWorld *world, const int64_t *dungeon_facts, const struct KeeperAssistCandidate *c, GameTurn now)
{
    if ((c->sets & KEEPER_ASSIST_SET(r->target)) == 0) return false;
    if (recently_targeted(run, c->thing, r->target_cooldown, now)) return false;
    int64_t allies_near = 0;
    if (area_power(r->power) || rule_uses_fact(r, KAF_AlliesNear)) {
        allies_near = world->allies_near(world->ctx, r, c);
    }
    if (area_power(r->power) && (allies_near > r->max_allies_hit)) return false;
    return conds_hold(r, dungeon_facts, c, allies_near);
}

/** The price if the keeper may spend it on this rule now, else -1 (and why, in the rule's last_refusal). */
static int64_t affordable_price(struct KeeperAssistState *st, int slot, const struct KeeperAssistWorld *world,
    const int64_t *dungeon_facts)
{
    const struct KeeperAssistRule *r = &st->live[slot];
    struct KeeperAssistRuleRun *run = &st->run[slot];
    if (!world->power_available(world->ctx, r->power)) { run->last_refusal = KAR_NotAvailable; return -1; }
    const int64_t price = world->price(world->ctx, r->power, r->level);
    const int64_t gold = dungeon_facts[KAF_Gold];
    if (price > gold) { run->last_refusal = KAR_CannotAfford; return -1; }
    if (gold - price < st->globals.reserve) { run->last_refusal = KAR_Reserve; return -1; }
    if ((r->budget > 0) && (run->spent_this_payday + price > r->budget)) { run->last_refusal = KAR_Budget; return -1; }
    if ((st->globals.max_spend > 0) && (st->spent_this_payday + price > st->globals.max_spend)) { run->last_refusal = KAR_MaxSpend; return -1; }
    run->last_refusal = KAR_None;
    return price;
}

struct KeeperAssistDecision keeper_assist_decide(struct KeeperAssistState *st, const struct KeeperAssistWorld *world,
    const int64_t *dungeon_facts, const struct KeeperAssistCandidate *cands, int ncands, GameTurn now,
    TbBool casts_allowed)
{
    struct KeeperAssistDecision dec = { -1, -1, 0 };
    const TbBool gap_passed = (st->last_cast_turn == 0) || ((int64_t)(now - st->last_cast_turn) >= st->globals.min_gap);
    for (int slot = 0; slot < KEEPER_ASSIST_RULES; slot++) {
        const struct KeeperAssistRule *r = &st->live[slot];
        struct KeeperAssistRuleRun *run = &st->run[slot];
        if (!r->used) continue;
        if (r->action == KAA_PowerOff) {
            const int64_t active = (r->power == PwrK_CALL2ARMS) ? dungeon_facts[KAF_CtaActive]
                                 : (r->power == PwrK_OBEY) ? dungeon_facts[KAF_ObeyActive] : 0;
            if (!active || !conds_hold(r, dungeon_facts, NULL, 0)) continue;
            dec.slot = slot;
            return dec;
        }
        if ((r->action != KAA_Cast) || !casts_allowed || !gap_passed) continue;
        if ((run->last_cast_turn != 0) && ((int64_t)(now - run->last_cast_turn) < r->cooldown)) continue;
        const enum KeeperAssistPick pick = effective_pick(r);
        int best = -1;
        for (int ci = 0; ci < ncands; ci++) {
            const struct KeeperAssistCandidate *c = &cands[ci];
            if ((best >= 0) && !better_pick(pick, c, &cands[best])) continue;
            if (!candidate_ok(r, run, world, dungeon_facts, c, now)) continue;
            if (!world->can_target(world->ctx, r, c)) continue;
            best = ci;
        }
        if (best < 0) continue;
        const int64_t price = affordable_price(st, slot, world, dungeon_facts);
        if (price < 0) continue;
        dec.slot = slot;
        dec.cand = best;
        dec.price = price;
        return dec;
    }
    return dec;
}

static void log_action(struct KeeperAssistState *st, int slot, ThingIndex target, int64_t cost, GameTurn now)
{
    struct KeeperAssistCast *e = &st->log[st->log_total % KEEPER_ASSIST_LOG];
    memset(e, 0, sizeof(*e));
    e->turn = now;
    e->target = target;
    e->cost = cost;
    e->power = st->live[slot].power;
    e->slot = (uint8_t)slot;
    e->level = st->live[slot].level;
    st->log_total++;
}

void keeper_assist_note_cast(struct KeeperAssistState *st, int slot, ThingIndex target, int64_t cost, GameTurn now)
{
    struct KeeperAssistRuleRun *run = &st->run[slot];
    run->last_cast_turn = now;
    run->fired_total++;
    run->spent_total += cost;
    run->spent_this_payday += cost;
    run->recent_target[run->recent_next] = target;
    run->recent_target_turn[run->recent_next] = now;
    run->recent_next = (uint8_t)((run->recent_next + 1) % KEEPER_ASSIST_RECENT_TARGETS);
    run->last_refusal = KAR_None;
    st->last_cast_turn = now;
    st->spent_this_payday += cost;
    log_action(st, slot, target, cost, now);
}

// ---- the world -------------------------------------------------------------------------------------------------------

struct AssistCtx {
    PlayerNumber plyr_idx;
};

static struct Thing *cand_thing(const struct KeeperAssistCandidate *c)
{
    struct Thing *thing = thing_get(c->thing);
    return (thing_is_invalid(thing) || !thing_is_creature(thing)) ? NULL : thing;
}

static TbBool cb_can_target(void *vctx, const struct KeeperAssistRule *r, const struct KeeperAssistCandidate *c)
{
    const struct AssistCtx *ctx = (const struct AssistCtx *)vctx;
    struct Thing *thing = cand_thing(c);
    if ((thing == NULL) || creature_is_dying(thing)) return false;
    const MapSubtlCoord x = thing->mappos.x.stl.num, y = thing->mappos.y.stl.num;
    if (!can_cast_spell(ctx->plyr_idx, (PowerKind)r->power, x, y, thing, CastChk_Default)) return false;
    // Lightning passes over a creature under Armour (electricity_affecting_area).
    if ((r->power == PwrK_LIGHTNING) && creature_under_spell_effect(thing, CSAfF_Armour)) return false;
    const struct PowerConfigStats *powerst = get_power_model_stats((PowerKind)r->power);
    if (powerst->spell_idx > 0) {
        const uint64_t flags = get_spell_config(powerst->spell_idx)->spell_flags;
        if ((flags != 0) && (creature_under_spell_effect(thing, flags) || creature_is_immune_to_spell_effect(thing, flags))) {
            return false; // already has it, or it would not take
        }
    }
    return true;
}

/** Creatures of `plyr_idx` or of its mutual allies (`own` false: allies only) within `range` (chessboard) of `pos`. */
static int64_t friends_within(PlayerNumber plyr_idx, TbBool own, const struct Coord3d *pos, MapCoordDelta range, TbBool skip_armoured)
{
    int64_t n = 0;
    const struct StructureList *slist = get_list_for_thing_class(TCls_Creature);
    FOR_EACH_THING(thing, thing_walk_list(slist->index, THINGS_COUNT))
    {
        if (thing_is_picked_up(thing) || creature_is_dying(thing)) continue;
        const TbBool mine = (thing->owner == plyr_idx);
        if (mine ? !own : !players_are_mutual_allies(plyr_idx, thing->owner)) continue;
        if (skip_armoured && creature_under_spell_effect(thing, CSAfF_Armour)) continue;
        if (get_chessboard_distance(pos, &thing->mappos) <= range) n++;
    }
    return n;
}

static int64_t cb_allies_near(void *vctx, const struct KeeperAssistRule *r, const struct KeeperAssistCandidate *c)
{
    const struct AssistCtx *ctx = (const struct AssistCtx *)vctx;
    const struct Thing *thing = cand_thing(c);
    if (thing == NULL) return 0;
    const struct PowerConfigStats *powerst = get_power_model_stats((PowerKind)r->power);
    const int64_t level = (r->level < MAGIC_OVERCHARGE_LEVELS) ? r->level : MAGIC_OVERCHARGE_LEVELS - 1;
    if (r->power == PwrK_LIGHTNING) {
        // magic_use_power_lightning: range (strength << 8) / 2; the caster's own creatures are immune, an ally's are not.
        return friends_within(ctx->plyr_idx, false, &thing->mappos, (powerst->strength[level] << 8) / 2, true);
    }
    if (r->power == PwrK_CAVEIN) {
        // A cave-in hurts every creature it falls on, the caster's too, and spreads up to strength slabs from where it
        // was cast (update_cave_in); counted generously, a slab further.
        return friends_within(ctx->plyr_idx, true, &thing->mappos, (powerst->strength[level] + 1) * COORD_PER_STL * STL_PER_SLB, false);
    }
    return 0;
}

static TbBool cb_power_available(void *vctx, PowerKind power)
{
    return is_power_available(((const struct AssistCtx *)vctx)->plyr_idx, power);
}

static int64_t cb_price(void *vctx, PowerKind power, int64_t level)
{
    return compute_power_price(((const struct AssistCtx *)vctx)->plyr_idx, power, (KeepPwrLevel)level);
}

static TbBool creature_can_be_target(const struct Thing *thing)
{
    return !thing_is_picked_up(thing) && !creature_is_dying(thing) && !creature_is_being_unconscious(thing)
        && !creature_is_kept_in_custody(thing);
}

static void fill_target_facts(PlayerNumber plyr_idx, const struct Thing *thing, struct KeeperAssistCandidate *c)
{
    const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
    c->thing = thing->index;
    c->score = get_creature_thing_score(thing);
    c->facts[KAF_HealthPct] = (cctrl->max_health > 0) ? (int64_t)thing->health * 100 / cctrl->max_health : 100;
    c->facts[KAF_Health] = thing->health;
    c->facts[KAF_Level] = (int64_t)cctrl->exp_level + 1;
    c->facts[KAF_Force] = creature_force_score(thing);
    c->facts[KAF_IsDigger] = thing_is_creature_digger(thing);
    const PlayerNumber ground = slabmap_owner(get_slabmap_for_subtile(thing->mappos.x.stl.num, thing->mappos.y.stl.num));
    c->facts[KAF_OnOwnGround] = (ground == plyr_idx) || players_are_mutual_allies(plyr_idx, ground);
    c->facts[KAF_Fleeing] = creature_is_fleeing_combat(thing);
    c->facts[KAF_Kind] = thing->model;
}

static int find_cand(const struct KeeperAssistCandidate *cands, int n, ThingIndex thing)
{
    for (int i = 0; i < n; i++) {
        if (cands[i].thing == thing) return i;
    }
    return -1;
}

/** The keeper's creatures in battles, the enemies in those battles it can see, and (`want_any`) the rest of its own. */
static int build_candidates(PlayerNumber plyr_idx, TbBool want_fights, TbBool want_any, struct KeeperAssistCandidate *cands)
{
    int n = 0;
    if (want_fights) {
        for (BattleIndex b = 1; b < BATTLES_COUNT; b++) {
            const struct CreatureBattle *battle = creature_battle_get(b);
            if ((battle->fighters_num == 0) || !battle_with_creature_of_player(plyr_idx, b)) continue;
            struct Thing *members[BATTLE_MEMBERS_MAX];
            int nm = 0;
            int64_t ally_force = 0, enemy_force = 0;
            ThingIndex i = battle->first_creatr;
            for (int guard = 0; (i != 0) && (guard < CREATURES_COUNT); guard++) {
                struct Thing *thing = thing_get(i);
                if (thing_is_invalid(thing)) break;
                i = creature_control_get_from_thing(thing)->battle_prev_creatr;
                if (!creature_can_be_target(thing)) continue;
                if (thing->owner == plyr_idx) {
                    ally_force += creature_force_score(thing);
                } else if (players_are_enemies(plyr_idx, thing->owner)) {
                    // The keeper sees it as a human would: on its revealed ground, and not concealed.
                    const struct Map *mapblk = get_map_block_at(thing->mappos.x.stl.num, thing->mappos.y.stl.num);
                    if (!map_block_revealed(mapblk, plyr_idx) || creature_is_invisible(thing)) continue;
                    enemy_force += creature_force_score(thing);
                } else {
                    continue;
                }
                if (nm < BATTLE_MEMBERS_MAX) members[nm++] = thing;
            }
            for (int m = 0; (m < nm) && (n < KEEPER_ASSIST_MAX_CANDIDATES); m++) {
                struct KeeperAssistCandidate *c = &cands[n++];
                memset(c, 0, sizeof(*c));
                fill_target_facts(plyr_idx, members[m], c);
                c->sets = (members[m]->owner == plyr_idx)
                    ? (KEEPER_ASSIST_SET(KAT_AllyInFight) | KEEPER_ASSIST_SET(KAT_AllyAny)) : KEEPER_ASSIST_SET(KAT_EnemyInFight);
                c->facts[KAF_BattleAllyForce] = ally_force;
                c->facts[KAF_BattleEnemyForce] = enemy_force;
                c->facts[KAF_ForceRatio] = enemy_force * 100 / ((ally_force > 0) ? ally_force : 1);
                c->facts[KAF_BattleSize] = nm;
            }
        }
    }
    if (want_any) {
        const struct Dungeon *dungeon = get_players_num_dungeon(plyr_idx);
        for (int pass = 0; pass < 2; pass++) {
            ThingIndex i = (pass == 0) ? dungeon->creatr_list_start : dungeon->digger_list_start;
            for (int guard = 0; (i > 0) && (guard < CREATURES_COUNT) && (n < KEEPER_ASSIST_MAX_CANDIDATES); guard++) {
                struct Thing *thing = thing_get(i);
                if (thing_is_invalid(thing)) break;
                i = creature_control_get_from_thing(thing)->players_next_creature_idx;
                if (!creature_can_be_target(thing) || (find_cand(cands, n, thing->index) >= 0)) continue;
                struct KeeperAssistCandidate *c = &cands[n++];
                memset(c, 0, sizeof(*c));
                fill_target_facts(plyr_idx, thing, c);
                c->sets = KEEPER_ASSIST_SET(KAT_AllyAny);
            }
        }
    }
    return n;
}

int keeper_assist_candidates(PlayerNumber plyr_idx, TbBool want_fights, TbBool want_any, struct KeeperAssistCandidate *out)
{
    return build_candidates(plyr_idx, want_fights, want_any, out);
}

static void fill_dungeon_facts(PlayerNumber plyr_idx, int64_t *f)
{
    memset(f, 0, sizeof(int64_t) * KAF_Count);
    const struct Dungeon *dungeon = get_players_num_dungeon(plyr_idx);
    f[KAF_Gold] = dungeon->total_money_owned;
    f[KAF_GoldAfterWages] = get_dungeon_money_less_cost(dungeon);
    const int64_t gap = kfx_config_state.conf.rules[plyr_idx].gameplay.pay_day_gap;
    const int64_t per_turn = kfx_config_state.conf.rules[plyr_idx].gameplay.pay_day_speed / 100;
    const int64_t progress = kfx_config_state.pay_day_progress[plyr_idx];
    f[KAF_PaydayTurns] = ((per_turn > 0) && (gap > progress)) ? (gap - progress + per_turn - 1) / per_turn : 0;
    f[KAF_Fights] = dungeon->fights_num;
    int64_t fighters = 0;
    ThingIndex i = dungeon->creatr_list_start;
    for (int guard = 0; (i > 0) && (guard < CREATURES_COUNT); guard++) {
        const struct Thing *thing = thing_get(i);
        if (thing_is_invalid(thing)) break;
        i = creature_control_get_from_thing(thing)->players_next_creature_idx;
        if (!thing_is_creature_digger(thing)) fighters++;
    }
    f[KAF_Fighters] = fighters;
    f[KAF_CtaActive] = player_uses_power_call_to_arms(plyr_idx);
    f[KAF_CtaPaying] = player_call_to_arms_is_charged(plyr_idx);
    f[KAF_ObeyActive] = player_uses_power_obey(plyr_idx);
    const struct Thing *heart = find_players_dungeon_heart(plyr_idx);
    if (!thing_is_invalid(heart)) {
        const int64_t max = get_object_model_stats(heart->model)->health;
        f[KAF_HeartHealthPct] = (max > 0) ? (int64_t)heart->health * 100 / max : 100;
    }
}

static TbBool cast_now(PlayerNumber plyr_idx, struct KeeperAssistState *st, int slot, struct Thing *thing, GameTurn now)
{
    const struct KeeperAssistRule *r = &st->live[slot];
    struct Dungeon *dungeon = get_players_num_dungeon(plyr_idx);
    const GoldAmount before = dungeon->total_money_owned;
    const MapSubtlCoord x = thing->mappos.x.stl.num, y = thing->mappos.y.stl.num;
    const ThingIndex target = thing->index;
    TbResult res;
    if (area_power(r->power)) {
        res = magic_use_available_power_on_subtile(plyr_idx, (PowerKind)r->power, (KeepPwrLevel)r->level, x, y, PwCast_None, PwMod_Default);
    } else {
        res = magic_use_available_power_on_thing(plyr_idx, (PowerKind)r->power, (KeepPwrLevel)r->level, x, y, thing, PwMod_Default);
    }
    if (res <= Lb_OK) {
        return false;
    }
    const int64_t cost = (before > dungeon->total_money_owned) ? (int64_t)(before - dungeon->total_money_owned) : 0;
    keeper_assist_note_cast(st, slot, target, cost, now);
    SYNCDBG(7, "Player %d cast %s at level %d on thing %d for %d gold (rule %d)", (int)plyr_idx,
        power_code_name((PowerKind)r->power), (int)r->level, (int)target, (int)cost, slot);
    return true;
}

static void power_off(PlayerNumber plyr_idx, struct KeeperAssistState *st, int slot, GameTurn now)
{
    if (st->live[slot].power == PwrK_CALL2ARMS) {
        turn_off_power_call_to_arms(plyr_idx);
    } else if (st->live[slot].power == PwrK_OBEY) {
        turn_off_power_obey(plyr_idx);
    }
    st->run[slot].fired_total++;
    log_action(st, slot, 0, 0, now);
}

static void evaluate_keeper(PlayerNumber plyr_idx, struct KeeperAssistState *st, GameTurn now)
{
    struct AssistCtx ctx = { plyr_idx };
    const struct KeeperAssistWorld world = { &ctx, cb_can_target, cb_allies_near, cb_power_available, cb_price };
    int64_t dungeon_facts[KAF_Count];
    fill_dungeon_facts(plyr_idx, dungeon_facts);
    TbBool want_fights = false, want_any = false;
    for (int s = 0; s < KEEPER_ASSIST_RULES; s++) {
        const struct KeeperAssistRule *r = &st->live[s];
        if (!r->used || (r->action != KAA_Cast)) continue;
        if (r->target == KAT_AllyAny) want_any = true;
        want_fights = true; // in-fight creatures are also AllyAny's, and their battle facts come from the battles
    }
    static struct KeeperAssistCandidate cands[KEEPER_ASSIST_MAX_CANDIDATES];
    const int ncands = (want_fights || want_any) ? build_candidates(plyr_idx, want_fights, want_any, cands) : 0;

    if (st->pending.used && (now >= st->pending.due)) {
        // The charge is done: cast, if the creature is still there, still matches the rule, and it can still be paid.
        const int slot = st->pending.slot;
        st->pending.used = 0;
        const struct KeeperAssistRule *r = &st->live[slot];
        const int ci = find_cand(cands, ncands, st->pending.target);
        struct Thing *thing = thing_get(st->pending.target);
        if (r->used && (r->action == KAA_Cast) && (ci >= 0) && !thing_is_invalid(thing)
         && (thing->creation_turn == st->pending.target_creation_turn)
         && candidate_ok(r, &st->run[slot], &world, dungeon_facts, &cands[ci], now) && world.can_target(world.ctx, r, &cands[ci])
         && (affordable_price(st, slot, &world, dungeon_facts) >= 0)) {
            cast_now(plyr_idx, st, slot, thing, now);
        }
        return;
    }
    const struct KeeperAssistDecision dec = keeper_assist_decide(st, &world, dungeon_facts, cands, ncands, now, !st->pending.used);
    if (dec.slot < 0) return;
    if (dec.cand < 0) {
        power_off(plyr_idx, st, dec.slot, now);
        return;
    }
    struct Thing *thing = thing_get(cands[dec.cand].thing);
    const struct KeeperAssistRule *r = &st->live[dec.slot];
    if (st->globals.charge_time && (r->level > 0)) {
        // A human would now hold the button for the charge; the cast comes when it is done.
        st->pending.used = 1;
        st->pending.slot = (uint8_t)dec.slot;
        st->pending.target = thing->index;
        st->pending.target_creation_turn = thing->creation_turn;
        st->pending.due = now + (GameTurn)(KEEPER_ASSIST_TURNS_PER_LEVEL * r->level);
        return;
    }
    cast_now(plyr_idx, st, dec.slot, thing, now);
}

void keeper_assist_dungeon_facts(PlayerNumber plyr_idx, int64_t *facts)
{
    fill_dungeon_facts(plyr_idx, facts);
}

void keeper_assist_fill_candidate(PlayerNumber plyr_idx, const struct Thing *thing, struct KeeperAssistCandidate *cand)
{
    memset(cand, 0, sizeof(*cand));
    fill_target_facts(plyr_idx, thing, cand);
}

TbBool keeper_assist_conds_hold(const struct KeeperAssistRule *rule, const int64_t *dungeon_facts, const struct KeeperAssistCandidate *cand)
{
    return conds_hold(rule, dungeon_facts, cand, 0);
}

TbBool keeper_assist_heal_ready(PlayerNumber plyr_idx, const struct KeeperAssistState *st)
{
    if (!st->globals.enabled) return false;
    const struct Dungeon *dungeon = get_players_num_dungeon(plyr_idx);
    const int64_t gold = dungeon->total_money_owned;
    for (int s = 0; s < KEEPER_ASSIST_RULES; s++) {
        const struct KeeperAssistRule *r = &st->live[s];
        if (!r->used || (r->action != KAA_Cast) || (r->power != PwrK_HEALCRTR)) continue;
        if ((r->target != KAT_AllyInFight) && (r->target != KAT_AllyAny)) continue;
        if (!is_power_available(plyr_idx, r->power)) continue;
        const int64_t price = compute_power_price(plyr_idx, r->power, r->level);
        if ((price > gold) || (gold - price < st->globals.reserve)) continue;
        if ((r->budget > 0) && (st->run[s].spent_this_payday + price > r->budget)) continue;
        if ((st->globals.max_spend > 0) && (st->spent_this_payday + price > st->globals.max_spend)) continue;
        return true;
    }
    return false;
}

void process_keeper_assists(void)
{
    if (game_refuses_cheats()) {
        return; // never in a multiplayer game (docs/newFeatures/llm/01 section 10.4)
    }
    const GameTurn now = get_gameturn();
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        struct KeeperAssistState *st = &kfx_sim_state.keeper_assist[p];
        if (!st->globals.enabled) continue;
        const struct PlayerInfo *player = get_player(p);
        if (!player_exists(player) || !is_active_keeper(player) || flag_is_set(player->allocflags, PlaF_CompCtrl)) continue;
        // A pay day starts a new pay period for the budgets.
        const int64_t progress = kfx_config_state.pay_day_progress[p];
        if (progress < st->last_payday_progress) {
            st->spent_this_payday = 0;
            for (int s = 0; s < KEEPER_ASSIST_RULES; s++) st->run[s].spent_this_payday = 0;
        }
        st->last_payday_progress = progress;
        const int64_t interval = (st->globals.interval > 0) ? st->globals.interval : 1;
        if ((st->last_eval_turn != 0) && ((int64_t)(now - st->last_eval_turn) < interval)) continue;
        st->last_eval_turn = now;
        evaluate_keeper(p, st, now);
    }
}

#ifdef __cplusplus
}
#endif
