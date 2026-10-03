#include "pre_inc.h"
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "api_seat_decision.h"
#include "bflib_basics.h"
#include "config_creature.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "config_rules.h"
#include "config_magic.h"
#include "config.h"
#include "creature_states_gardn.h"
#include "creature_states.h"
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
#define OBJECTIVE_HISTORY 5
#define CUSTODY_TRACKED 64
#define SACRIFICE_OUTCOMES 5

struct SacrificeOutcome {
    char text[160];
    int64_t turn;
};

/** A prisoner as last seen, to tell what changed: arrived, went hungry, hurt on the rack, broke (points fell back),
 *  converted to the seat, or gone (escaped, freed, died). */
struct CustodySeen {
    ThingIndex id;
    GameTurn creation_turn;
    TbBool hungry;
    TbBool hurt;
    int64_t torture_points;
};

struct ObjectiveSeen {
    char text[MESSAGE_TEXT_LEN];
    int64_t turn;
};

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
    unsigned char last_victory;  /**< victory_state at the last look: a change is urgent (victory / defeat) */
    TbBool urgent;               /**< a pending reason that must not wait out the minimum interval */
    /** The level's objectives as they appeared, oldest first. The sim keeps only the latest
     *  (evntbox_text_objective), so one replaced while the agent was busy would otherwise be lost. */
    struct ObjectiveSeen objectives[OBJECTIVE_HISTORY];
    int objective_count;
    struct CustodySeen custody[CUSTODY_TRACKED];
    int custody_count;
    int64_t chat_seen;                      /**< chat messages delivered when last looked */
    int64_t bodies_toward_vampire;          /**< falls when a vampire rises from the graveyard */
    unsigned char offered[CREATURE_TYPES_MAX]; /**< creature_sacrifice[] as last seen: a recipe firing resets its victims' */
    struct SacrificeOutcome outcomes[SACRIFICE_OUTCOMES];
    int outcome_count;
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
    case EvKind_Information:
    case EvKind_QuickInformation: return "information";
    case EvKind_DnSpecialFound: return "special_found";
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

static const char *victory_code(unsigned char state)
{
    switch (state) {
    case VicS_WonLevel: return "won";
    case VicS_LostLevel: return "lost";
    case VicS_Undecided: return "undecided";
    default: return "other";
    }
}

/** Records the seat's objective if it changed since the last one seen; true if it did. */
static TbBool note_objective(PlayerNumber p, struct SeatDecision *d, int64_t now)
{
    const char *text = kfx_sim_state.evntbox_text_objective[p];
    if (text[0] == 0) return false;
    if ((d->objective_count > 0) && (strcmp(d->objectives[d->objective_count - 1].text, text) == 0)) return false;
    if (d->objective_count == OBJECTIVE_HISTORY) {
        memmove(&d->objectives[0], &d->objectives[1], sizeof(d->objectives[0]) * (OBJECTIVE_HISTORY - 1));
        d->objective_count--;
    }
    struct ObjectiveSeen *o = &d->objectives[d->objective_count++];
    snprintf(o->text, sizeof(o->text), "%s", text);
    o->turn = now;
    return true;
}

/** The seat's prisoners now; returns how many (at most CUSTODY_TRACKED). */
static int custody_now(PlayerNumber p, struct CustodySeen *out)
{
    int n = 0;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int guard = 0; (idx > 0) && (guard < 4000) && (n < CUSTODY_TRACKED); guard++) {
        const struct Thing *thing = thing_get((ThingIndex)idx);
        if (thing_is_invalid(thing)) break;
        idx = thing->next_of_class;
        if (!thing_is_creature(thing) || (thing->owner == p) || !creature_is_kept_in_custody_by_player(thing, p)) continue;
        const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
        struct CustodySeen *c = &out[n++];
        c->id = thing->index;
        c->creation_turn = thing->creation_turn;
        c->hungry = hunger_is_creature_hungry(thing);
        const TbBool tortured = creature_is_being_tortured(thing);
        c->hurt = tortured && (cctrl->max_health > 0) && (3 * thing->health < cctrl->max_health);
        c->torture_points = tortured ? cctrl->tortured.accumulated_torture_points : 0;
    }
    return n;
}

static void note_custody(PlayerNumber p, struct SeatDecision *d, TbBool baseline)
{
    struct CustodySeen now[CUSTODY_TRACKED];
    const int n = custody_now(p, now);
    if (!baseline) {
        for (int i = 0; i < n; i++) {
            const struct CustodySeen *was = NULL;
            for (int k = 0; k < d->custody_count; k++) {
                if ((d->custody[k].id == now[i].id) && (d->custody[k].creation_turn == now[i].creation_turn)) { was = &d->custody[k]; break; }
            }
            if (was == NULL) { add_pending(d, "new_prisoner"); continue; }
            if (now[i].hungry && !was->hungry) add_pending(d, "prisoner_hungry");
            if (now[i].hurt && !was->hurt) add_pending(d, "torture_victim_hurt");
            if ((was->torture_points > 0) && (now[i].torture_points < was->torture_points)) add_pending(d, "prisoner_interrogated");
        }
        for (int k = 0; k < d->custody_count; k++) {
            TbBool still = false;
            for (int i = 0; i < n; i++) if ((now[i].id == d->custody[k].id) && (now[i].creation_turn == d->custody[k].creation_turn)) still = true;
            if (still) continue;
            const struct Thing *thing = thing_get(d->custody[k].id);
            const TbBool ours = !thing_is_invalid(thing) && thing_is_creature(thing) && (thing->creation_turn == d->custody[k].creation_turn)
                && (thing->owner == p);
            add_pending(d, ours ? "prisoner_converted" : "prisoner_gone");
        }
    }
    memcpy(d->custody, now, sizeof(now[0]) * (size_t)n);
    d->custody_count = n;
}

void api_seat_recipe_text(const struct SacrificeRecipe *sac, char *buf, size_t len)
{
    const char *act = get_conf_parameter_text(rules_sacrifices_commands, sac->action);
    char param[64];
    switch (sac->action) {
    case SacA_MkCreature:
    case SacA_MkGoodHero: snprintf(param, sizeof(param), "%s", creature_code_name((ThingModel)sac->param)); break;
    case SacA_NegSpellAll:
    case SacA_PosSpellAll: snprintf(param, sizeof(param), "%s", spell_code_name((SpellKind)sac->param)); break;
    case SacA_NegUniqFunc:
    case SacA_PosUniqFunc: snprintf(param, sizeof(param), "%s", get_conf_parameter_text(sacrifice_unique_desc, sac->param)); break;
    default: snprintf(param, sizeof(param), "flag %" PRId64, (int64_t)sac->param); break;
    }
    snprintf(buf, len, "%s %s <-", act, param);
    for (int i = 0; i < MAX_SACRIFICE_VICTIMS; i++) {
        if (sac->victims[i] > 0) str_appendf(buf, len, " %s", creature_code_name(sac->victims[i]));
    }
}

/** A recipe fires, its victims' offered counts are reset (creature_sacrifice_reset): find which, and say what it did. */
static void note_sacrifices(PlayerNumber p, struct SeatDecision *d, TbBool baseline, int64_t now)
{
    const struct Dungeon *dungeon = get_players_dungeon(get_player(p));
    if (dungeon_invalid(dungeon)) return;
    if (!baseline) {
        TbBool fell[CREATURE_TYPES_MAX];
        TbBool any = false;
        for (int m = 0; m < CREATURE_TYPES_MAX; m++) {
            fell[m] = dungeon->creature_sacrifice[m] < d->offered[m];
            any = any || fell[m];
        }
        if (any) {
            for (int r = 0; r < MAX_SACRIFICE_RECIPES; r++) {
                const struct SacrificeRecipe *sac = &kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[r];
                if (sac->action == SacA_None) continue;
                TbBool match = true, has_victim = false;
                for (int i = 0; i < MAX_SACRIFICE_VICTIMS; i++) {
                    if (sac->victims[i] <= 0) continue;
                    has_victim = true;
                    if ((sac->victims[i] >= CREATURE_TYPES_MAX) || !fell[sac->victims[i]]) { match = false; break; }
                }
                if (!match || !has_victim) continue;
                if (d->outcome_count == SACRIFICE_OUTCOMES) {
                    memmove(&d->outcomes[0], &d->outcomes[1], sizeof(d->outcomes[0]) * (SACRIFICE_OUTCOMES - 1));
                    d->outcome_count--;
                }
                struct SacrificeOutcome *o = &d->outcomes[d->outcome_count++];
                api_seat_recipe_text(sac, o->text, sizeof(o->text));
                o->turn = now;
                add_pending(d, "sacrifice_result");
                break;
            }
        }
        if (dungeon->bodies_rotten_for_vampire < d->bodies_toward_vampire) add_pending(d, "vampire_raised");
    }
    memcpy(d->offered, dungeon->creature_sacrifice, sizeof(d->offered));
    d->bodies_toward_vampire = dungeon->bodies_rotten_for_vampire;
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
    d->urgent = false;
    d->last_emit_turn = d->latest_turn;
    struct ApiEventData data[6];
    memset(data, 0, sizeof(data));
    data[0].name = "player"; data[0].type = API_EVENT_DATA_INT32; data[0].value.int32_value = p;
    data[1].name = "seq"; data[1].type = API_EVENT_DATA_INT64; data[1].value.int64_value = d->seq;
    data[2].name = "turn"; data[2].type = API_EVENT_DATA_INT64; data[2].value.int64_value = d->latest_turn;
    data[3].name = "reasons"; data[3].type = API_EVENT_DATA_STRING; data[3].value.string_value = joined;
    data[4].name = "quarter"; data[4].type = API_EVENT_DATA_INT32; data[4].value.int32_value = quarter_of(p);
    // In the push itself, so a client that only reads pushes still learns the level is over.
    data[5].name = "victory_state"; data[5].type = API_EVENT_DATA_STRING;
    data[5].value.string_value = victory_code(get_player(p)->victory_state);
    api_event_with_data("DECISION_DUE", data, 6);
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
        d->last_victory = get_player(p)->victory_state;
        note_objective(p, d, now); // the objective the level opened with is history, not news
        note_custody(p, d, true);
        note_sacrifices(p, d, true, now);
        d->chat_seen = extseat_chat_total();
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
    if (note_objective(p, d, now)) add_pending(d, "objective");
    note_custody(p, d, false);
    note_sacrifices(p, d, false, now);
    {
        // Another player said something (the seat's own messages are not news to it).
        const int64_t total = extseat_chat_total();
        if (total > d->chat_seen) {
            struct ExtSeatChat log[EXTSEAT_CHAT_LOG];
            const int64_t n = extseat_chat_log(log, EXTSEAT_CHAT_LOG);
            const int64_t fresh = (total - d->chat_seen < n) ? total - d->chat_seen : n;
            for (int64_t i = n - fresh; i < n; i++) if (log[i].player != p) { add_pending(d, "chat"); break; }
            d->chat_seen = total;
        }
    }
    const unsigned char vic = get_player(p)->victory_state;
    if (vic != d->last_victory) {
        if (vic == VicS_WonLevel) { add_pending(d, "victory"); d->urgent = true; }
        else if (vic == VicS_LostLevel) { add_pending(d, "defeat"); d->urgent = true; }
        d->last_victory = vic;
    }
    for (int m = 1; m < MODEL_BITS; m++) {
        if (kinds[m] && !d->kind_owned[m]) {
            char r[REASON_LEN];
            snprintf(r, sizeof(r), "new_kind_%s", creature_code_name((ThingModel)m));
            add_pending(d, r);
        }
        d->kind_owned[m] = kinds[m];
    }
    // Victory and defeat end the level: they never wait out the minimum interval.
    if ((d->pending_count > 0) && (d->urgent || (now - d->last_emit_turn >= s_min_interval))) {
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

void api_seat_decision_add_sacrifice_outcomes(VALUE *sacrifices, PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT)) return;
    const struct SeatDecision *d = &s_seat[plyr_idx];
    VALUE *arr = value_dict_add(sacrifices, "outcomes");
    value_init_array(arr);
    for (int i = 0; i < d->outcome_count; i++) {
        VALUE *e = value_array_append(arr);
        value_init_dict(e);
        value_init_string(value_dict_add(e, "recipe"), d->outcomes[i].text);
        value_init_int64(value_dict_add(e, "turn"), d->outcomes[i].turn);
    }
}

void api_seat_decision_add_objectives(VALUE *own, PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT)) return;
    const struct SeatDecision *d = &s_seat[plyr_idx];
    VALUE *arr = value_dict_add(own, "objective_history");
    value_init_array(arr);
    for (int i = 0; i < d->objective_count; i++) {
        VALUE *e = value_array_append(arr);
        value_init_dict(e);
        value_init_string(value_dict_add(e, "text"), d->objectives[i].text);
        value_init_int64(value_dict_add(e, "turn"), d->objectives[i].turn);
    }
}

#ifdef __cplusplus
}
#endif
