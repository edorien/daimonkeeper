#include "pre_inc.h"
#include <string.h>

#include "api_seat_assist.h"
#include "config_magic.h"
#include "keeper_assist_state.h"
#include "kfx_sim_state.h"
#include "player_data.h"
#include "seat_reflexes.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static const char *refusal_name(uint8_t refusal)
{
    switch (refusal) {
    case KAR_NotAvailable: return "unavailable";
    case KAR_Budget: return "budget_spent";
    case KAR_MaxSpend: return "max_spend";
    case KAR_Reserve: return "reserve";
    case KAR_CannotAfford: return "cannot_afford";
    default: return "ok";
    }
}

void api_assist_add_to_view(VALUE *own, PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT)) return;
    const struct KeeperAssistState *st = &kfx_sim_state.keeper_assist[plyr_idx];
    VALUE *a = value_dict_add(own, "assist");
    value_init_dict(a);
    value_init_bool(value_dict_add(a, "enabled"), st->globals.enabled != 0);
    value_init_int32(value_dict_add(a, "interval"), st->globals.interval);
    value_init_bool(value_dict_add(a, "charge_time"), st->globals.charge_time != 0);
    value_init_int32(value_dict_add(a, "min_gap"), st->globals.min_gap);
    value_init_int64(value_dict_add(a, "reserve"), st->globals.reserve);
    value_init_int64(value_dict_add(a, "max_spend"), st->globals.max_spend);
    value_init_int64(value_dict_add(a, "spent_this_payday"), st->spent_this_payday);
    if (st->pending.used) {
        VALUE *p = value_dict_add(a, "pending");
        value_init_dict(p);
        value_init_int32(value_dict_add(p, "rule"), st->pending.slot);
        value_init_int64(value_dict_add(p, "target"), st->pending.target);
        value_init_int64(value_dict_add(p, "due"), (int64_t)st->pending.due);
    }
    VALUE *rules = value_dict_add(a, "rules");
    value_init_array(rules);
    for (int s = 0; s < KEEPER_ASSIST_RULES; s++) {
        const struct KeeperAssistRule *r = &st->live[s];
        if (!r->used) continue;
        const struct KeeperAssistRuleRun *run = &st->run[s];
        VALUE *e = value_array_append(rules);
        value_init_dict(e);
        value_init_int32(value_dict_add(e, "slot"), s);
        keeper_assist_rule_to_value(e, r);
        value_init_int64(value_dict_add(e, "fired"), run->fired_total);
        value_init_int64(value_dict_add(e, "spent"), run->spent_total);
        value_init_int64(value_dict_add(e, "spent_this_payday"), run->spent_this_payday);
        if (run->last_cast_turn != 0) value_init_int64(value_dict_add(e, "last_turn"), (int64_t)run->last_cast_turn);
        if (keeper_assist_action_is_hand(r->action)) {
            // A hand rule is the seat's (seat_reflexes.h): its own reason for holding back.
            const char *why = seat_reflexes_refusal(get_player(plyr_idx)->user_id, s);
            value_init_string(value_dict_add(e, "state"), why ? why : "ok");
        } else {
            value_init_string(value_dict_add(e, "state"), refusal_name(run->last_refusal));
        }
    }
    {
        // What the hand rules did: each trip, the creatures and where they went.
        struct SeatReflexAction acts[SEAT_REFLEX_LOG];
        const int64_t na = seat_reflexes_log(get_player(plyr_idx)->user_id, acts, SEAT_REFLEX_LOG);
        VALUE *rx = value_dict_add(a, "reflexes");
        value_init_array(rx);
        for (int64_t k = 0; k < na; k++) {
            VALUE *e = value_array_append(rx);
            value_init_dict(e);
            value_init_int64(value_dict_add(e, "turn"), acts[k].turn);
            value_init_int32(value_dict_add(e, "rule"), acts[k].slot);
            value_init_string(value_dict_add(e, "hand"), keeper_assist_hand_name(acts[k].action));
            value_init_int64(value_dict_add(e, "verb_id"), acts[k].verb_id);
            VALUE *th = value_dict_add(e, "things");
            value_init_array(th);
            for (int t = 0; t < acts[k].count; t++) value_init_int64(value_array_append(th), acts[k].things[t]);
            if (acts[k].y >= 0) {
                VALUE *pos = value_dict_add(e, "pos");
                value_init_array(pos);
                value_init_int64(value_array_append(pos), acts[k].x);
                value_init_int64(value_array_append(pos), acts[k].y);
            } else {
                value_init_int64(value_dict_add(e, "to_room"), acts[k].x);
            }
        }
    }
    // The banks: complete rule sets kept ready, made live in one packet (use_assist_bank).
    value_init_int32(value_dict_add(a, "active_bank"), st->active_bank);
    VALUE *banks = value_dict_add(a, "banks");
    value_init_array(banks);
    for (int b = 0; b < KEEPER_ASSIST_BANKS; b++) {
        const struct KeeperAssistBank *bk = &st->bank[b];
        if (!bk->used) continue;
        VALUE *e = value_array_append(banks);
        value_init_dict(e);
        value_init_int32(value_dict_add(e, "bank"), b + 1);
        value_init_bool(value_dict_add(e, "enabled"), bk->globals.enabled != 0);
        value_init_int64(value_dict_add(e, "reserve"), bk->globals.reserve);
        value_init_int64(value_dict_add(e, "max_spend"), bk->globals.max_spend);
        VALUE *br = value_dict_add(e, "rules");
        value_init_array(br);
        for (int s = 0; s < KEEPER_ASSIST_RULES; s++) {
            if (!bk->rules[s].used) continue;
            VALUE *re = value_array_append(br);
            value_init_dict(re);
            value_init_int32(value_dict_add(re, "slot"), s);
            keeper_assist_rule_to_value(re, &bk->rules[s]);
        }
    }
    VALUE *casts = value_dict_add(a, "casts");
    value_init_array(casts);
    const uint64_t held = (st->log_total < KEEPER_ASSIST_LOG) ? st->log_total : KEEPER_ASSIST_LOG;
    for (uint64_t i = st->log_total - held; i < st->log_total; i++) {
        const struct KeeperAssistCast *c = &st->log[i % KEEPER_ASSIST_LOG];
        VALUE *e = value_array_append(casts);
        value_init_dict(e);
        value_init_int64(value_dict_add(e, "turn"), (int64_t)c->turn);
        value_init_int32(value_dict_add(e, "rule"), c->slot);
        value_init_string(value_dict_add(e, "power"), power_code_name(c->power));
        value_init_int32(value_dict_add(e, "level"), c->level);
        value_init_int64(value_dict_add(e, "target"), c->target);
        value_init_int64(value_dict_add(e, "cost"), c->cost);
    }
}

#ifdef __cplusplus
}
#endif
