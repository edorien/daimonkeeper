#include "pre_inc.h"
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "api_seat_decision.h"
#include "bflib_basics.h"
#include "config_creature.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "map_events.h"
#include "player_data.h"
#include "thing_list.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_REASONS 8
#define REASON_LEN 40
#define MODEL_BITS 64

struct SeatDecision {
    TbBool initialised;
    int64_t last_quarter;
    TbBool event_seen[EVENTS_COUNT];
    unsigned char event_kind[EVENTS_COUNT];
    TbBool kind_owned[MODEL_BITS];
    char pending[MAX_REASONS][REASON_LEN];
    int pending_count;
    int64_t last_emit_turn;
    int64_t seq;
    int64_t latest_turn;
    char latest[MAX_REASONS][REASON_LEN];
    int latest_count;
};

static struct SeatDecision s_seat[PLAYERS_COUNT];
static int64_t s_min_interval = 100;

void api_seat_decision_set_min_interval(int64_t turns)
{
    s_min_interval = (turns < 0) ? 0 : turns;
}

void api_seat_decision_reset(void)
{
    memset(s_seat, 0, sizeof(s_seat));
}

static void add_pending(struct SeatDecision *d, const char *reason)
{
    for (int i = 0; i < d->pending_count; i++) {
        if (strcmp(d->pending[i], reason) == 0) return;
    }
    if (d->pending_count < MAX_REASONS) {
        snprintf(d->pending[d->pending_count++], REASON_LEN, "%s", reason);
    }
}

static const char *event_reason(unsigned char kind)
{
    switch (kind) {
    case EvKind_HeartAttacked: return "heart_attacked";
    case EvKind_EnemyFight: return "enemy_fight";
    case EvKind_RoomUnderAttack: return "room_under_attack";
    case EvKind_Breach: return "breach";
    case EvKind_RoomLost: return "room_lost";
    default: return NULL;
    }
}

static int64_t quarter_of(PlayerNumber p)
{
    const int64_t gap = (int64_t)kfx_config_state.conf.rules[p].gameplay.pay_day_gap;
    const int64_t progress = (int64_t)kfx_config_state.pay_day_progress[p];
    return (gap > 0) ? (4 * progress / gap) % 4 : 0;
}

static void owned_kinds(PlayerNumber p, TbBool *out)
{
    memset(out, 0, sizeof(TbBool) * MODEL_BITS);
    const struct Dungeon *dungeon = get_players_dungeon(get_player(p));
    if (dungeon_invalid(dungeon)) return;
    for (int pass = 0; pass < 2; pass++) {
        int64_t idx = (pass == 0) ? dungeon->creatr_list_start : dungeon->digger_list_start;
        for (int guard = 0; (idx > 0) && (guard < 2000); guard++) {
            const struct Thing *thing = thing_get((ThingIndex)idx);
            if (thing_is_invalid(thing) || !thing_is_creature(thing)) break;
            if ((thing->model > 0) && (thing->model < MODEL_BITS)) out[thing->model] = true;
            const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
            idx = cctrl->players_next_creature_idx;
        }
    }
}

static void emit(PlayerNumber p, struct SeatDecision *d)
{
    d->seq++;
    d->latest_turn = (int64_t)get_gameturn();
    d->latest_count = d->pending_count;
    memcpy(d->latest, d->pending, sizeof(d->latest));
    char joined[MAX_REASONS * REASON_LEN];
    joined[0] = 0;
    for (int i = 0; i < d->pending_count; i++) {
        if (i > 0) strncat(joined, ",", sizeof(joined) - strlen(joined) - 1);
        strncat(joined, d->pending[i], sizeof(joined) - strlen(joined) - 1);
    }
    d->pending_count = 0;
    d->last_emit_turn = d->latest_turn;
    struct ApiEventData data[5];
    memset(data, 0, sizeof(data));
    data[0].name = "player"; data[0].type = API_EVENT_DATA_INT32; data[0].value.int32_value = p;
    data[1].name = "seq"; data[1].type = API_EVENT_DATA_INT64; data[1].value.int64_value = d->seq;
    data[2].name = "turn"; data[2].type = API_EVENT_DATA_INT64; data[2].value.int64_value = d->latest_turn;
    data[3].name = "reasons"; data[3].type = API_EVENT_DATA_STRING; data[3].value.string_value = joined;
    data[4].name = "quarter"; data[4].type = API_EVENT_DATA_INT32; data[4].value.int32_value = quarter_of(p);
    api_event_with_data("DECISION_DUE", data, 5);
}

static void tick_seat(PlayerNumber p)
{
    struct SeatDecision *d = &s_seat[p];
    const int64_t now = (int64_t)get_gameturn();
    TbBool kinds[MODEL_BITS];
    owned_kinds(p, kinds);
    if (!d->initialised || (now < d->last_emit_turn)) {
        // First look at this seat (or the clock went backwards: a load): take the baseline, raise nothing.
        memset(d, 0, sizeof(*d));
        d->initialised = true;
        d->last_quarter = quarter_of(p);
        memcpy(d->kind_owned, kinds, sizeof(kinds));
        for (int i = 0; i < EVENTS_COUNT; i++) {
            const struct Event *ev = &kfx_sim_state.event[i];
            d->event_seen[i] = flag_is_set(ev->flags, EvF_Exists) && (ev->owner == p);
            d->event_kind[i] = ev->kind;
        }
        d->last_emit_turn = now;
        return;
    }
    const int64_t q = quarter_of(p);
    if (q != d->last_quarter) {
        char r[REASON_LEN];
        snprintf(r, sizeof(r), (q == 0) ? "payday" : "quarter_%d", (int)q);
        add_pending(d, r);
        d->last_quarter = q;
    }
    for (int i = 0; i < EVENTS_COUNT; i++) {
        const struct Event *ev = &kfx_sim_state.event[i];
        const TbBool exists = flag_is_set(ev->flags, EvF_Exists) && (ev->owner == p);
        if (exists && (!d->event_seen[i] || (d->event_kind[i] != ev->kind))) {
            const char *why = event_reason(ev->kind);
            if (why != NULL) add_pending(d, why);
        }
        d->event_seen[i] = exists;
        d->event_kind[i] = ev->kind;
    }
    for (int m = 1; m < MODEL_BITS; m++) {
        if (kinds[m] && !d->kind_owned[m]) {
            char r[REASON_LEN];
            snprintf(r, sizeof(r), "new_kind_%s", creature_code_name((ThingModel)m));
            add_pending(d, r);
        }
        d->kind_owned[m] = kinds[m];
    }
    if ((d->pending_count > 0) && (now - d->last_emit_turn >= s_min_interval)) {
        emit(p, d);
    }
}

void api_seat_decision_tick(void)
{
    if (kfx_sim_state.game_kind != GKind_LocalGame) return;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        const struct PlayerInfo *pl = get_player(p);
        if (!player_exists(pl) || !flag_is_set(pl->allocflags, PlaF_ExternalSeat)) {
            s_seat[p].initialised = false; // a slot that stops being a seat starts over when it becomes one again
            continue;
        }
        tick_seat(p);
    }
}

void api_seat_decision_add_to_view(VALUE *seat, PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT)) return;
    const struct SeatDecision *d = &s_seat[plyr_idx];
    VALUE *dec = value_dict_add(seat, "decision");
    value_init_dict(dec);
    value_init_int64(value_dict_add(dec, "seq"), d->seq);
    value_init_int64(value_dict_add(dec, "turn"), d->latest_turn);
    VALUE *arr = value_dict_add(dec, "reasons");
    value_init_array(arr);
    for (int i = 0; i < d->latest_count; i++) value_init_string(value_array_append(arr), d->latest[i]);
    value_init_int32(value_dict_add(dec, "pending"), d->pending_count);
}

#ifdef __cplusplus
}
#endif
