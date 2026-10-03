#include "pre_inc.h"
#include "external_seat.h"

#include <string.h>
#include <strings.h>

#include "bflib_basics.h"
#include "bflib_datetm.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_players.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "creature_control.h"
#include "creature_states.h"
#include "creature_states_combt.h"
#include "dungeon_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "packet_data.h"
#include "player_data.h"
#include "player_instances.h"
#include "power_hand.h"
#include "map_columns.h"
#include "room_data.h"
#include "thing_objects.h"
#include "slab_data.h"
#include "thing_data.h"
#include "thing_list.h"
#include "config_keeperfx.h"
#include "thing_doors.h"
#include "power_specials.h"
#include "power_process.h"
#include "thing_traps.h"
#include "thing_doors.h"
#include "thing_navigate.h"
#include "player_availability.h"
#include "thing_stats.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Slaps are ignored for this many turns after a creature was dropped (magic_use_power_slap_thing). */
#define SLAP_AFTER_DROP_TURNS 10

struct OrderedCreature {
    ThingIndex idx;
    GameTurn creation_turn;
    GameTurn ordered_turn;
    GameTurn hold_until;
};

struct ExtSeatQueue {
    struct ExtSeatStep steps[EXTSEAT_MAX_STEPS];
    int64_t count;
    int64_t next;
    TbBool button_down; /**< a written step pressed the button and none has released it yet */
    int64_t cur_id;     /**< id of the verb whose steps are in `steps` (0 = none, e.g. raw steps) */
    struct ExtSeatVerb cur_verb; /**< that verb, while cur_id != 0 */
    const char *cur_error; /**< why that verb failed part-way (a step's precondition), recorded as its result; NULL = none */
    int64_t step_wait;     /**< turns the next step has been held back waiting for its precondition */
    int64_t last_chat_turn; /**< send_message: when the seat last spoke (0 = never) */
    uint64_t hand_dropped; /**< pick_up_and_drop: which of the running verb's things (by index in its list) were dropped */
    char cur_error_buf[28];
    struct ExtSeatVerb fifo[EXTSEAT_MAX_QUEUED_VERBS];
    int64_t fifo_id[EXTSEAT_MAX_QUEUED_VERBS];
    int64_t fifo_head, fifo_count;
    int64_t next_id;
    struct ExtSeatResult results[EXTSEAT_RESULT_RING];
    int64_t results_total;
    struct OrderedCreature ordered[EXTSEAT_MAX_ORDERED];
    int64_t ordered_count;
    struct ExtSeatAutoRelease auto_rel[EXTSEAT_AUTO_RELEASE_RING];
    int64_t auto_total;
};

static struct ExtSeatQueue s_queue[MAX_NET_USERS];
static TbBool s_agent_owns_pause = false;
static TbBool s_advancing = false;
static GameTurn s_advance_target = 0;
static int64_t s_pending_advance = 0; /**< turns requested; started by the next extseat_tick() */
static TbClockMSec s_last_activity = 0;
static int64_t s_watchdog_ms = EXTSEAT_DEFAULT_WATCHDOG_MS;
static TbBool s_takeover = false;

static TbBool user_ok(NetUserId user)
{
    return (user > SOLO_HUMAN_ID) && (user < MAX_NET_USERS);
}

static void promote_verbs(NetUserId user);

static TbBool steps_done(NetUserId user)
{
    return user_ok(user) && (s_queue[user].next >= s_queue[user].count);
}

TbBool extseat_idle(NetUserId user)
{
    return steps_done(user) && (s_queue[user].fifo_count == 0);
}

int64_t extseat_queued_verbs(NetUserId user)
{
    return user_ok(user) ? s_queue[user].fifo_count : 0;
}

static void record_result(NetUserId user, int64_t id, const char *error)
{
    struct ExtSeatQueue *q = &s_queue[user];
    struct ExtSeatResult *r = &q->results[q->results_total % EXTSEAT_RESULT_RING];
    memset(r, 0, sizeof(*r));
    r->id = id;
    r->rejected = (error != NULL);
    if (error != NULL) {
        snprintf(r->error, sizeof(r->error), "%s", error);
    }
    r->turn = (int64_t)get_gameturn();
    q->results_total++;
}

// ---- ordered creatures: the safety net ---------------------------------------------------------------------------------

static int64_t payday_gap(PlayerNumber plyr_idx)
{
    return (int64_t)kfx_config_state.conf.rules[plyr_idx].gameplay.pay_day_gap;
}

static int64_t turns_to_payday(PlayerNumber plyr_idx)
{
    const int64_t gap = payday_gap(plyr_idx);
    const int64_t per_turn = (int64_t)kfx_config_state.conf.rules[plyr_idx].gameplay.pay_day_speed / 100;
    const int64_t progress = (int64_t)kfx_config_state.pay_day_progress[plyr_idx];
    if ((per_turn <= 0) || (gap <= progress)) {
        return 0;
    }
    return (gap - progress + per_turn - 1) / per_turn;
}

static int64_t default_hold_turns(PlayerNumber plyr_idx)
{
    const int64_t q = payday_gap(plyr_idx) / 4;
    return (q > EXTSEAT_ORDER_MIN_HOLD_TURNS) ? q : EXTSEAT_ORDER_MIN_HOLD_TURNS;
}

static int64_t max_hold_turns(PlayerNumber plyr_idx)
{
    const int64_t h = payday_gap(plyr_idx) / 2;
    return (h > EXTSEAT_ORDER_MIN_HOLD_TURNS) ? h : EXTSEAT_ORDER_MIN_HOLD_TURNS;
}

static TbBool creature_near_hunger_limit(const struct Thing *thing)
{
    const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
    const struct CreatureModelConfig *crconf = creature_stats_get_from_thing(thing);
    if (creature_control_invalid(cctrl) || creature_stats_invalid(crconf) || (crconf->hunger_rate == 0)) {
        return false;
    }
    return (cctrl->hunger_level * 4) >= ((int64_t)crconf->hunger_rate * 3);
}

static TbBool creature_owed_pay(const struct Thing *thing)
{
    const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
    return !creature_control_invalid(cctrl) && (cctrl->paydays_owed > 0);
}

static TbBool in_manual_control(const struct Thing *thing)
{
    return (thing->active_state == CrSt_ManualControl) || (thing->continue_state == CrSt_ManualControl);
}

static void record_auto_release(NetUserId user, ThingIndex idx, const char *reason)
{
    struct ExtSeatQueue *q = &s_queue[user];
    struct ExtSeatAutoRelease *r = &q->auto_rel[q->auto_total % EXTSEAT_AUTO_RELEASE_RING];
    memset(r, 0, sizeof(*r));
    r->thing_id = idx;
    snprintf(r->reason, sizeof(r->reason), "%s", reason);
    r->turn = (int64_t)get_gameturn();
    q->auto_total++;
}

static void hand_back(NetUserId user, struct OrderedCreature *o, const char *reason)
{
    struct Thing *thing = thing_get(o->idx);
    if (!thing_is_invalid(thing) && thing_is_creature(thing) && (thing->creation_turn == o->creation_turn) && in_manual_control(thing)) {
        set_start_state(thing);
        record_auto_release(user, o->idx, reason);
        WARNLOG("Ordered creature %" PRId64 " handed back to its own behaviour (%s)", (int64_t)o->idx, reason);
    }
}

static void drop_ordered(struct ExtSeatQueue *q, int64_t i)
{
    q->ordered[i] = q->ordered[--q->ordered_count];
}

int64_t extseat_auto_releases(NetUserId user, struct ExtSeatAutoRelease *out, int64_t max)
{
    if (!user_ok(user) || (out == NULL)) {
        return 0;
    }
    const struct ExtSeatQueue *q = &s_queue[user];
    const int64_t have = (q->auto_total < EXTSEAT_AUTO_RELEASE_RING) ? q->auto_total : EXTSEAT_AUTO_RELEASE_RING;
    int64_t n = 0;
    for (int64_t i = q->auto_total - have; (i < q->auto_total) && (n < max); i++) {
        out[n++] = q->auto_rel[i % EXTSEAT_AUTO_RELEASE_RING];
    }
    return n;
}

static struct ExtSeatChat s_chat[EXTSEAT_CHAT_LOG];
static int64_t s_chat_total = 0;

void extseat_note_chat(PlayerNumber plyr_idx, const char *text)
{
    if ((text == NULL) || (text[0] == 0)) return;
    struct ExtSeatChat *c = &s_chat[s_chat_total % EXTSEAT_CHAT_LOG];
    c->player = plyr_idx;
    snprintf(c->text, sizeof(c->text), "%s", text);
    c->turn = (int64_t)get_gameturn();
    s_chat_total++;
}

int64_t extseat_chat_total(void)
{
    return s_chat_total;
}

int64_t extseat_chat_log(struct ExtSeatChat *out, int64_t max)
{
    if (out == NULL) return 0;
    const int64_t have = (s_chat_total < EXTSEAT_CHAT_LOG) ? s_chat_total : EXTSEAT_CHAT_LOG;
    int64_t n = 0;
    for (int64_t i = s_chat_total - have; (i < s_chat_total) && (n < max); i++) out[n++] = s_chat[i % EXTSEAT_CHAT_LOG];
    return n;
}

int64_t extseat_pending_dig_rects(NetUserId user, int64_t *out4, int64_t max)
{
    if (!user_ok(user) || (out4 == NULL)) {
        return 0;
    }
    const struct ExtSeatQueue *q = &s_queue[user];
    int64_t n = 0;
    if ((q->cur_id != 0) && (q->next < q->count) && (q->cur_verb.kind == ESV_MarkDig) && q->cur_verb.has_rect && (n < max)) {
        const struct ExtSeatVerb *v = &q->cur_verb;
        out4[4 * n] = v->slab_x0; out4[4 * n + 1] = v->slab_y0; out4[4 * n + 2] = v->slab_x1; out4[4 * n + 3] = v->slab_y1;
        n++;
    }
    for (int64_t i = 0; (i < q->fifo_count) && (n < max); i++) {
        const struct ExtSeatVerb *v = &q->fifo[(q->fifo_head + i) % EXTSEAT_MAX_QUEUED_VERBS];
        if ((v->kind != ESV_MarkDig) || !v->has_rect) continue;
        out4[4 * n] = v->slab_x0; out4[4 * n + 1] = v->slab_y0; out4[4 * n + 2] = v->slab_x1; out4[4 * n + 3] = v->slab_y1;
        n++;
    }
    return n;
}

int64_t extseat_ordered_count(NetUserId user)
{
    return user_ok(user) ? s_queue[user].ordered_count : 0;
}

void extseat_release_ordered_now(NetUserId user, const char *reason)
{
    if (!user_ok(user)) {
        return;
    }
    struct ExtSeatQueue *q = &s_queue[user];
    for (int64_t i = 0; i < q->ordered_count; i++) {
        hand_back(user, &q->ordered[i], reason);
    }
    q->ordered_count = 0;
}

/** One turn of the safety net for one seat. */
static void sweep_ordered(NetUserId user)
{
    struct ExtSeatQueue *q = &s_queue[user];
    const GameTurn now = get_gameturn();
    for (int64_t i = 0; i < q->ordered_count;) {
        struct OrderedCreature *o = &q->ordered[i];
        struct Thing *thing = thing_get(o->idx);
        if (thing_is_invalid(thing) || !thing_is_creature(thing) || (thing->creation_turn != o->creation_turn)) {
            drop_ordered(q, i); // gone (dead, or the slot was reused)
            continue;
        }
        if (!in_manual_control(thing)) {
            // The order's clicks take a few turns to land; after that, not being in manual control means someone (the
            // agent, or the engine) already handed the creature back.
            if ((now > o->ordered_turn + 30)) {
                drop_ordered(q, i);
                continue;
            }
            i++;
            continue;
        }
        const char *why = NULL;
        // A held creature does not fight back on its own (CrSt_ManualControl), so an enemy engaging it would otherwise
        // kill it where it stands; the attacker is already in its opponent lists, so handing it back lets it fight.
        if (creature_is_being_attacked_by_enemy_player(thing)) why = "attacked";
        else if (creature_near_hunger_limit(thing)) why = "hungry";
        else if (creature_owed_pay(thing)) why = "owed_pay";
        else if (turns_to_payday(thing->owner) <= EXTSEAT_ORDER_PAYDAY_MARGIN_TURNS) why = "payday";
        else if (now >= o->hold_until) why = "max_hold";
        if (why != NULL) {
            hand_back(user, o, why);
            drop_ordered(q, i);
            continue;
        }
        i++;
    }
}

static void note_order_started(NetUserId user, PlayerNumber plyr_idx, const struct ExtSeatVerb *verb)
{
    struct ExtSeatQueue *q = &s_queue[user];
    if (!verb->has_thing) {
        return;
    }
    struct Thing *thing = thing_get((ThingIndex)verb->thing_id);
    if (thing_is_invalid(thing) || !thing_is_creature(thing)) {
        return;
    }
    for (int64_t i = 0; i < q->ordered_count; i++) {
        if (q->ordered[i].idx == thing->index) {
            drop_ordered(q, i);
            break;
        }
    }
    if (verb->kind != ESV_MoveCreature) {
        return; // a release: no longer tracked
    }
    if (q->ordered_count >= EXTSEAT_MAX_ORDERED) {
        drop_ordered(q, 0);
    }
    struct OrderedCreature *o = &q->ordered[q->ordered_count++];
    o->idx = thing->index;
    o->creation_turn = thing->creation_turn;
    o->ordered_turn = get_gameturn();
    const int64_t hold = (verb->hold_turns > 0) ? verb->hold_turns : default_hold_turns(plyr_idx);
    o->hold_until = get_gameturn() + hold;
}

int64_t extseat_results(NetUserId user, struct ExtSeatResult *out, int64_t max)
{
    if (!user_ok(user) || (out == NULL)) {
        return 0;
    }
    const struct ExtSeatQueue *q = &s_queue[user];
    const int64_t have = (q->results_total < EXTSEAT_RESULT_RING) ? q->results_total : EXTSEAT_RESULT_RING;
    int64_t n = 0;
    for (int64_t i = q->results_total - have; (i < q->results_total) && (n < max); i++) {
        out[n++] = q->results[i % EXTSEAT_RESULT_RING];
    }
    return n;
}

TbBool extseat_enqueue(NetUserId user, const struct ExtSeatStep *steps, int64_t count)
{
    if (!user_ok(user) || (count < 1) || (count > EXTSEAT_MAX_STEPS) || !extseat_idle(user)) {
        return false;
    }
    memcpy(s_queue[user].steps, steps, sizeof(struct ExtSeatStep) * count);
    s_queue[user].count = count;
    s_queue[user].next = 0;
    return true;
}

void extseat_forget_user(NetUserId user)
{
    if (user_ok(user)) {
        memset(&s_queue[user], 0, sizeof(s_queue[user]));
    }
}

void extseat_set_takeover(TbBool armed)
{
    s_takeover = armed;
}

TbBool extseat_takeover_armed(void)
{
    return s_takeover;
}

void extseat_reset(void)
{
    memset(s_chat, 0, sizeof(s_chat));
    s_chat_total = 0;
    s_takeover = false;
    memset(s_queue, 0, sizeof(s_queue));
    s_agent_owns_pause = false;
    s_advancing = false;
    s_advance_target = 0;
    s_pending_advance = 0;
}

int64_t extseat_queued_steps(NetUserId user)
{
    return user_ok(user) ? (s_queue[user].count - s_queue[user].next) : 0;
}

TbBool extseat_mid_gesture(NetUserId user)
{
    return user_ok(user) && s_queue[user].button_down;
}

int64_t extseat_cancel(NetUserId user)
{
    if (!user_ok(user)) {
        return 0;
    }
    struct ExtSeatQueue *q = &s_queue[user];
    int64_t dropped = (q->next < q->count) ? (q->count - q->next) : 0;
    while (q->fifo_count > 0) {
        record_result(user, q->fifo_id[q->fifo_head], "CANCELLED");
        q->fifo_head = (q->fifo_head + 1) % EXTSEAT_MAX_QUEUED_VERBS;
        q->fifo_count--;
        dropped++;
    }
    if ((q->cur_id != 0) && (q->next < q->count)) {
        record_result(user, q->cur_id, "CANCELLED");
    }
    q->cur_id = 0;
    q->cur_error = NULL;
    q->count = q->next = 0;
    q->step_wait = 0;
    if (q->button_down) {
        // Leaving the tool cancels a drag in progress (a state switch clears the roomspace and its drag), and the
        // release clears the engine's "button down" bookkeeping so the next gesture starts clean.
        struct ExtSeatStep end[2];
        memset(end, 0, sizeof(end));
        end[0].action = PckA_SetPlyrState;
        end[0].par1 = PSt_CtrlDungeon;
        end[1].has_pos = true;
        end[1].control_flags = PCtr_LBtnRelease;
        memcpy(q->steps, end, sizeof(end));
        q->count = 2;
    }
    return dropped;
}

static TbBool game_paused(void)
{
    return flag_is_set(kfx_sim_state.operation_flags, GOF_Paused);
}

// The poll at the start of update() pauses a finished advance before that turn's simulation runs, so
// a step written for that same tick would be dispatched into a paused game; skip it.
static TbBool advance_is_finished(void)
{
    return s_advancing && (s_pending_advance == 0) && (get_gameturn() >= s_advance_target);
}

void extseat_tick(void)
{
    // An advance starts here, before this tick's step is written, not from the API poll: the poll runs inside
    // update(), so unpausing there would let a whole turn run before any step could be applied to it.
    if (s_pending_advance > 0) {
        s_advance_target = get_gameturn() + s_pending_advance;
        s_pending_advance = 0;
        clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    }
    if (game_paused() || advance_is_finished()) {
        return;
    }
    for (NetUserId user = SOLO_HUMAN_ID + 1; user < MAX_NET_USERS; user++) {
        struct ExtSeatQueue *q = &s_queue[user];
        if (get_net_user_player_number(user) < 0) {
            memset(q, 0, sizeof(*q)); // the seat is gone (a load without it)
            continue;
        }
        sweep_ordered(user);
        promote_verbs(user);
        if (q->next >= q->count) {
            continue;
        }
        const struct ExtSeatStep *s = &q->steps[q->next];
        if (s->wait_build_free) {
            const struct PlayerInfo *player = get_player(get_net_user_player_number(user));
            if (player->roomspace.is_active) {
                if (q->step_wait++ < EXTSEAT_BUILD_WAIT_TURNS) {
                    continue;
                }
                q->next = q->count;
                q->step_wait = 0;
                q->cur_error = "BUILD_BUSY";
                continue;
            }
        }
        if (s->wait_hand_free) {
            const struct PlayerInfo *player = get_player(get_net_user_player_number(user));
            // A hand instance still playing: the engine ignores a new hand action (a grab, a drop, a slap) until it ends.
            if ((player->instance_num == PI_Grab) || (player->instance_num == PI_Drop)
             || (player->instance_num == PI_Whip) || (player->instance_num == PI_WhipEnd)) {
                if (q->step_wait++ < EXTSEAT_HAND_WAIT_TURNS) {
                    continue;
                }
                q->next = q->count;
                q->step_wait = 0;
                q->cur_error = "HAND_BUSY";
                continue;
            }
        }
        if (s->hand_set_drop) {
            const PlayerNumber plyr = get_net_user_player_number(user);
            const struct Dungeon *dungeon = get_dungeon(plyr);
            const struct ExtSeatVerb *v = &q->cur_verb;
            // One thing (thing_id) or several (thing_ids).
            const int64_t nset = (v->thing_count > 0) ? v->thing_count : 1;
            const int64_t *set = (v->thing_count > 0) ? v->thing_ids : &v->thing_id;
            const ThingIndex top = (dungeon->num_things_in_hand > 0) ? dungeon->things_in_hand[0] : 0;
            int64_t mine = -1, dropped = 0, waiting = 0;
            for (int64_t i = 0; i < nset; i++) {
                if ((q->hand_dropped & (1ULL << i)) != 0) { dropped++; continue; }
                if (set[i] == top) mine = i;
                const struct Thing *t = thing_get((ThingIndex)set[i]);
                if (!thing_is_invalid(t) && !thing_is_picked_up_by_player(t, plyr)) waiting++;
            }
            if (mine < 0) {
                // Nothing of ours on top of the hand. A pick may still be landing (the grab is a player instance), so
                // give it a moment; then end the verb -- never drop something else.
                if ((top == 0) && (waiting > 0) && (dropped == 0) && (q->step_wait++ < 3)) {
                    continue;
                }
                q->next = q->count;
                q->step_wait = 0;
                if (dropped == 0) {
                    q->cur_error = (top == 0) ? "PICK_UP_FAILED" : "HAND_CHANGED";
                } else if (dropped < nset) {
                    snprintf(q->cur_error_buf, sizeof(q->cur_error_buf), "PARTIAL: %" PRId64 " of %" PRId64, dropped, nset);
                    q->cur_error = q->cur_error_buf;
                }
                continue;
            }
            q->hand_dropped |= (1ULL << mine);
        }
        q->step_wait = 0;
        q->next++;
        if (s->send_chat) {
            struct PlayerInfo *speaker = get_player(get_net_user_player_number(user));
            snprintf(speaker->mp_pending_message, PLAYER_MP_MESSAGE_LEN, "%s", q->cur_verb.message);
            q->last_chat_turn = (int64_t)get_gameturn();
        }
        struct Packet *p = get_packet(user);
        if (s->action != PckA_None) {
            set_packet_action(p, s->action, s->par1, s->par2, s->par3, s->par4);
        }
        if (s->has_pos) {
            int64_t px = s->pos_x, py = s->pos_y;
            if (s->follow_thing != 0) {
                // Order-creature clicks pick "the creature nearest the cursor": aim at where it is now, not where it was
                // when the verb was planned (it may have walked since).
                const struct Thing *ft = thing_get(s->follow_thing);
                if (!thing_is_invalid(ft)) {
                    // The exact position, not the subtile centre: the engine takes the creature nearest the cursor, and a
                    // neighbour standing in the same subtile would otherwise win.
                    px = ft->mappos.x.val;
                    py = ft->mappos.y.val;
                }
            }
            set_players_packet_position(p, px, py, s->context);
        }
        p->control_flags |= s->control_flags;
        if ((s->control_flags & (PCtr_LBtnClick | PCtr_LBtnHeld)) != 0) {
            q->button_down = true;
        }
        if ((s->control_flags & PCtr_LBtnRelease) != 0) {
            q->button_down = false;
        }
    }
}

void extseat_note_activity(void)
{
    s_last_activity = LbTimerClock();
}

void extseat_pause(void)
{
    set_flag(kfx_sim_state.operation_flags, GOF_Paused);
    s_agent_owns_pause = true;
    s_advancing = false;
    s_pending_advance = 0;
    extseat_note_activity();
}

void extseat_resume(void)
{
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    s_agent_owns_pause = false;
    s_advancing = false;
    s_pending_advance = 0;
    extseat_note_activity();
}

void extseat_advance(int64_t turns)
{
    if (turns < 1) {
        return;
    }
    s_pending_advance = turns;
    s_advancing = true;
    s_agent_owns_pause = true;
    extseat_note_activity();
}

TbBool extseat_agent_owns_pause(void)
{
    return s_agent_owns_pause;
}

TbBool extseat_advancing(void)
{
    return s_advancing;
}

void extseat_set_watchdog_ms(int64_t ms)
{
    s_watchdog_ms = ms;
}

static void takeover_if_armed(const char *why)
{
    if (s_takeover) {
        const int64_t n = net_release_all_external_seats();
        WARNLOG("%s: handed %" PRId64 " External seat(s) to the built-in AI", why, (int64_t)n);
        s_takeover = false;
    }
}

void extseat_on_client_lost(void)
{
    if (s_agent_owns_pause) {
        WARNLOG("The agent connection was lost while it held the pause; resuming the game");
        extseat_resume();
    }
    for (NetUserId u = SOLO_HUMAN_ID + 1; u < MAX_NET_USERS; u++) {
        extseat_release_ordered_now(u, "agent_lost");
    }
    takeover_if_armed("The agent connection was lost");
}

void extseat_poll(void)
{
    if (advance_is_finished()) {
        set_flag(kfx_sim_state.operation_flags, GOF_Paused);
        s_advancing = false;
        extseat_note_activity();
        return;
    }
    if (s_agent_owns_pause && game_paused() && (s_watchdog_ms > 0)
     && ((int64_t)(LbTimerClock() - s_last_activity) > s_watchdog_ms)) {
        WARNLOG("No seat command for %" PRId64 " ms while the agent held the pause; resuming the game", (int64_t)s_watchdog_ms);
        extseat_resume();
        takeover_if_armed("The watchdog fired");
    }
}

// ---------------------------------------------------------------------------------------------
// Verbs
// ---------------------------------------------------------------------------------------------

static TbBool stl_on_map(int64_t x, int64_t y)
{
    return (x >= 0) && (y >= 0) && (x < kfx_sim_state.map_subtiles_x) && (y < kfx_sim_state.map_subtiles_y);
}

#define PRESS_FLAGS (PCtr_LBtnClick | PCtr_LBtnHeld)

static TbBool slab_on_map(int64_t x, int64_t y)
{
    return (x >= 0) && (y >= 0) && (x < kfx_sim_state.map_tiles_x) && (y < kfx_sim_state.map_tiles_y);
}

static void step_action(struct ExtSeatStep *s, unsigned char action, int64_t p1, int64_t p2)
{
    memset(s, 0, sizeof(*s));
    s->action = action;
    s->par1 = p1;
    s->par2 = p2;
}

static void step_click_ctx(struct ExtSeatStep *s, int64_t stl_x, int64_t stl_y, uint64_t button, unsigned char context)
{
    memset(s, 0, sizeof(*s));
    s->has_pos = true;
    s->pos_x = subtile_coord_center(stl_x);
    s->pos_y = subtile_coord_center(stl_y);
    s->control_flags = button;
    s->context = context;
}

static void step_click(struct ExtSeatStep *s, int64_t stl_x, int64_t stl_y, uint64_t button)
{
    step_click_ctx(s, stl_x, stl_y, button, 0);
}

static void step_slab(struct ExtSeatStep *s, int64_t slb_x, int64_t slb_y, uint64_t button, unsigned char context)
{
    step_click_ctx(s, slab_subtile_center(slb_x), slab_subtile_center(slb_y), button, context);
}

static void step_idle(struct ExtSeatStep *s)
{
    memset(s, 0, sizeof(*s));
}

static const struct Thing *creature_thing(int64_t id)
{
    const struct Thing *thing = thing_get((ThingIndex)id);
    if (thing_is_invalid(thing) || !thing_is_creature(thing)) {
        return NULL;
    }
    return thing;
}


#define PRESS (PCtr_LBtnClick | PCtr_LBtnHeld) /* the frame the button goes down: the input driver reports both */
#define HOLD  (PCtr_LBtnHeld)
#define LET_GO (PCtr_LBtnRelease)

static int64_t extseat_plan_dig_sweep_from(struct ExtSeatStep *out, int64_t max, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int corner);

int64_t extseat_plan_dig_sweep(struct ExtSeatStep *out, int64_t max, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    return extseat_plan_dig_sweep_from(out, max, x0, y0, x1, y1, 0);
}

/** extseat_plan_dig_sweep starting from a chosen corner (0 top-left, 1 bottom-right, 2 top-right, 3 bottom-left): a
 *  pickaxe drag tags or untags depending on its first slab, so mark_dig starts on one not already marked. */
static int64_t extseat_plan_dig_sweep_from(struct ExtSeatStep *out, int64_t max, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int corner)
{
    const int64_t lx = (x0 < x1) ? x0 : x1, rx = (x0 < x1) ? x1 : x0;
    const int64_t ty = (y0 < y1) ? y0 : y1, by = (y0 < y1) ? y1 : y0;
    const int64_t rows = by - ty + 1;
    int64_t n = 0;
    if (rows > EXTSEAT_MAX_DIG_ROWS || (4 + 1 + 2 * rows + 1) > max) {
        return 0;
    }
    const TbBool rev_x = (corner == 1) || (corner == 2), rev_y = (corner == 1) || (corner == 3);
    const int64_t sx = rev_x ? rx : lx, ex = rev_x ? lx : rx;
    const int64_t sy = rev_y ? by : ty, dy = rev_y ? -1 : 1;
    // The brush is a 1x1 box; the pickaxe cursor context makes the held button tag what the cursor crosses,
    // and the engine interpolates a fast move between turns, so each row is one sweep and each row change one step.
    // That is the pickaxe's box mode, which is the player's own setting (a local player starts in DEFAULT_TAG_MODE,
    // and in DRAG mode a drag tags only the box from press to release), so set it rather than inherit it.
    step_action(&out[n++], PckA_SetPlyrState, PSt_CtrlDungeon, 0);
    step_action(&out[n++], PckA_SetRoomspaceMan, 1, 0);
    step_action(&out[n++], PckA_SetRoomspaceHighlight, box_placement_mode, 1);
    step_idle(&out[n++]);
    step_slab(&out[n++], sx, sy, PRESS, CSt_PickAxe);
    int64_t x = sx;
    int64_t y = sy;
    for (int64_t r = 0; r < rows; r++) {
        y = sy + dy * r;
        if (r > 0) {
            step_slab(&out[n++], x, y, HOLD, CSt_PickAxe);
        }
        const int64_t end = (r % 2 == 0) ? ex : sx;
        step_slab(&out[n++], end, y, HOLD, CSt_PickAxe);
        x = end;
    }
    step_slab(&out[n++], x, y, LET_GO, CSt_PickAxe);
    return n;
}

/** Press-and-release once over each of `count` slabs in the given player state and cursor context. */
static int64_t plan_slab_taps(struct ExtSeatStep *out, int64_t max, const int64_t *slab_xy, int64_t count, int64_t state, unsigned char context)
{
    int64_t n = 0;
    if (count < 1 || (4 + 2 * count) > max) {
        return 0;
    }
    step_action(&out[n++], PckA_SetPlyrState, state, 0);
    step_action(&out[n++], PckA_SetRoomspaceMan, 1, 0);
    // One slab per tap: the pickaxe's box mode, not the player's DRAG or auto-detect tag mode (see the dig sweep).
    step_action(&out[n++], PckA_SetRoomspaceHighlight, box_placement_mode, 1);
    step_idle(&out[n++]);
    for (int64_t i = 0; i < count; i++) {
        step_slab(&out[n++], slab_xy[2 * i], slab_xy[2 * i + 1], PRESS, context);
        step_slab(&out[n++], slab_xy[2 * i], slab_xy[2 * i + 1], LET_GO, context);
    }
    return n;
}

int64_t extseat_plan_slab_clicks(struct ExtSeatStep *out, int64_t max, const int64_t *slab_xy, int64_t count)
{
    int64_t n = 0;
    if (count < 1 || (3 + 2 * count) > max) {
        return 0;
    }
    step_action(&out[n++], PckA_SetPlyrState, PSt_Sell, 0);
    step_action(&out[n++], PckA_SetRoomspaceMan, 1, 0);
    step_idle(&out[n++]);
    for (int64_t i = 0; i < count; i++) {
        step_slab(&out[n++], slab_xy[2 * i], slab_xy[2 * i + 1], PRESS, 0);
        step_slab(&out[n++], slab_xy[2 * i], slab_xy[2 * i + 1], LET_GO, 0);
    }
    return n;
}

/** Validates `verb` against the live state and writes its steps into `steps` (EXTSEAT_MAX_STEPS long). NULL on success. */
/** Whether a bridge-kind slab could be built at (x, y) once `built` (the rectangle's slabs already built in this plan,
 *  indexed from lx/ty with width w) count as the player's land -- the engine's own rule, can_build_room_at_slab, needs
 *  a slab of the player's safe land beside a bridge slab, and a bridge slab just built is such land. */
static TbBool bridge_slab_buildable(PlayerNumber plyr_idx, RoomKind rkind, MapSlabCoord x, MapSlabCoord y,
    const unsigned char *built, int64_t lx, int64_t ty, int64_t w, int64_t h)
{
    if (can_build_room_at_slab(plyr_idx, rkind, x, y)) return true;
    const struct SlabMap *slb = get_slabmap_block(x, y);
    if (!room_can_build_on_bridge_slab(rkind, slb->kind) || (slb->room_index > 0)) return false;
    if (!subtile_revealed(slab_subtile_center(x), slab_subtile_center(y), plyr_idx)) return false;
    if (slab_has_trap_on(x, y) || slab_has_door_thing_on(x, y)) return false;
    static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
    for (int d = 0; d < 4; d++) {
        const int64_t nx = x + dx[d] - lx, ny = y + dy[d] - ty;
        if ((nx >= 0) && (nx < w) && (ny >= 0) && (ny < h) && built[ny * w + nx]) return true;
    }
    return false;
}

int64_t extseat_room_build_check(PlayerNumber plyr_idx, RoomKind rkind, int64_t x0, int64_t y0, int64_t x1, int64_t y1,
    int *corner_out, int64_t *bad_xy, int64_t max_bad, int64_t *bad_count)
{
    const int64_t lx = (x0 < x1) ? x0 : x1, rx = (x0 < x1) ? x1 : x0, ty = (y0 < y1) ? y0 : y1, by = (y0 < y1) ? y1 : y0;
    const int64_t w = rx - lx + 1, h = by - ty + 1;
    unsigned char built[EXTSEAT_MAX_BUILD_SLABS], best_built[EXTSEAT_MAX_BUILD_SLABS];
    if ((w * h > EXTSEAT_MAX_BUILD_SLABS) || (w <= 0) || (h <= 0)) { if (bad_count) *bad_count = 0; return 0; }
    int64_t best = -1;
    int best_corner = 0;
    if (!room_role_matches(rkind, RoRoF_PassWater | RoRoF_PassLava | RoRoF_PassAbyss)) {
        // Any order: each slab stands on its own.
        best = 0;
        for (int64_t y = 0; y < h; y++)
            for (int64_t x = 0; x < w; x++) {
                best_built[y * w + x] = can_build_room_at_slab(plyr_idx, rkind, (MapSlabCoord)(lx + x), (MapSlabCoord)(ty + y)) ? 1 : 0;
                best += best_built[y * w + x];
            }
    } else {
        // Bridges: the engine builds one slab per turn, in the drag's order (rows from the start corner's row, and
        // along each row from its column), and each needs land beside it by its turn -- so simulate every start corner
        // (0 top-left, 1 bottom-right, 2 top-right, 3 bottom-left: roomspace.c's find_next_point modes) and keep the best.
        for (int c = 0; c < 4; c++) {
            memset(built, 0, (size_t)(w * h));
            const TbBool rev_x = (c == 1) || (c == 2), rev_y = (c == 1) || (c == 3);
            int64_t count = 0;
            for (int64_t iy = 0; iy < h; iy++) {
                const int64_t y = rev_y ? (h - 1 - iy) : iy;
                for (int64_t ix = 0; ix < w; ix++) {
                    const int64_t x = rev_x ? (w - 1 - ix) : ix;
                    if (bridge_slab_buildable(plyr_idx, rkind, (MapSlabCoord)(lx + x), (MapSlabCoord)(ty + y), built, lx, ty, w, h)) {
                        built[y * w + x] = 1;
                        count++;
                    }
                }
            }
            if (count > best) { best = count; best_corner = c; memcpy(best_built, built, (size_t)(w * h)); }
        }
    }
    int64_t nbad = 0;
    for (int64_t y = 0; y < h; y++)
        for (int64_t x = 0; x < w; x++) {
            if (best_built[y * w + x]) continue;
            if ((bad_xy != NULL) && (nbad < max_bad)) { bad_xy[2 * nbad] = lx + x; bad_xy[2 * nbad + 1] = ty + y; }
            nbad++;
        }
    if (bad_count) *bad_count = nbad;
    if (corner_out) *corner_out = best_corner;
    return best;
}

/** A thing the hand may take for this seat: its own creature, a prisoner in its custody, or an object the hand picks
 *  up (food, gold, ...). NULL with `err` set otherwise. */
static struct Thing *hand_thing(PlayerNumber plyr_idx, int64_t id, const char **err)
{
    struct Thing *thing = thing_get((ThingIndex)id);
    if (thing_is_invalid(thing) || !(thing_is_creature(thing) || thing_is_object(thing))) { *err = "NO_SUCH_THING"; return NULL; }
    if (thing_is_creature(thing) && (thing->owner != plyr_idx) && !creature_is_kept_in_custody_by_player(thing, plyr_idx)) {
        *err = "NOT_YOURS";
        return NULL;
    }
    if (!can_thing_be_picked_up_by_player(thing, plyr_idx)) { *err = "CANNOT_PICK_UP"; return NULL; }
    return thing;
}

/** Whether dropping `thing` at the subtile is what the order says it means. A prisoner dropped anywhere but the
 *  seat's own prison or torture chamber goes free (unless the order says `release`); anything dropped on a temple pool
 *  is sacrificed (unless the order says `sacrifice`, only on the pool). */
static const char *check_drop_intent(PlayerNumber plyr_idx, const struct Thing *thing, MapSubtlCoord x, MapSubtlCoord y,
    TbBool release, TbBool sacrifice)
{
    if (!thing_is_creature(thing)) return NULL;
    const TbBool pool = subtile_has_sacrificial_on_top(x, y);
    if (pool && !sacrifice) return "WOULD_SACRIFICE";
    if (!pool && sacrifice) return "NOT_A_POOL";
    if ((thing->owner != plyr_idx) && !pool && !release) {
        const struct Room *room = subtile_room_get(x, y);
        const TbBool holds = !room_is_invalid(room) && (room->owner == plyr_idx)
            && (room_role_matches(room->kind, RoRoF_Prison) || room_role_matches(room->kind, RoRoF_Torture));
        if (!holds) return "WOULD_RELEASE_PRISONER";
    }
    return NULL;
}

/** A prisoner dropped into a prison or torture chamber with no room left for it is not held: on arrival the engine's
 *  add_creature_to_work_room refuses it ("room too small") and it walks free. Same test, summed over the prisoners
 *  being dropped (Job_CAPTIVITY in a prison, Job_PAINFUL_TORTURE in a torture chamber). */
static const char *check_room_admission(PlayerNumber plyr_idx, MapSubtlCoord x, MapSubtlCoord y, struct Thing *const *things, int64_t count)
{
    const struct Room *room = subtile_room_get(x, y);
    if (room_is_invalid(room) || (room->owner != plyr_idx)) return NULL;
    CreatureJob job;
    if (room_role_matches(room->kind, RoRoF_Prison)) job = Job_CAPTIVITY;
    else if (room_role_matches(room->kind, RoRoF_Torture)) job = Job_PAINFUL_TORTURE;
    else return NULL;
    int64_t need = 0;
    for (int64_t i = 0; i < count; i++) {
        if (thing_is_creature(things[i]) && (things[i]->owner != plyr_idx)) need += get_required_room_capacity_for_job(job, things[i]->model);
    }
    if ((need > 0) && (room->used_capacity + need > room->total_capacity)) return "ROOM_FULL";
    return NULL;
}

static TbBool room_drop_spot_ok(PlayerNumber plyr_idx, struct Thing *thing, MapSubtlCoord x, MapSubtlCoord y, TbBool sacrifice)
{
    return ((subtile_has_sacrificial_on_top(x, y) != 0) == (sacrifice != 0)) && can_place_thing_here(thing, x, y, plyr_idx);
}

/** to_room: a subtile inside the seat's room `room_id` where the hand would put `thing` -- the pool when the order
 *  sacrifices, else anywhere but the pool -- room centre first. */
static const char *resolve_room_drop(PlayerNumber plyr_idx, int64_t room_id, struct Thing *thing, TbBool sacrifice,
    int64_t *out_x, int64_t *out_y)
{
    struct Room *room = room_get((RoomIndex)room_id);
    if ((room_id <= 0) || room_is_invalid(room) || !room_exists(room)) return "NO_ROOM";
    if (room->owner != plyr_idx) return "NOT_YOUR_ROOM";
    // The room centre first, then every subtile of every slab of the room.
    if (room_drop_spot_ok(plyr_idx, thing, room->central_stl_x, room->central_stl_y, sacrifice)) {
        *out_x = room->central_stl_x; *out_y = room->central_stl_y;
        return NULL;
    }
    SlabCodedCoords sn = room->slabs_list;
    for (int64_t guard = 0; (sn != 0) && (guard < 4096); guard++) {
        const MapSlabCoord sx = slb_num_decode_x(sn), sy = slb_num_decode_y(sn);
        for (int k = 0; k < 9; k++) {
            const MapSubtlCoord x = slab_subtile(sx, k % 3), y = slab_subtile(sy, k / 3);
            if (room_drop_spot_ok(plyr_idx, thing, x, y, sacrifice)) {
                *out_x = x; *out_y = y;
                return NULL;
            }
        }
        sn = get_next_slab_number_in_room(sn);
    }
    return sacrifice ? "NO_POOL" : "CANNOT_DROP_HERE";
}

/** send_message: at most one per EXTSEAT_CHAT_MIN_TURNS -- short messages, not a stream. */
static const char *check_chat_rate(NetUserId user, const struct ExtSeatVerb *verb)
{
    const struct ExtSeatQueue *q = &s_queue[user];
    if ((verb->kind == ESV_SendMessage) && (q->last_chat_turn > 0)
     && ((int64_t)get_gameturn() - q->last_chat_turn < EXTSEAT_CHAT_MIN_TURNS)) return "TOO_SOON";
    return NULL;
}

static const char *plan_verb(PlayerNumber plyr_idx, const struct ExtSeatVerb *verb, struct ExtSeatStep *steps, int64_t *out_n)
{
    int64_t n = 0;
    struct PlayerInfo *player = get_player(plyr_idx);
    struct Dungeon *dungeon = get_players_dungeon(player);
    switch (verb->kind) {
    case ESV_PlaceTrap:
    case ESV_PlaceDoor: {
        if (!verb->has_pos) return "MISSING_POSITION";
        if (!stl_on_map(verb->stl_x, verb->stl_y)) return "POSITION_OFF_MAP";
        const TbBool trap = (verb->kind == ESV_PlaceTrap);
        const int64_t model = trap ? trap_model_id(verb->name) : door_model_id(verb->name);
        if (model < 1) return "UNKNOWN_KIND";
        if (!(trap ? is_trap_placeable(plyr_idx, model) : is_door_placeable(plyr_idx, model))) return "NOT_AVAILABLE";
        // The engine's own spot tests (the click would otherwise land and do nothing): a trap on the seat's claimed
        // floor (or a bridge, for the kinds allowed there) not already trapped; a door on its claimed floor between two
        // walls, with nothing across the doorway.
        if (trap ? !can_place_trap_on(plyr_idx, (MapSubtlCoord)verb->stl_x, (MapSubtlCoord)verb->stl_y, (ThingModel)model)
                 : !door_placement_allowed(plyr_idx, (MapSubtlCoord)verb->stl_x, (MapSubtlCoord)verb->stl_y)) return "CANNOT_PLACE_HERE";
        step_action(&steps[n++], PckA_SetPlyrState, trap ? PSt_PlaceTrap : PSt_PlaceDoor, model);
        step_click(&steps[n++], verb->stl_x, verb->stl_y, PCtr_LBtnClick);
        break;
    }
    case ESV_Slap: {
        if (!verb->has_thing) return "MISSING_THING";
        struct Thing *thing = thing_get((ThingIndex)verb->thing_id);
        if (thing_is_invalid(thing) || !(thing_is_creature(thing) || (thing->class_id == TCls_Trap))) return "NO_SUCH_THING";
        if (!thing_slappable(thing, plyr_idx)) return "NOT_SLAPPABLE";
        if (!dungeon_invalid(dungeon)
         && ((int64_t)(get_gameturn() - dungeon->last_creature_dropped_gameturn) <= SLAP_AFTER_DROP_TURNS)) return "SLAP_NOT_READY";
        // A whip still playing is waited out by the step (wait_hand_free), not refused: several slaps can be batched.
        step_action(&steps[n], PckA_UsePwrOnThing, PwrK_SLAP, thing->index);
        steps[n].wait_hand_free = true; // the engine ignores a slap while the last one's whip is still playing
        if ((thing->class_id == TCls_Trap)) {
            // A slapped trap that shoots at a target (a boulder) goes the way the slapper's camera faces: the packet
            // carries that angle (packet_action_has_camera_angle), so the order says which way.
            const struct TrapConfigStats *trapst = get_trap_model_stats(thing->model);
            const TbBool aimed = (trapst->activation_type == TrpAcT_HeadforTarget90) || (trapst->activation_type == TrpAcT_CreatureShot);
            if (aimed && !verb->has_direction) return "MISSING_DIRECTION";
            if (verb->has_direction) {
                steps[n].par3 = verb->direction_angle;
                steps[n].par4 = CamIV_Isometric;
            }
        }
        n++;
        break;
    }
    case ESV_PickUp: {
        if (!verb->has_thing) return "MISSING_THING";
        const char *err = NULL;
        const struct Thing *thing = hand_thing(plyr_idx, verb->thing_id, &err);
        if (thing == NULL) return err;
        step_action(&steps[n], PckA_UsePwrHandPick, thing->index, 0);
        steps[n++].wait_hand_free = true;
        break;
    }
    case ESV_PickUpAndDrop: {
        // pick_up then drop in one verb -- one thing or several (thing_ids, up to the hand's MaxThingsInHand), to one
        // spot (pos, or to_room) -- so an agent need not see the hand full before it may ask for the drop. Everything is
        // checked now with the engine's own tests: the hand must start empty (the drops release the top of the hand, and
        // every drop checks the top is one of this verb's things), each thing must be one the hand may take, and the
        // spot one the hand would drop it on, meaning what the order says (no prisoner freed, nothing sacrificed, by
        // accident).
        int64_t ids[EXTSEAT_MAX_HAND_SET];
        int64_t count = 0;
        if (verb->thing_count > 0) {
            if (verb->thing_count > EXTSEAT_MAX_HAND_SET) return "TOO_MANY";
            for (int64_t i = 0; i < verb->thing_count; i++) ids[count++] = verb->thing_ids[i];
        } else if (verb->has_thing) {
            ids[count++] = verb->thing_id;
        } else {
            return "MISSING_THING";
        }
        if (count > (int64_t)kfx_config_state.conf.rules[plyr_idx].gameplay.max_things_in_hand) return "TOO_MANY";
        if (!power_hand_is_empty(player)) return "HAND_NOT_EMPTY";
        struct Thing *things[EXTSEAT_MAX_HAND_SET];
        for (int64_t i = 0; i < count; i++) {
            for (int64_t j = 0; j < i; j++) if (ids[j] == ids[i]) return "DUPLICATE_THING";
            const char *err = NULL;
            things[i] = hand_thing(plyr_idx, ids[i], &err);
            if (things[i] == NULL) return err;
        }
        int64_t x, y;
        if (verb->has_room) {
            // No "room full" refusal: the hand itself never refuses a drop into a full room, so neither does the seat --
            // the view reports capacity and free torture devices for the agent to judge.
            const char *err = resolve_room_drop(plyr_idx, verb->room_id, things[0], verb->sacrifice, &x, &y);
            if (err != NULL) return err;
        } else if (verb->has_pos) {
            if (!stl_on_map(verb->stl_x, verb->stl_y)) return "POSITION_OFF_MAP";
            x = verb->stl_x; y = verb->stl_y;
        } else {
            return "MISSING_POSITION";
        }
        {
            const char *err = check_room_admission(plyr_idx, (MapSubtlCoord)x, (MapSubtlCoord)y, things, count);
            if (err != NULL) return err;
        }
        for (int64_t i = 0; i < count; i++) {
            if (!can_place_thing_here(things[i], x, y, plyr_idx)) return "CANNOT_DROP_HERE";
            const char *err = check_drop_intent(plyr_idx, things[i], (MapSubtlCoord)x, (MapSubtlCoord)y, verb->release, verb->sacrifice);
            if (err != NULL) return err;
        }
        if (2 * count > EXTSEAT_MAX_STEPS) return "TOO_MANY";
        for (int64_t i = 0; i < count; i++) {
            step_action(&steps[n], PckA_UsePwrHandPick, things[i]->index, 0);
            steps[n++].wait_hand_free = true;
        }
        for (int64_t i = 0; i < count; i++) {
            step_action(&steps[n], PckA_UsePwrHandDrop, x, y);
            steps[n].wait_hand_free = true;
            steps[n++].hand_set_drop = true;
        }
        break;
    }
    case ESV_Drop: {
        if (power_hand_is_empty(player)) return "HAND_EMPTY";
        struct Thing *held = get_first_thing_in_power_hand(player);
        int64_t x, y;
        if (verb->has_room) {
            const char *err = resolve_room_drop(plyr_idx, verb->room_id, held, verb->sacrifice, &x, &y);
            if (err != NULL) return err;
        } else if (verb->has_pos) {
            if (!stl_on_map(verb->stl_x, verb->stl_y)) return "POSITION_OFF_MAP";
            x = verb->stl_x; y = verb->stl_y;
        } else {
            return "MISSING_POSITION";
        }
        if (!thing_is_invalid(held)) {
            const char *err = check_drop_intent(plyr_idx, held, (MapSubtlCoord)x, (MapSubtlCoord)y, verb->release, verb->sacrifice);
            if (err == NULL) err = check_room_admission(plyr_idx, (MapSubtlCoord)x, (MapSubtlCoord)y, &held, 1);
            if (err != NULL) return err;
        }
        step_action(&steps[n], PckA_UsePwrHandDrop, x, y);
        steps[n++].wait_hand_free = true;
        break;
    }
    case ESV_CastPower: {
        const int64_t pwkind = power_model_id(verb->name);
        if (pwkind < 1) return "UNKNOWN_KIND";
        if (!is_power_available(plyr_idx, (PowerKind)pwkind)) return "NOT_AVAILABLE";
        if ((pwkind == PwrK_OBEY) || (pwkind == PwrK_ARMAGEDDON) || (pwkind == PwrK_HOLDAUDNC)) {
            // Whole-level powers: no target. (The engine takes them as a generic level power, exactly as the panel does.)
            if (verb->has_thing || verb->has_pos) return "UNSUPPORTED_POWER";
            step_action(&steps[n++], PckA_GenericLevelPower, 0, pwkind);
        } else if (verb->has_thing) {
            const struct Thing *thing = creature_thing(verb->thing_id);
            if (thing == NULL) return "NO_SUCH_THING";
            if (verb->overcharge_turns < 0 || verb->overcharge_turns > EXTSEAT_MAX_OVERCHARGE_TURNS) return "BAD_OVERCHARGE";
            if (verb->overcharge_turns == 0) {
                step_action(&steps[n++], PckA_UsePwrOnThing, pwkind, thing->index);
            } else {
                // As a human charges a creature power: choose it, hold the button while the level builds (one level per
                // four held turns), and release on the creature, which sends the cast with the charge reached.
                const MapSubtlCoord sx = thing->mappos.x.stl.num, sy = thing->mappos.y.stl.num;
                step_action(&steps[n++], PckA_SetPlyrState, PST_CastPowerOnTarget, pwkind);
                step_idle(&steps[n++]);
                step_click(&steps[n++], sx, sy, PRESS_FLAGS);
                for (int64_t i = 0; i < verb->overcharge_turns; i++) {
                    step_click(&steps[n++], sx, sy, PCtr_LBtnHeld);
                }
                step_click(&steps[n], sx, sy, PCtr_LBtnRelease);
                steps[n].action = PckA_UsePwrOnThing;
                steps[n].par1 = pwkind;
                steps[n].par2 = thing->index;
                n++;
            }
        } else if (verb->has_pos) {
            if (!stl_on_map(verb->stl_x, verb->stl_y)) return "POSITION_OFF_MAP";
            const uint64_t ws = get_power_model_stats((PowerKind)pwkind)->work_state;
            if ((ws != PSt_CastPowerOnSubtile) && (ws != PSt_CallToArms) && (ws != PSt_SightOfEvil) && (ws != PSt_CreateDigger)) {
                return "UNSUPPORTED_POWER";
            }
            if (verb->overcharge_turns < 0 || verb->overcharge_turns > EXTSEAT_MAX_OVERCHARGE_TURNS) return "BAD_OVERCHARGE";
            step_action(&steps[n++], PckA_SetPlyrState, ws, pwkind);
            if (verb->overcharge_turns == 0) {
                step_click(&steps[n++], verb->stl_x, verb->stl_y, PCtr_LBtnRelease);
            } else {
                // Holding builds the charge (one step per held turn; the level is turns / 4); the release casts at it.
                step_idle(&steps[n++]);
                step_click(&steps[n++], verb->stl_x, verb->stl_y, PRESS_FLAGS);
                for (int64_t i = 0; i < verb->overcharge_turns; i++) {
                    step_click(&steps[n++], verb->stl_x, verb->stl_y, PCtr_LBtnHeld);
                }
                step_click(&steps[n++], verb->stl_x, verb->stl_y, PCtr_LBtnRelease);
            }
        } else {
            return "MISSING_POSITION";
        }
        break;
    }
    case ESV_MoveCreature:
    case ESV_ReleaseCreature: {
        // Order creature mode (PSt_OrderCreatr): one click selects a creature, the next sends it to a subtile, a click on its
        // own subtile hands it back to its normal AI; leaving the mode clears the selection but not the order. Possession
        // needs a live view, this does not (ftest ai_gesture_order_creature records the engine's behaviour).
        if (!verb->has_thing) return "MISSING_THING";
        struct Thing *thing = (struct Thing *)creature_thing(verb->thing_id);
        if (thing == NULL) return "NO_SUCH_THING";
        if (thing->owner != plyr_idx) return "NOT_YOURS";
        if (thing_is_picked_up(thing)) return "NOT_ORDERABLE";
        const MapSubtlCoord cx = thing->mappos.x.stl.num, cy = thing->mappos.y.stl.num;
        int64_t goal_x = cx, goal_y = cy;
        if (verb->kind == ESV_MoveCreature) {
            // A creature held in manual control does not work, eat or collect its pay. Refuse orders that would cost it
            // that, and bound how long an order may hold it (the safety net releases it when the time is up).
            if ((verb->hold_turns < 0) || (verb->hold_turns > max_hold_turns(plyr_idx))) return "BAD_HOLD";
            if (creature_near_hunger_limit(thing)) return "HUNGRY";
            if (creature_owed_pay(thing)) return "OWED_PAY";
            if (turns_to_payday(plyr_idx) <= EXTSEAT_ORDER_PAYDAY_MARGIN_TURNS) return "PAYDAY_TOO_CLOSE";
            if (!verb->has_pos) return "MISSING_POSITION";
            if (!stl_on_map(verb->stl_x, verb->stl_y)) return "POSITION_OFF_MAP";
            if ((verb->stl_x == cx) && (verb->stl_y == cy)) return "ALREADY_THERE";
            struct Coord3d dest;
            memset(&dest, 0, sizeof(dest));
            dest.x.stl.num = (MapSubtlCoord)verb->stl_x;
            dest.y.stl.num = (MapSubtlCoord)verb->stl_y;
            dest.z.val = get_floor_height((MapSubtlCoord)verb->stl_x, (MapSubtlCoord)verb->stl_y);
            if (!creature_can_navigate_to(thing, &dest, NavRtF_Default)) return "CANNOT_REACH";
            goal_x = verb->stl_x;
            goal_y = verb->stl_y;
        }
        step_action(&steps[n++], PckA_SetPlyrState, PSt_CtrlDungeon, 0); // a clean start: no creature selected
        step_idle(&steps[n++]);
        step_action(&steps[n++], PckA_SetPlyrState, PSt_OrderCreatr, 0);
        step_idle(&steps[n++]);
        step_click(&steps[n], cx, cy, PCtr_LBtnRelease);                 // select the creature (aimed at where it is then)
        steps[n++].follow_thing = thing->index;
        step_idle(&steps[n++]);
        step_click(&steps[n], goal_x, goal_y, PCtr_LBtnRelease);         // send it there / (its own subtile) hand it back
        if (verb->kind == ESV_ReleaseCreature) steps[n].follow_thing = thing->index;
        n++;
        step_idle(&steps[n++]);
        step_action(&steps[n++], PckA_SetPlyrState, PSt_CtrlDungeon, 0); // leaving the mode clears the selection
        break;
    }
    case ESV_SetTendency: {
        if (!verb->has_enabled) return "MISSING_ENABLED";
        int64_t tend;
        if (strcasecmp(verb->name, "imprison") == 0) tend = CrTend_Imprison;
        else if (strcasecmp(verb->name, "flee") == 0) tend = CrTend_Flee;
        else return "UNKNOWN_KIND";
        if (((dungeon->creature_tendencies & tend) != 0) == (verb->enabled != 0)) return "ALREADY_SET";
        // As the panel button: imprison can only be switched on with a prison to take captives to (the packet itself
        // does not check, so without this a seat could do what a human cannot).
        if ((tend == CrTend_Imprison) && verb->enabled && !player_has_room_of_role(plyr_idx, RoRoF_Prison)) return "NO_PRISON";
        step_action(&steps[n++], PckA_ToggleTendency, tend, 0);
        break;
    }
    case ESV_SetAlliance: {
        // A one-way declaration, exactly what the human alliance button does (toggle_ally_with_player only ever
        // touches the caller's own allied_players bit): the engine's own rules (players_are_mutual_allies) require
        // both sides to declare before it actually changes combat/vision, and own.alliance in the view shows both.
        if (!verb->has_target_player) return "MISSING_TARGET_PLAYER";
        const PlayerNumber other = (PlayerNumber)verb->target_player;
        if ((other < 0) || (other >= PLAYERS_COUNT) || (other == plyr_idx)) return "INVALID_PLAYER";
        if (!player_exists(get_player(other))) return "INVALID_PLAYER";
        if (!verb->has_enabled) return "MISSING_ENABLED";
        if (is_player_ally_locked(plyr_idx, other)) return "ALLIANCE_LOCKED";
        if (player_allied_with(player, other) == (verb->enabled != 0)) return "ALREADY_SET";
        step_action(&steps[n++], PckA_PlyrToggleAlly, other, 0);
        break;
    }
    case ESV_BuildRoom: {
        if (!verb->has_rect) return "MISSING_RECT";
        const int64_t rx0 = verb->slab_x0, ry0 = verb->slab_y0, rx1 = verb->slab_x1, ry1 = verb->slab_y1;
        if (!slab_on_map(rx0, ry0) || !slab_on_map(rx1, ry1)) return "POSITION_OFF_MAP";
        const int64_t area = ((rx1 > rx0 ? rx1 - rx0 : rx0 - rx1) + 1) * ((ry1 > ry0 ? ry1 - ry0 : ry0 - ry1) + 1);
        if (area > EXTSEAT_MAX_BUILD_SLABS) return "AREA_TOO_LARGE";
        const int64_t rkind = get_rid(room_desc, verb->name);
        if (rkind < 1) return "UNKNOWN_KIND";
        if (!is_room_available(plyr_idx, (RoomKind)rkind)) return "NOT_AVAILABLE";
        // The engine's own per-slab rule, in the order it builds: a room nowhere in the rectangle is refused (the drag
        // would do nothing), and a bridge is dragged from the corner whose order lets each slab reach land in time.
        // A bridge drag is a path, not a box (roomspace.c's detect_bridge_shape: an L between the ends, turning the way the
        // previous drag did), so the seat builds bridges as straight lines only -- deterministic, and wider crossings
        // are several lines.
        if (room_role_matches((RoomKind)rkind, RoRoF_PassWater | RoRoF_PassLava | RoRoF_PassAbyss) && (rx0 != rx1) && (ry0 != ry1)) {
            return "BRIDGE_NOT_A_LINE";
        }
        int corner = 0;
        if (extseat_room_build_check(plyr_idx, (RoomKind)rkind, rx0, ry0, rx1, ry1, &corner, NULL, 0, NULL) == 0) return "CANNOT_BUILD_HERE";
        int64_t px = rx0, py = ry0, qx = rx1, qy = ry1;
        if (room_role_matches((RoomKind)rkind, RoRoF_PassWater | RoRoF_PassLava | RoRoF_PassAbyss)) {
            const int64_t lx = (rx0 < rx1) ? rx0 : rx1, hx = (rx0 < rx1) ? rx1 : rx0, ty = (ry0 < ry1) ? ry0 : ry1, by = (ry0 < ry1) ? ry1 : ry0;
            const TbBool rev_x = (corner == 1) || (corner == 2), rev_y = (corner == 1) || (corner == 3);
            px = rev_x ? hx : lx; qx = rev_x ? lx : hx;
            py = rev_y ? by : ty; qy = rev_y ? ty : by;
        }
        // Select the room, switch to drag mode (a rectangle from the press to the release), let a turn pass, then
        // press at one corner, hold at the other, and release there. The slabs are then built by the engine, one
        // per turn, with no further packets.
        step_action(&steps[n], PckA_SetPlyrState, PSt_BuildRoom, rkind);
        steps[n++].wait_build_free = true;
        step_action(&steps[n++], PckA_SetRoomspaceDrag, 0, 0);
        step_idle(&steps[n++]);
        step_click_ctx(&steps[n++], slab_subtile_center(px), slab_subtile_center(py), PRESS_FLAGS, 0);
        step_click_ctx(&steps[n++], slab_subtile_center(qx), slab_subtile_center(qy), PCtr_LBtnHeld, 0);
        step_click_ctx(&steps[n++], slab_subtile_center(qx), slab_subtile_center(qy), PCtr_LBtnHeld, 0);
        step_click_ctx(&steps[n++], slab_subtile_center(qx), slab_subtile_center(qy), PCtr_LBtnRelease, 0);
        break;
    }
    case ESV_MarkDig: {
        if (!verb->has_rect) return "MISSING_RECT";
        if (!slab_on_map(verb->slab_x0, verb->slab_y0) || !slab_on_map(verb->slab_x1, verb->slab_y1)) return "POSITION_OFF_MAP";
        const int64_t dw = ((verb->slab_x1 > verb->slab_x0) ? verb->slab_x1 - verb->slab_x0 : verb->slab_x0 - verb->slab_x1) + 1;
        const int64_t dh = ((verb->slab_y1 > verb->slab_y0) ? verb->slab_y1 - verb->slab_y0 : verb->slab_y0 - verb->slab_y1) + 1;
        if (dw * dh > EXTSEAT_MAX_DIG_SLABS) return "AREA_TOO_LARGE";
        // A pickaxe drag tags or untags according to its first slab (a marked one untags the whole sweep), so start on a
        // corner that is not marked; if all four are, tap the unmarked slabs one by one.
        const int64_t lx = (verb->slab_x0 < verb->slab_x1) ? verb->slab_x0 : verb->slab_x1, rx = (verb->slab_x0 < verb->slab_x1) ? verb->slab_x1 : verb->slab_x0;
        const int64_t ty = (verb->slab_y0 < verb->slab_y1) ? verb->slab_y0 : verb->slab_y1, by = (verb->slab_y0 < verb->slab_y1) ? verb->slab_y1 : verb->slab_y0;
        const int64_t cx[4] = { lx, rx, rx, lx }, cy[4] = { ty, by, ty, by };
        int corner = -1;
        for (int c = 0; (c < 4) && (corner < 0); c++) {
            if (find_from_task_list_by_slab(plyr_idx, (MapSlabCoord)cx[c], (MapSlabCoord)cy[c]) == -1) corner = c;
        }
        if (corner >= 0) {
            n = extseat_plan_dig_sweep_from(steps, EXTSEAT_MAX_STEPS, verb->slab_x0, verb->slab_y0, verb->slab_x1, verb->slab_y1, corner);
        } else {
            int64_t xy[2 * EXTSEAT_MAX_DIG_SLABS], cnt = 0;
            for (int64_t y = ty; y <= by; y++)
                for (int64_t x = lx; x <= rx; x++)
                    if (find_from_task_list_by_slab(plyr_idx, (MapSlabCoord)x, (MapSlabCoord)y) == -1) { xy[2 * cnt] = x; xy[2 * cnt + 1] = y; cnt++; }
            if (cnt == 0) {
                // Everything is marked already: nothing to do (and a drag here would take the marks off).
                step_idle(&steps[n++]);
                break;
            }
            n = plan_slab_taps(steps, EXTSEAT_MAX_STEPS, xy, cnt, PSt_CtrlDungeon, CSt_PickAxe);
        }
        if (n == 0) return "AREA_TOO_LARGE";
        break;
    }
    case ESV_UnmarkDig: {
        // A pickaxe tap on a marked slab takes its mark off (the tap's first slab is marked, so it untags).
        if (!verb->has_rect) return "MISSING_RECT";
        if (!slab_on_map(verb->slab_x0, verb->slab_y0) || !slab_on_map(verb->slab_x1, verb->slab_y1)) return "POSITION_OFF_MAP";
        const int64_t lx = (verb->slab_x0 < verb->slab_x1) ? verb->slab_x0 : verb->slab_x1, rx = (verb->slab_x0 < verb->slab_x1) ? verb->slab_x1 : verb->slab_x0;
        const int64_t ty = (verb->slab_y0 < verb->slab_y1) ? verb->slab_y0 : verb->slab_y1, by = (verb->slab_y0 < verb->slab_y1) ? verb->slab_y1 : verb->slab_y0;
        if ((rx - lx + 1) * (by - ty + 1) > EXTSEAT_MAX_DIG_SLABS) return "AREA_TOO_LARGE";
        int64_t xy[2 * EXTSEAT_MAX_DIG_SLABS], cnt = 0;
        for (int64_t y = ty; y <= by; y++)
            for (int64_t x = lx; x <= rx; x++)
                if (find_from_task_list_by_slab(plyr_idx, (MapSlabCoord)x, (MapSlabCoord)y) != -1) { xy[2 * cnt] = x; xy[2 * cnt + 1] = y; cnt++; }
        if (cnt == 0) return "NOTHING_MARKED";
        n = plan_slab_taps(steps, EXTSEAT_MAX_STEPS, xy, cnt, PSt_CtrlDungeon, CSt_PickAxe);
        if (n == 0) return "AREA_TOO_LARGE";
        break;
    }
    case ESV_SetDoorLock: {
        // A click on one of your own doors toggles its lock, as a human's click does (the door-key cursor).
        if (!verb->has_pos) return "MISSING_POSITION";
        if (!verb->has_enabled) return "MISSING_ENABLED";
        if (!stl_on_map(verb->stl_x, verb->stl_y)) return "POSITION_OFF_MAP";
        struct Thing *door = get_door_for_position((MapSubtlCoord)verb->stl_x, (MapSubtlCoord)verb->stl_y);
        if (thing_is_invalid(door)) return "NO_DOOR";
        if (door->owner != plyr_idx) return "NOT_YOURS";
        if ((door->door.is_locked != 0) == (verb->enabled != 0)) return "ALREADY_SET";
        step_action(&steps[n++], PckA_SetPlyrState, PSt_CtrlDungeon, 0);
        step_idle(&steps[n++]);
        step_click_ctx(&steps[n++], door->mappos.x.stl.num, door->mappos.y.stl.num, PRESS, CSt_DoorKey);
        step_click_ctx(&steps[n++], door->mappos.x.stl.num, door->mappos.y.stl.num, LET_GO, CSt_DoorKey);
        break;
    }
    case ESV_PowerOff: {
        const int64_t pwkind = power_model_id(verb->name);
        if (pwkind < 1) return "UNKNOWN_KIND";
        if (pwkind == PwrK_CALL2ARMS) {
            if (!player_uses_power_call_to_arms(plyr_idx)) return "NOT_ACTIVE";
            step_action(&steps[n++], PckA_PwrCTADis, 0, 0);
        } else if (pwkind == PwrK_SIGHT) {
            if (!player_uses_power_sight(plyr_idx)) return "NOT_ACTIVE";
            step_action(&steps[n++], PckA_PwrSOEDis, 0, 0);
        } else if (pwkind == PwrK_OBEY) {
            // Must Obey switches off by being cast again, as the panel button does.
            if (!player_uses_power_obey(plyr_idx)) return "NOT_ACTIVE";
            step_action(&steps[n++], PckA_UsePwrObey, 0, 0);
        } else {
            return "UNSUPPORTED_POWER";
        }
        break;
    }
    case ESV_UseSpecial: {
        // A special box the seat's hand could reach (what a human needs to pick it up and use it). Resurrect and
        // transfer open a chooser for a human; here the choice comes with the order.
        if (!verb->has_thing) return "MISSING_THING";
        struct Thing *box = thing_get((ThingIndex)verb->thing_id);
        if (thing_is_invalid(box) || !thing_is_special_box(box)) return "NO_SUCH_THING";
        if (!can_thing_be_picked_up_by_player(box, plyr_idx)) return "CANNOT_REACH";
        const SpecialKind spk = box_thing_to_special(box);
        if (spk == SpcKind_Resurrect) {
            const int64_t model = creature_model_id(verb->name);
            if (model < 1) return "MISSING_CHOICE";
            if ((verb->level < 1) || (verb->level > CREATURE_MAX_LEVEL)) return "BAD_LEVEL";
            TbBool found = false;
            for (int64_t i = 0; (i < dungeon->dead_creatures_count) && (i < DEAD_CREATURES_MAX_COUNT); i++) {
                if ((dungeon->dead_creatures[i].model == model) && (dungeon->dead_creatures[i].exp_level == verb->level - 1)) found = true;
            }
            if (!found) return "NO_SUCH_DEAD";
            step_action(&steps[n++], PckA_ResurrectCrtr, box->index, (plyr_idx & 0x0F) | ((model & 0xFF) << 4) | (((verb->level - 1) & 0x0F) << 12));
        } else if (spk == SpcKind_TrnsfrCrtr) {
            if (!verb->has_target_thing) return "MISSING_CHOICE";
            const struct Thing *cr = creature_thing(verb->target_thing);
            if (cr == NULL) return "NO_SUCH_THING";
            if (cr->owner != plyr_idx) return "NOT_YOURS";
            step_action(&steps[n++], PckA_TransferCreatr, box->index, cr->index);
        } else {
            step_action(&steps[n++], PckA_UseSpecialBox, box->index, 0);
        }
        break;
    }
    case ESV_SendMessage: {
        // Short messages only, never a game command (a message starting with the command character would run one).
        const size_t len = strlen(verb->message);
        if ((len < 1) || (len > EXTSEAT_CHAT_MAX_LEN)) return "BAD_MESSAGE";
        if (verb->message[0] == cmd_char) return "BAD_MESSAGE";
        for (size_t i = 0; i < len; i++) if (((unsigned char)verb->message[i] < 32) || ((unsigned char)verb->message[i] > 126)) return "BAD_MESSAGE";
        step_action(&steps[n], PckA_PlyrMsgEnd, 0, 0);
        steps[n++].send_chat = true;
        break;
    }
    case ESV_Sell: {
        int64_t x0, y0, x1, y1;
        if (verb->has_rect) {
            x0 = verb->slab_x0; y0 = verb->slab_y0; x1 = verb->slab_x1; y1 = verb->slab_y1;
        } else if (verb->has_pos) {
            x0 = x1 = subtile_slab(verb->stl_x); y0 = y1 = subtile_slab(verb->stl_y);
        } else {
            return "MISSING_RECT";
        }
        if (!slab_on_map(x0, y0) || !slab_on_map(x1, y1)) return "POSITION_OFF_MAP";
        const int64_t lx = (x0 < x1) ? x0 : x1, rx = (x0 < x1) ? x1 : x0, ty = (y0 < y1) ? y0 : y1, by = (y0 < y1) ? y1 : y0;
        if ((rx - lx + 1) * (by - ty + 1) > EXTSEAT_MAX_SELL_SLABS) return "AREA_TOO_LARGE";
        // A sell drag skips items in this engine (see ftest_ai_gesture_drag_verbs), so each slab of the area that is
        // the seat's own gets its own click; anything else cannot be sold and is left out.
        int64_t xy[2 * EXTSEAT_MAX_SELL_SLABS];
        int64_t count = 0;
        for (int64_t y = ty; y <= by; y++) {
            for (int64_t x = lx; x <= rx; x++) {
                if (slabmap_owner(get_slabmap_block((MapSlabCoord)x, (MapSlabCoord)y)) == plyr_idx) {
                    xy[2 * count] = x;
                    xy[2 * count + 1] = y;
                    count++;
                }
            }
        }
        if (count == 0) return "NOTHING_TO_SELL";
        n = extseat_plan_slab_clicks(steps, EXTSEAT_MAX_STEPS, xy, count);
        if (n == 0) return "AREA_TOO_LARGE";
        break;
    }
    default:
        return "UNKNOWN_VERB";
    }
    *out_n = n;
    return NULL;
}

/** Starts the next waiting verb if the running gesture is done. Called each turn before a step is written. */
static void promote_verbs(NetUserId user)
{
    struct ExtSeatQueue *q = &s_queue[user];
    if ((q->next >= q->count) && (q->cur_id != 0)) {
        record_result(user, q->cur_id, q->cur_error);
        q->cur_id = 0;
        q->cur_error = NULL;
        q->step_wait = 0;
        q->hand_dropped = 0;
    }
    while ((q->next >= q->count) && (q->fifo_count > 0)) {
        const struct ExtSeatVerb verb = q->fifo[q->fifo_head];
        const int64_t id = q->fifo_id[q->fifo_head];
        q->fifo_head = (q->fifo_head + 1) % EXTSEAT_MAX_QUEUED_VERBS;
        q->fifo_count--;
        if ((verb.expires_turn > 0) && ((int64_t)get_gameturn() > verb.expires_turn)) {
            record_result(user, id, "EXPIRED");
            continue;
        }
        struct ExtSeatStep steps[EXTSEAT_MAX_STEPS];
        int64_t n = 0;
        const char *err = plan_verb(get_net_user_player_number(user), &verb, steps, &n);
        if (err != NULL) {
            record_result(user, id, err);
            continue;
        }
        memcpy(q->steps, steps, sizeof(struct ExtSeatStep) * n);
        q->count = n;
        q->next = 0;
        q->cur_id = id;
        q->cur_verb = verb;
        q->cur_error = NULL;
        q->step_wait = 0;
        q->hand_dropped = 0;
        note_order_started(user, get_net_user_player_number(user), &verb);
    }
}

const char *extseat_submit_verb_ex(NetUserId user, PlayerNumber plyr_idx, const struct ExtSeatVerb *verb, TbBool queue, struct ExtSeatSubmitInfo *info)
{
    if (!user_ok(user) || (get_net_user_player_number(user) != plyr_idx)) {
        return "NOT_A_VALID_SEAT";
    }
    struct ExtSeatQueue *q = &s_queue[user];
    if (verb->kind == ESV_Cancel) {
        const int64_t dropped = extseat_cancel(user);
        if (info != NULL) {
            info->steps = dropped;
            info->id = 0;
            info->queued_behind = 0;
        }
        return NULL;
    }
    if ((verb->expires_turn > 0) && ((int64_t)get_gameturn() > verb->expires_turn)) {
        return "STALE_VIEW";
    }
    const TbBool busy = !extseat_idle(user);
    if (busy && !queue) {
        return "ACTION_ALREADY_QUEUED";
    }
    if (busy && (q->fifo_count >= EXTSEAT_MAX_QUEUED_VERBS)) {
        return "QUEUE_FULL";
    }
    struct ExtSeatStep steps[EXTSEAT_MAX_STEPS];
    int64_t n = 0;
    const char *err = plan_verb(plyr_idx, verb, steps, &n);
    if (err == NULL) err = check_chat_rate(user, verb); // a bad message says so whenever it is sent
    if (err != NULL) {
        return err;
    }
    const int64_t id = ++q->next_id;
    if (verb->kind == ESV_SendMessage) q->last_chat_turn = (int64_t)get_gameturn(); // counts from acceptance: a batch cannot spam
    int64_t behind = 0;
    if (busy) {
        const int64_t slot = (q->fifo_head + q->fifo_count) % EXTSEAT_MAX_QUEUED_VERBS;
        q->fifo[slot] = *verb;
        q->fifo_id[slot] = id;
        q->fifo_count++;
        behind = q->fifo_count + ((q->next < q->count) ? 1 : 0) - 1;
    } else {
        if (!extseat_enqueue(user, steps, n)) {
            return "ACTION_ALREADY_QUEUED";
        }
        q->cur_id = id;
        q->cur_verb = *verb;
        q->cur_error = NULL;
        q->step_wait = 0;
        q->hand_dropped = 0;
        note_order_started(user, plyr_idx, verb);
    }
    if (info != NULL) {
        info->steps = n;
        info->id = id;
        info->queued_behind = behind;
    }
    return NULL;
}

const char *extseat_submit_verb(NetUserId user, PlayerNumber plyr_idx, const struct ExtSeatVerb *verb, int64_t *out_steps)
{
    struct ExtSeatSubmitInfo info = {0};
    const char *err = extseat_submit_verb_ex(user, plyr_idx, verb, false, &info);
    if ((err == NULL) && (out_steps != NULL)) {
        *out_steps = info.steps;
    }
    return err;
}

const char *extseat_check_verb(NetUserId user, PlayerNumber plyr_idx, const struct ExtSeatVerb *verb, int64_t *out_steps)
{
    if (!user_ok(user) || (get_net_user_player_number(user) != plyr_idx)) {
        return "NOT_A_VALID_SEAT";
    }
    if (verb->kind == ESV_Cancel) {
        if (out_steps != NULL) {
            *out_steps = 0;
        }
        return NULL; // a dry-run cancel is trivially "fine": the real one always succeeds, never mutates anything to check
    }
    if ((verb->expires_turn > 0) && ((int64_t)get_gameturn() > verb->expires_turn)) {
        return "STALE_VIEW";
    }
    struct ExtSeatStep steps[EXTSEAT_MAX_STEPS];
    int64_t n = 0;
    const char *err = plan_verb(plyr_idx, verb, steps, &n);
    if (err == NULL) err = check_chat_rate(user, verb); // a bad message says so whenever it is sent
    if (err != NULL) {
        return err;
    }
    if (out_steps != NULL) {
        *out_steps = n;
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif
