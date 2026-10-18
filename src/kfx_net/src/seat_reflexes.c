/** @file seat_reflexes.c
 *     An External seat's hand reflexes (seat_reflexes.h).
 */
#include "pre_inc.h"
#include "seat_reflexes.h"

#include <string.h>

#include "bflib_basics.h"
#include "cheat_mode.h"
#include "config_terrain.h"
#include "creature_battle.h"
#include "creature_control.h"
#include "creature_states.h"
#include "creature_states_combt.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "keeper_assist.h"
#include "keeper_assist_state.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "map_data.h"
#include "net_game.h"
#include "player_data.h"
#include "power_hand.h"
#include "room_data.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "thing_list.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A firing's verb expires this long after it is queued: a late reflex is worse than none. */
#define REFLEX_MAX_AGE 200
#define MAX_THREATS 64
#define MAX_BATTLE_MEMBERS 64
#define NEAR_DEST 6               /**< subtiles: a creature this near the destination is there already */

struct SeatReflexState {
    int64_t last_eval_turn;
    int64_t last_fire_turn[KEEPER_ASSIST_RULES];
    const char *refusal[KEEPER_ASSIST_RULES];
    int64_t in_flight;            /**< the verb of the last firing, until it is no longer queued or running */
    struct SeatReflexAction log[SEAT_REFLEX_LOG];
    int64_t log_total;
};

static struct SeatReflexState s_reflex[MAX_NET_USERS];

void seat_reflexes_forget(NetUserId user)
{
    if ((user >= 0) && (user < MAX_NET_USERS)) memset(&s_reflex[user], 0, sizeof(s_reflex[user]));
}

void seat_reflexes_reset(void)
{
    memset(s_reflex, 0, sizeof(s_reflex));
}

int64_t seat_reflexes_log(NetUserId user, struct SeatReflexAction *out, int64_t max)
{
    if ((user < 0) || (user >= MAX_NET_USERS)) return 0;
    const struct SeatReflexState *rs = &s_reflex[user];
    const int64_t have = (rs->log_total < SEAT_REFLEX_LOG) ? rs->log_total : SEAT_REFLEX_LOG;
    int64_t n = 0;
    for (int64_t i = rs->log_total - have; (i < rs->log_total) && (n < max); i++) out[n++] = rs->log[i % SEAT_REFLEX_LOG];
    return n;
}

const char *seat_reflexes_refusal(NetUserId user, int slot)
{
    if ((user < 0) || (user >= MAX_NET_USERS) || (slot < 0) || (slot >= KEEPER_ASSIST_RULES)) return NULL;
    return s_reflex[user].refusal[slot];
}

static int64_t dist(int64_t ax, int64_t ay, int64_t bx, int64_t by)
{
    const int64_t dx = (ax > bx) ? ax - bx : bx - ax, dy = (ay > by) ? ay - by : by - ay;
    return (dx > dy) ? dx : dy;
}

/** One of the keeper's creatures the hand could take now. */
static TbBool liftable(const struct Thing *t, PlayerNumber p)
{
    return !thing_is_invalid(t) && thing_is_creature(t) && (t->owner == p) && !thing_is_picked_up(t) && !creature_is_dying(t)
        && !creature_is_being_unconscious(t) && !creature_is_kept_in_custody(t);
}

/** An enemy the keeper sees as a human would: on its revealed ground, not concealed, able to fight. */
static TbBool visible_enemy(const struct Thing *t, PlayerNumber p)
{
    if (thing_is_invalid(t) || !thing_is_creature(t) || !players_are_enemies(p, t->owner)) return false;
    if ((t->owner == kfx_config_state.neutral_player_num) || creature_is_dying(t) || creature_is_being_unconscious(t)
     || creature_is_kept_in_custody(t) || thing_is_picked_up(t) || creature_is_invisible(t)) return false;
    return map_block_revealed(get_map_block_at(t->mappos.x.stl.num, t->mappos.y.stl.num), p);
}

static int64_t health_pct(const struct Thing *t)
{
    const struct CreatureControl *cctrl = creature_control_get_from_thing(t);
    return (cctrl->max_health > 0) ? (int64_t)t->health * 100 / cctrl->max_health : 100;
}

static TbBool has_cond(const struct KeeperAssistRule *r, enum KeeperAssistFact fact)
{
    for (int c = 0; c < r->cond_count; c++) if (r->cond[c].fact == fact) return true;
    return false;
}

static int64_t hand_cap(PlayerNumber p, const struct KeeperAssistRule *r)
{
    int64_t cap = (int64_t)kfx_config_state.conf.rules[p].gameplay.max_things_in_hand;
    if ((cap < 1) || (cap > KEEPER_ASSIST_HAND_MAX)) cap = KEEPER_ASSIST_HAND_MAX;
    return (r->count < cap) ? r->count : cap;
}

struct Pick { ThingIndex id; int64_t key; };

static void sort_picks(struct Pick *a, int64_t n)  // ascending key, stable
{
    for (int64_t i = 1; i < n; i++) {
        const struct Pick v = a[i];
        int64_t j = i - 1;
        while ((j >= 0) && (a[j].key > v.key)) { a[j + 1] = a[j]; j--; }
        a[j + 1] = v;
    }
}

/** Queues the lift: the creatures to (x, y), or into room `room_id` when it is > 0. Returns the verb id, or 0 with *err. */
static int64_t queue_lift(NetUserId user, PlayerNumber p, const struct Pick *picks, int64_t n, int64_t x, int64_t y, int64_t room_id,
    const char **err)
{
    struct ExtSeatVerb v;
    memset(&v, 0, sizeof(v));
    v.kind = ESV_PickUpAndDrop;
    for (int64_t i = 0; i < n; i++) v.thing_ids[v.thing_count++] = picks[i].id;
    if (room_id > 0) { v.has_room = true; v.room_id = room_id; }
    else { v.has_pos = true; v.stl_x = x; v.stl_y = y; }
    v.priority = true;
    v.expires_turn = (int64_t)get_gameturn() + REFLEX_MAX_AGE;
    struct ExtSeatSubmitInfo info;
    memset(&info, 0, sizeof(info));
    *err = extseat_submit_verb_ex(user, p, &v, true, &info);
    return (*err == NULL) ? info.id : 0;
}

static void note_firing(NetUserId user, int slot, const struct KeeperAssistRule *r, const struct Pick *picks, int64_t n,
    int64_t id, int64_t x, int64_t y)
{
    struct SeatReflexState *rs = &s_reflex[user];
    struct SeatReflexAction *a = &rs->log[rs->log_total % SEAT_REFLEX_LOG];
    memset(a, 0, sizeof(*a));
    a->turn = (int64_t)get_gameturn();
    a->slot = (uint8_t)slot;
    a->action = r->action;
    a->count = (uint8_t)n;
    a->verb_id = id;
    a->x = x; a->y = y;
    for (int64_t i = 0; (i < n) && (i < KEEPER_ASSIST_HAND_MAX); i++) a->things[i] = picks[i].id;
    rs->log_total++;
    rs->in_flight = id;
    rs->last_fire_turn[slot] = a->turn;
    rs->refusal[slot] = NULL;
    JUSTMSG("Seat reflex: player %d rule %d %s moves %d creature(s) to (%d,%d), verb #%d", (int)get_net_user_player_number(user),
            slot, keeper_assist_hand_name(r->action), (int)n, (int)x, (int)y, (int)id);
}

/** evacuate_diggers: the diggers near a visible enemy fighter, nearest the danger first. */
static TbBool try_evacuate(NetUserId user, PlayerNumber p, int slot, const struct KeeperAssistRule *r, const int64_t *facts)
{
    int64_t tx[MAX_THREATS], ty[MAX_THREATS], nt = 0;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t guard = 0; (idx > 0) && (guard < THINGS_COUNT) && (nt < MAX_THREATS); guard++) {
        const struct Thing *t = thing_get((ThingIndex)idx);
        if (thing_is_invalid(t)) break;
        idx = t->next_of_class;
        if (!visible_enemy(t, p) || thing_is_creature_digger(t)) continue;
        tx[nt] = t->mappos.x.stl.num; ty[nt] = t->mappos.y.stl.num; nt++;
    }
    if (nt == 0) return false;
    const int64_t reach = 3 * (int64_t)r->radius;
    struct Pick picks[64];
    int64_t n = 0;
    const struct Dungeon *dungeon = get_players_num_dungeon(p);
    ThingIndex i = dungeon->digger_list_start;
    for (int64_t guard = 0; (i > 0) && (guard < CREATURES_COUNT) && (n < 64); guard++) {
        const struct Thing *t = thing_get(i);
        if (thing_is_invalid(t)) break;
        i = creature_control_get_from_thing(t)->players_next_creature_idx;
        if (!liftable(t, p)) continue;
        const int64_t x = t->mappos.x.stl.num, y = t->mappos.y.stl.num;
        if (dist(x, y, r->dest_x, r->dest_y) <= NEAR_DEST) continue; // safe already
        int64_t nearest = -1;
        for (int64_t k = 0; k < nt; k++) {
            const int64_t d = dist(x, y, tx[k], ty[k]);
            if ((nearest < 0) || (d < nearest)) nearest = d;
        }
        if (nearest > reach) continue;
        struct KeeperAssistCandidate c;
        keeper_assist_fill_candidate(p, t, &c);
        if (!keeper_assist_conds_hold(r, facts, &c)) continue;
        picks[n].id = t->index; picks[n].key = nearest; n++;
    }
    if (n == 0) return false;
    sort_picks(picks, n);
    const int64_t take = (n < hand_cap(p, r)) ? n : hand_cap(p, r);
    int64_t x = 0, y = 0;
    if (extseat_find_drop_spot(p, picks[0].id, r->dest_x, r->dest_y, 15, &x, &y) != NULL) { s_reflex[user].refusal[slot] = "NO_DROP_SPOT"; return false; }
    const char *err = NULL;
    const int64_t id = queue_lift(user, p, picks, take, x, y, 0, &err);
    if (id == 0) { s_reflex[user].refusal[slot] = err; return false; }
    note_firing(user, slot, r, picks, take, id, x, y);
    return true;
}

/** The keeper's lair to rest the wounded in (the first it owns), or 0. */
static int64_t own_lair(PlayerNumber p)
{
    for (RoomIndex ri = 1; ri < ROOMS_COUNT; ri++) {
        const struct Room *room = room_get(ri);
        if (room_is_invalid(room) || (room->owner != p)) continue;
        if (room_role_matches(room->kind, RoRoF_LairStorage)) return ri;
    }
    return 0;
}

/** lift_wounded: fighters in a fight under the health threshold, the most hurt first -- when no heal rule can heal them. */
static TbBool try_lift_wounded(NetUserId user, PlayerNumber p, int slot, const struct KeeperAssistRule *r, const int64_t *facts)
{
    static struct KeeperAssistCandidate cands[KEEPER_ASSIST_MAX_CANDIDATES];
    const int nc = keeper_assist_candidates(p, true, false, cands);
    struct Pick picks[64];
    int64_t n = 0;
    const TbBool own_threshold = has_cond(r, KAF_HealthPct);
    for (int ci = 0; (ci < nc) && (n < 64); ci++) {
        const struct KeeperAssistCandidate *c = &cands[ci];
        if ((c->sets & KEEPER_ASSIST_SET(KAT_AllyInFight)) == 0) continue;
        const struct Thing *t = thing_get(c->thing);
        if (!liftable(t, p) || thing_is_creature_digger(t)) continue;
        if (!own_threshold && (c->facts[KAF_HealthPct] >= KEEPER_ASSIST_DEFAULT_WOUNDED_PCT)) continue;
        if (!keeper_assist_conds_hold(r, facts, c)) continue;
        picks[n].id = c->thing; picks[n].key = c->facts[KAF_HealthPct]; n++;
    }
    if (n == 0) return false;
    if (keeper_assist_heal_ready(p, &kfx_sim_state.keeper_assist[p])) { s_reflex[user].refusal[slot] = "HEAL_READY"; return false; }
    sort_picks(picks, n);
    const int64_t take = (n < hand_cap(p, r)) ? n : hand_cap(p, r);
    int64_t x = 0, y = -1, room = 0;
    if ((r->dest_x != 0) || (r->dest_y != 0)) {
        if (extseat_find_drop_spot(p, picks[0].id, r->dest_x, r->dest_y, 15, &x, &y) != NULL) { s_reflex[user].refusal[slot] = "NO_DROP_SPOT"; return false; }
    } else {
        room = own_lair(p);
        if (room == 0) { s_reflex[user].refusal[slot] = "NO_LAIR"; return false; }
        x = room;
    }
    const char *err = NULL;
    const int64_t id = queue_lift(user, p, picks, take, x, y, room, &err);
    if (id == 0) { s_reflex[user].refusal[slot] = err; return false; }
    note_firing(user, slot, r, picks, take, id, x, y);
    return true;
}

/** reinforce: the fight the keeper is doing worst in that its healthy, matching fighters from elsewhere could turn. */
static TbBool try_reinforce(NetUserId user, PlayerNumber p, int slot, const struct KeeperAssistRule *r, const int64_t *facts)
{
    const char *why = NULL;
    do {
        int64_t best_ratio = -1, best_x = 0, best_y = 0, best_ally = 0, best_enemy = 0, best_max_enemy = 0;
        for (BattleIndex b = 1; b < BATTLES_COUNT; b++) {
            const struct CreatureBattle *battle = creature_battle_get(b);
            if ((battle->fighters_num == 0) || !battle_with_creature_of_player(p, b)) continue;
            int64_t ally = 0, enemy = 0, max_enemy = 0, sx = 0, sy = 0, nown = 0, members = 0;
            const struct Thing *strongest_own = NULL;
            ThingIndex i = battle->first_creatr;
            for (int64_t guard = 0; (i != 0) && (guard < CREATURES_COUNT) && (members < MAX_BATTLE_MEMBERS); guard++) {
                const struct Thing *t = thing_get(i);
                if (thing_is_invalid(t)) break;
                i = creature_control_get_from_thing(t)->battle_prev_creatr;
                if (t->owner == p) {
                    if (creature_is_dying(t) || creature_is_being_unconscious(t)) continue;
                    const int64_t f = creature_force_score(t);
                    ally += f;
                    sx += t->mappos.x.stl.num; sy += t->mappos.y.stl.num; nown++;
                    if ((strongest_own == NULL) || (f > creature_force_score(strongest_own))) strongest_own = t;
                    members++;
                } else if (visible_enemy(t, p)) {
                    const int64_t f = creature_force_score(t);
                    enemy += f;
                    if (f > max_enemy) max_enemy = f;
                    members++;
                }
            }
            if ((nown == 0) || (enemy == 0)) continue;
            const int64_t ratio = enemy * 100 / ((ally > 0) ? ally : 1);
            // The rule's conditions read the fight as one of the keeper's creatures in it sees it.
            struct KeeperAssistCandidate c;
            keeper_assist_fill_candidate(p, strongest_own, &c);
            c.facts[KAF_BattleAllyForce] = ally;
            c.facts[KAF_BattleEnemyForce] = enemy;
            c.facts[KAF_ForceRatio] = ratio;
            c.facts[KAF_BattleSize] = members;
            if (!has_cond(r, KAF_ForceRatio) && (ratio < KEEPER_ASSIST_DEFAULT_REINFORCE_RATIO)) continue;
            if (!keeper_assist_conds_hold(r, facts, &c)) continue;
            if (ratio > best_ratio) {
                best_ratio = ratio; best_x = sx / nown; best_y = sy / nown;
                best_ally = ally; best_enemy = enemy; best_max_enemy = max_enemy;
            }
        }
        if (best_ratio < 0) return false;
        // Who could go: healthy fighters out of every fight, away from this one, each a match for its strongest enemy.
        const int64_t floor = best_max_enemy * (int64_t)r->min_match / 100;
        const int64_t reach = 3 * (int64_t)r->radius;
        struct Pick picks[64];
        int64_t n = 0;
        const struct Dungeon *dungeon = get_players_num_dungeon(p);
        ThingIndex i = dungeon->creatr_list_start;
        for (int64_t guard = 0; (i > 0) && (guard < CREATURES_COUNT) && (n < 64); guard++) {
            const struct Thing *t = thing_get(i);
            if (thing_is_invalid(t)) break;
            const struct CreatureControl *cctrl = creature_control_get_from_thing(t);
            i = cctrl->players_next_creature_idx;
            if (!liftable(t, p) || thing_is_creature_digger(t) || (cctrl->battle_id != 0)) continue;
            if ((health_pct(t) < 60) || (dist(t->mappos.x.stl.num, t->mappos.y.stl.num, best_x, best_y) <= reach)) continue;
            const int64_t f = creature_force_score(t);
            if (f < floor) continue;
            picks[n].id = t->index; picks[n].key = -f; n++;   // strongest first
        }
        if (n == 0) { why = "NO_MATCH"; break; }
        sort_picks(picks, n);
        // Only enough to turn it (a tenth over the enemy's force), and none if even all it may send would not.
        const int64_t cap = hand_cap(p, r);
        int64_t take = 0, sum = best_ally;
        while ((take < n) && (take < cap) && (sum * 100 < best_enemy * 110)) { sum += -picks[take].key; take++; }
        if (sum < best_enemy) { why = "WOULD_NOT_TURN_IT"; break; }
        if (take == 0) return false;
        int64_t x = 0, y = 0;
        if (extseat_find_drop_spot(p, picks[0].id, best_x, best_y, 9, &x, &y) != NULL) { why = "NO_DROP_SPOT"; break; }
        const char *err = NULL;
        const int64_t id = queue_lift(user, p, picks, take, x, y, 0, &err);
        if (id == 0) { why = err; break; }
        note_firing(user, slot, r, picks, take, id, x, y);
        return true;
    } while (0);
    s_reflex[user].refusal[slot] = why;
    return false;
}

static TbBool verb_still_queued(NetUserId user, int64_t id)
{
    if (id == 0) return false;
    struct ExtSeatQueueInfo qi;
    extseat_queue_info(user, &qi);
    if (qi.running_id == id) return true;
    for (int l = 0; l < EXTSEAT_LANES; l++)
        for (int64_t k = 0; k < qi.lane_count[l]; k++)
            if (qi.lane_ids[l][k] == id) return true;
    return false;
}

void seat_reflexes_tick(NetUserId user, PlayerNumber plyr_idx)
{
    if ((user < 0) || (user >= MAX_NET_USERS) || (plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT)) return;
    if (game_refuses_cheats()) return; // never in a multiplayer game, like the rest of the assistant
    const struct KeeperAssistState *st = &kfx_sim_state.keeper_assist[plyr_idx];
    if (!st->globals.enabled) return;
    struct SeatReflexState *rs = &s_reflex[user];
    const int64_t now = (int64_t)get_gameturn();
    if (now < rs->last_eval_turn) memset(rs, 0, sizeof(*rs)); // a load: the turn went back
    const int64_t interval = (st->globals.interval > 0) ? st->globals.interval : 1;
    if ((rs->last_eval_turn != 0) && (now - rs->last_eval_turn < interval)) return;
    TbBool any = false;
    for (int s = 0; s < KEEPER_ASSIST_RULES; s++) if (st->live[s].used && keeper_assist_action_is_hand(st->live[s].action)) any = true;
    if (!any) return;
    rs->last_eval_turn = now;
    if (verb_still_queued(user, rs->in_flight)) return; // the hand is on the last trip
    rs->in_flight = 0;
    int64_t facts[KAF_Count];
    keeper_assist_dungeon_facts(plyr_idx, facts);
    for (int s = 0; s < KEEPER_ASSIST_RULES; s++) {
        const struct KeeperAssistRule *r = &st->live[s];
        if (!r->used || !keeper_assist_action_is_hand(r->action)) continue;
        if ((rs->last_fire_turn[s] != 0) && (now - rs->last_fire_turn[s] < r->cooldown)) continue;
        TbBool fired = false;
        switch (r->action) {
        case KAA_EvacuateDiggers: fired = try_evacuate(user, plyr_idx, s, r, facts); break;
        case KAA_LiftWounded: fired = try_lift_wounded(user, plyr_idx, s, r, facts); break;
        case KAA_Reinforce: fired = try_reinforce(user, plyr_idx, s, r, facts); break;
        default: break;
        }
        if (fired) return; // one trip at a time
    }
}

#ifdef __cplusplus
}
#endif
