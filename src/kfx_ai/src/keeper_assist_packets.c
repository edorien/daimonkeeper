/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file keeper_assist_packets.c
 *     The combat assistant's rule packets (keeper_assist_packets.h).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "keeper_assist_packets.h"

#include <string.h>

#include "bflib_basics.h"
#include "keeper_assist_state.h"
#include "packet_data.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// PckA_AssistStageRule par1: slot 4 bits, action 2, target 2, pick 3, cond_count 3, level 4, power 12 (bits 0-29).
int32_t keeper_assist_pack_rule(int slot, const struct KeeperAssistRule *r, int32_t *par2)
{
    const uint32_t p1 = ((uint32_t)slot & 0xF) | (((uint32_t)r->action & 0x3) << 4) | (((uint32_t)r->target & 0x3) << 6)
        | (((uint32_t)r->pick & 0x7) << 8) | (((uint32_t)r->cond_count & 0x7) << 11) | (((uint32_t)r->level & 0xF) << 14)
        | (((uint32_t)r->power & 0xFFF) << 18);
    *par2 = (int32_t)((uint32_t)r->cooldown | ((uint32_t)r->target_cooldown << 16));
    return (int32_t)p1;
}

void keeper_assist_unpack_rule(int32_t par1, int32_t par2, int *slot, struct KeeperAssistRule *r)
{
    const uint32_t p1 = (uint32_t)par1, p2 = (uint32_t)par2;
    memset(r, 0, sizeof(*r));
    *slot = (int)(p1 & 0xF);
    r->used = 1;
    r->action = (uint8_t)((p1 >> 4) & 0x3);
    r->target = (uint8_t)((p1 >> 6) & 0x3);
    r->pick = (uint8_t)((p1 >> 8) & 0x7);
    r->cond_count = (uint8_t)((p1 >> 11) & 0x7);
    r->level = (uint8_t)((p1 >> 14) & 0xF);
    r->power = (uint16_t)((p1 >> 18) & 0xFFF);
    r->cooldown = (uint16_t)(p2 & 0xFFFF);
    r->target_cooldown = (uint16_t)(p2 >> 16);
}

static void emit(struct KeeperAssistPacket *out, int *n, uint8_t action, int32_t par1, int32_t par2)
{
    out[*n].action = action;
    out[*n].par1 = par1;
    out[*n].par2 = par2;
    (*n)++;
}

static int32_t global_value(const struct KeeperAssistGlobals *g, enum KeeperAssistGlobalField f)
{
    switch (f) {
    case KAG_Enabled: return g->enabled;
    case KAG_Interval: return g->interval;
    case KAG_ChargeTime: return g->charge_time;
    case KAG_MinGap: return g->min_gap;
    case KAG_Reserve: return g->reserve;
    case KAG_MaxSpend: return g->max_spend;
    default: return 0;
    }
}

static void set_global(struct KeeperAssistGlobals *g, int32_t f, int32_t v)
{
    switch (f) {
    case KAG_Enabled: g->enabled = (v != 0); break;
    case KAG_Interval: g->interval = (uint8_t)((v < 0) ? 0 : ((v > 255) ? 255 : v)); break;
    case KAG_ChargeTime: g->charge_time = (v != 0); break;
    case KAG_MinGap: g->min_gap = (uint16_t)((v < 0) ? 0 : ((v > 65535) ? 65535 : v)); break;
    case KAG_Reserve: g->reserve = v; break;
    case KAG_MaxSpend: g->max_spend = v; break;
    default: break;
    }
}

int keeper_assist_plan_packets(const struct KeeperAssistState *st, const struct KeeperAssistRule *rules,
    const struct KeeperAssistGlobals *globals, struct KeeperAssistPacket *out)
{
    int n = 0;
    uint32_t keep = 0, changed = 0, live = 0;
    for (int s = 0; s < KEEPER_ASSIST_RULES; s++) {
        if (st->live[s].used) live |= 1u << s;
        const struct KeeperAssistRule *r = &rules[s];
        if (!r->used) continue;
        keep |= 1u << s;
        if (st->live[s].used && (memcmp(r, &st->live[s], sizeof(*r)) == 0)) continue;
        changed |= 1u << s;
        int32_t p2 = 0;
        const int32_t p1 = keeper_assist_pack_rule(s, r, &p2);
        emit(out, &n, PckA_AssistStageRule, p1, p2);
        emit(out, &n, PckA_AssistStageRuleMoney, (int32_t)((uint32_t)s | ((uint32_t)(r->max_allies_hit & 0xF) << 4)), r->budget);
        for (int c = 0; (c < r->cond_count) && (c < KEEPER_ASSIST_CONDS); c++) {
            const struct KeeperAssistCond *cond = &r->cond[c];
            emit(out, &n, PckA_AssistStageCond,
                 (int32_t)((uint32_t)s | ((uint32_t)c << 4) | ((uint32_t)cond->fact << 6) | (((uint32_t)cond->op & 0xF) << 14)), cond->value);
        }
    }
    const TbBool globals_changed = (memcmp(globals, &st->globals, sizeof(*globals)) != 0);
    if (globals_changed) {
        for (int f = KAG_None + 1; f < KAG_Count; f++) {
            emit(out, &n, PckA_AssistStageGlobal, f, global_value(globals, (enum KeeperAssistGlobalField)f));
        }
    }
    if ((changed == 0) && !globals_changed && (keep == live)) {
        return 0;
    }
    emit(out, &n, PckA_AssistCommit, (int32_t)(keep | (changed << 16)), globals_changed ? 1 : 0);
    return n;
}

static void clear_slot(struct KeeperAssistState *st, int s)
{
    memset(&st->live[s], 0, sizeof(st->live[s]));
    memset(&st->run[s], 0, sizeof(st->run[s]));
    if (st->pending.used && (st->pending.slot == s)) st->pending.used = 0;
}

void keeper_assist_apply_packet(struct KeeperAssistState *st, uint8_t action, int32_t par1, int32_t par2)
{
    const uint32_t p1 = (uint32_t)par1;
    switch (action) {
    case PckA_AssistStageRule: {
        int slot = 0;
        struct KeeperAssistRule r;
        keeper_assist_unpack_rule(par1, par2, &slot, &r);
        st->staging[slot] = r;
        break;
    }
    case PckA_AssistStageRuleMoney: {
        struct KeeperAssistRule *r = &st->staging[p1 & 0xF];
        r->max_allies_hit = (uint8_t)((p1 >> 4) & 0xF);
        r->budget = par2;
        break;
    }
    case PckA_AssistStageCond: {
        const uint32_t idx = (p1 >> 4) & 0x3;
        struct KeeperAssistCond *c = &st->staging[p1 & 0xF].cond[idx];
        c->fact = (uint8_t)((p1 >> 6) & 0xFF);
        c->op = (uint8_t)((p1 >> 14) & 0xF);
        c->value = par2;
        break;
    }
    case PckA_AssistStageGlobal:
        set_global(&st->staging_globals, par1, par2);
        break;
    case PckA_AssistCommit: {
        const uint32_t keep = p1 & 0xFFFF, changed = (p1 >> 16) & 0xFFFF;
        for (int s = 0; s < KEEPER_ASSIST_RULES; s++) {
            if ((keep & (1u << s)) == 0) {
                clear_slot(st, s);
            } else if ((changed & (1u << s)) != 0) {
                clear_slot(st, s);
                if (st->staging[s].used && (keeper_assist_rule_validate(&st->staging[s]) == NULL)) {
                    st->live[s] = st->staging[s];
                } else {
                    WARNLOG("Combat assistant: rule %d dropped at the commit (%s)", s,
                            st->staging[s].used ? keeper_assist_rule_validate(&st->staging[s]) : "not staged");
                }
            }
        }
        if (par2 & 1) {
            if (keeper_assist_globals_validate(&st->staging_globals) == NULL) {
                st->globals = st->staging_globals;
            } else {
                WARNLOG("Combat assistant: globals out of range at the commit; the old ones stay");
            }
        }
        break;
    }
    case PckA_AssistEnable:
        st->globals.enabled = (par1 != 0);
        st->staging_globals.enabled = st->globals.enabled;
        break;
    default:
        break;
    }
}

#ifdef __cplusplus
}
#endif
