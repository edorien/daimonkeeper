#include "pre_inc.h"
#include <string.h>

#include "api_seat_assist.h"
#include "config_magic.h"
#include "keeper_assist_state.h"
#include "kfx_sim_state.h"
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
        value_init_string(value_dict_add(e, "state"), refusal_name(run->last_refusal));
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
