#include "pre_inc.h"
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "api_seat_view.h"
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
#include "power_hand.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "map_events.h"
#include "player_data.h"
#include "room_data.h"
#include "slab_data.h"
#include "tasks_list.h"
#include "thing_list.h"
#include "keeper_assist_state.h"
#include "api_urgent_hold.h"
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
#define OPEN_TRACKED 64
#define LOSS_KINDS 16
/** Turns between two loss wake-ups that skip the minimum interval: a raid claiming a corridor slab by slab wakes the
 *  agent at once, then again every LOSS_URGENT_GAP turns while it goes on, not every turn. */
#define LOSS_URGENT_GAP 60
#define INTRUDERS_LISTED 16
#define INTRUDERS_REMEMBERED 64
/** An intruder not seen on the seat's ground for this many turns is news again when it comes back. */
#define INTRUDER_FORGET_TURNS 1200

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

/** Ground or rooms the seat lost since its last decision, one entry per kind of loss, room kind and taker. */
struct LossSeen {
    char what[16];          /**< "room_captured", "room_destroyed", "ground_captured", "ground_lost" */
    RoomKind room_kind;     /**< the room the slab belonged to (0: floor or wall) */
    PlayerNumber by;        /**< who holds it now (-1: nobody; dug out, or claimed back to neutral) */
    int64_t count;          /**< slabs */
    MapSlabCoord x, y;      /**< the first of them */
    int64_t turn;           /**< when the first went */
};

/** An enemy creature on the seat's ground: who it is, as remembered (so one that steps off and back is not news), and
 *  as listed now. */
struct IntruderSeen {
    ThingIndex id;
    GameTurn born;
    int64_t last_seen;
};
struct IntruderNow {
    ThingIndex id;
    ThingModel model;
    PlayerNumber owner;
    int level;
    MapSubtlCoord x, y;
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
    int64_t pending_raised[MAX_REASONS]; /**< the turn each pending reason was first raised (latency: raise -> emit -> answer) */
    int pending_count;
    int64_t last_emit_turn;
    int64_t seq;
    int64_t latest_turn;
    char latest[MAX_REASONS][REASON_LEN];
    int64_t latest_raised[MAX_REASONS];
    int latest_count;
    unsigned char last_victory;  /**< victory_state at the last look: a change is urgent (victory / defeat; an alarm is too) */
    TbBool urgent;               /**< a pending reason that must not wait out the minimum interval */
    /** Where the pending reasons happened: the subtile of the most severe one with a place (where_sev, a SEV_ value;
     *  SEV_NONE: no place yet). */
    MapSubtlCoord where_x, where_y;
    int where_sev;
    MapSubtlCoord latest_where_x, latest_where_y;
    int latest_severity, latest_where_sev;
    /** The reasons of the last decision that went out urgently: the same reason again waits out LOSS_URGENT_GAP, a new
     *  one (wake_new: things got worse, an intruder then a fight) goes at once. */
    char last_urgent[MAX_REASONS][REASON_LEN];
    int last_urgent_count;
    TbBool wake_new;
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
    /** Unseen open ground the seat's own walls face (api_seat_open_behind_walls), as last seen: a new one is news. */
    int64_t open_xy[2 * OPEN_TRACKED];
    int64_t open_count;
    /** The combat assistant, as last seen: each rule's refusal, and how many of its actions were logged. */
    uint8_t assist_refusal[KEEPER_ASSIST_RULES];
    uint64_t assist_log_seen;
    /** Ground and rooms lost: since the last decision, and what the latest decision was told. */
    struct LossSeen losses[LOSS_KINDS];
    int loss_count;
    struct LossSeen latest_losses[LOSS_KINDS];
    int latest_loss_count;
    int64_t last_loss_urgent_turn;
    int64_t loss_scan_turn;
    TbBool loss_waiting;         /**< a loss, a new intruder or a fight came inside the gap: it goes out once the gap has passed */
    struct IntruderSeen remembered[INTRUDERS_REMEMBERED];
    int remembered_count;
    struct IntruderNow intruders[INTRUDERS_LISTED]; /**< on the seat's ground at the last scan, the first listed */
    int intruder_count;
    int64_t intruder_total;
    /** Sight of Evil (note_sight): the cast being watched, how far it has opened, for how many turns it has not grown. */
    ThingIndex sight_eye;
    GameTurn sight_born;
    int64_t sight_open, sight_still;
    int64_t sight_turn;          /**< the game turn sight_still was last counted on: the tick also runs while paused */
    TbBool sight_told;
};

/** How much a decision matters now (DECISION_DUE severity): what the agent should drop everything for, down to what can
 *  wait for its next look. A decision's severity is its most severe reason's. */
enum ReasonSeverity { SEV_CRITICAL = 0, SEV_COMBAT = 1, SEV_PROBLEM = 2, SEV_ROUTINE = 3, SEV_NONE = 4 };

static int reason_severity(const char *reason)
{
    static const char *const critical[] = { "heart_attacked", "defeat" };
    static const char *const combat[] = { "enemy_fight", "room_under_attack", "breach", "intruder", "alarm", "room_captured",
        "room_destroyed", "ground_lost", "room_lost" };
    static const char *const problem[] = { "creature_hungry", "treasure_room_full", "need_treasure_room", "creature_annoyed",
        "no_more_living_set", "work_room_unreachable", "storage_room_unreachable", "enemy_door", "creature_scavenged",
        "assist_budget", "assist_reserve", "assist_gold_guard", "sight_open", "prisoner_hungry", "torture_victim_hurt", "chat" };
    for (size_t i = 0; i < sizeof(critical) / sizeof(critical[0]); i++) if (strcmp(reason, critical[i]) == 0) return SEV_CRITICAL;
    for (size_t i = 0; i < sizeof(combat) / sizeof(combat[0]); i++) if (strcmp(reason, combat[i]) == 0) return SEV_COMBAT;
    for (size_t i = 0; i < sizeof(problem) / sizeof(problem[0]); i++) if (strcmp(reason, problem[i]) == 0) return SEV_PROBLEM;
    return SEV_ROUTINE;
}

static struct SeatDecision s_seat[PLAYERS_COUNT];
/** Each seat's map as last seen, one entry per slab: SLAB_OWNED, SLAB_MARKED (its own imps are to dig it), and the
 *  room kind on it in the low byte. */
#define SLAB_OWNED 0x8000
#define SLAB_MARKED 0x4000
static uint16_t s_slabs[PLAYERS_COUNT][MAX_TILES_X * MAX_TILES_Y];
static int64_t s_min_interval = 100;

void api_seat_decision_set_min_interval(int64_t turns)
{
    s_min_interval = (turns < 0) ? 0 : turns;
}

/** Notes where a pending reason happened (subtiles), keeping the place of the most severe one. */
static void note_where(struct SeatDecision *d, const char *reason, MapSubtlCoord x, MapSubtlCoord y)
{
    const int sev = reason_severity(reason);
    if (sev < d->where_sev) {
        d->where_sev = sev;
        d->where_x = x;
        d->where_y = y;
    }
}

static void add_pending(struct SeatDecision *d, const char *reason)
{
    for (int i = 0; i < d->pending_count; i++) {
        if (strcmp(d->pending[i], reason) == 0) return;
    }
    if (d->pending_count < MAX_REASONS) {
        d->pending_raised[d->pending_count] = (int64_t)get_gameturn();
        snprintf(d->pending[d->pending_count++], REASON_LEN, "%s", reason);
    }
}

/** The event kinds that make a decision due, by the name the view gives them (own.events). Besides attacks: the
 *  problems the event panel flashes to a human -- hungry creatures with a hatchery too small or none (creature_hungry,
 *  also the "food is scarce, enlarge the hatchery" speech), a full or missing treasure room, an annoyed creature, no
 *  lair space left, a room the creatures cannot reach, an enemy door in the way, a creature lost to scavenging, a
 *  secret door spotted -- and research finished (a room, spell, trap or door to use). Not urgent: they wait out the
 *  minimum interval. The engine spaces each kind (event_button_info's turns_between_events), so none becomes a stream. */
static const char *event_reason(unsigned char kind)
{
    switch (kind) {
    case EvKind_CreatrHungry: return "creature_hungry";
    case EvKind_TreasureRoomFull: return "treasure_room_full";
    case EvKind_NeedTreasureRoom: return "need_treasure_room";
    case EvKind_CreatrIsAnnoyed: return "creature_annoyed";
    case EvKind_NoMoreLivingSet: return "no_more_living_set";
    case EvKind_WorkRoomUnreachable: return "work_room_unreachable";
    case EvKind_StorageRoomUnreachable: return "storage_room_unreachable";
    case EvKind_EnemyDoor: return "enemy_door";
    case EvKind_CreatrScavenged: return "creature_scavenged";
    case EvKind_SecretDoorSpotted: return "secret_door_spotted";
    case EvKind_NewRoomResrch: return "new_room_research";
    case EvKind_NewSpellResrch: return "new_spell_research";
    case EvKind_NewTrap: return "new_trap";
    case EvKind_NewDoor: return "new_door";
    case EvKind_HeartAttacked: return "heart_attacked";
    case EvKind_EnemyFight: return "enemy_fight";
    case EvKind_RoomUnderAttack: return "room_under_attack";
    case EvKind_Breach: return "breach";
    case EvKind_RoomLost: return "room_lost";
    case EvKind_Information:
    case EvKind_QuickInformation: return "information";
    case EvKind_DnSpecialFound: return "special_found";
    case EvKind_AlarmTriggered: return "alarm";
    default: return NULL;
    }
}

static void add_pending_at(struct SeatDecision *d, const char *reason, MapSubtlCoord x, MapSubtlCoord y)
{
    add_pending(d, reason);
    note_where(d, reason, x, y);
}

/** A reason that wakes the agent at once, paced by LOSS_URGENT_GAP (note_losses): the same reason again waits out the
 *  gap since the last such wake-up, while one the last urgent decision did not have goes at once -- the situation got
 *  worse (an intruder, then the fight it starts), and that is news. Each kind can jump the gap once, so it cannot
 *  become a stream. */
static void wake_soon(struct SeatDecision *d, const char *reason)
{
    d->loss_waiting = true;
    for (int i = 0; i < d->last_urgent_count; i++) {
        if (strcmp(d->last_urgent[i], reason) == 0) return;
    }
    d->wake_new = true;
}

static TbBool event_is_fight(unsigned char kind)
{
    return (kind == EvKind_HeartAttacked) || (kind == EvKind_EnemyFight) || (kind == EvKind_RoomUnderAttack) || (kind == EvKind_Breach);
}

static int64_t quarter_of(PlayerNumber p)
{
    const int64_t gap = (int64_t)kfx_config_state.conf.rules[p].gameplay.pay_day_gap;
    const int64_t progress = (int64_t)kfx_config_state.pay_day_progress[p];
    return (gap > 0) ? (4 * progress / gap) % 4 : 0;
}

/** Sight of Evil shows what it uncovers only while it is open (it widens for a while, holds, then closes and that ground
 *  goes dark again), so once it has stopped widening the agent is called at once, like an alarm, to look at it while
 *  it can: reason sight_open, with own.active_powers.POWER_SIGHT saying what it uncovered. Once a cast. */
#define SIGHT_SETTLE_TURNS 8
static void note_sight(PlayerNumber p, struct SeatDecision *d)
{
    const struct Dungeon *dungeon = get_players_num_dungeon(p);
    const struct Thing *eye = (dungeon->sight_casted_thing_idx > 0) ? thing_get(dungeon->sight_casted_thing_idx) : NULL;
    if ((eye == NULL) || thing_is_invalid(eye)) {
        d->sight_eye = 0;
        return;
    }
    if ((d->sight_eye != eye->index) || (d->sight_born != eye->creation_turn)) {
        d->sight_eye = eye->index; d->sight_born = eye->creation_turn;
        d->sight_open = 0; d->sight_still = 0; d->sight_told = false;
    }
    if (d->sight_told) return;
    const int64_t now = (int64_t)get_gameturn();
    if (now == d->sight_turn) return; // settling is counted in game turns, not in polls (a paused game is still polled)
    d->sight_turn = now;
    int64_t open = 0;
    for (int64_t y = 0; y < 2 * MAX_SOE_RADIUS; y++)
        for (int64_t x = 0; x < 2 * MAX_SOE_RADIUS; x++)
            if (dungeon->soe_explored_flags[y][x]) open++;
    if (open > d->sight_open) {
        d->sight_open = open;
        d->sight_still = 0;
        return;
    }
    if ((open > 0) && (++d->sight_still >= SIGHT_SETTLE_TURNS)) {
        add_pending(d, "sight_open");
        d->urgent = true;
        d->sight_told = true;
    }
}

/** The combat assistant hit one of its limits since the last look: a rule's budget is spent, a cast was held back only by
 *  the reserve, the set's cap for the pay day (max_spend) is reached, or the gold guard switched a power off. The agent should look at its standing orders. */
static void note_assist(PlayerNumber p, struct SeatDecision *d, TbBool baseline)
{
    const struct KeeperAssistState *ka = &kfx_sim_state.keeper_assist[p];
    for (int s = 0; s < KEEPER_ASSIST_RULES; s++) {
        const uint8_t now = ka->live[s].used ? ka->run[s].last_refusal : KAR_None;
        if (!baseline && (now != d->assist_refusal[s])) {
            if ((now == KAR_Budget) || (now == KAR_MaxSpend)) add_pending(d, "assist_budget"); // a rule's budget, or the whole set's cap
            else if (now == KAR_Reserve) add_pending(d, "assist_reserve");
        }
        d->assist_refusal[s] = now;
    }
    if (!baseline) {
        const uint64_t from = (ka->log_total > d->assist_log_seen + KEEPER_ASSIST_LOG) ? ka->log_total - KEEPER_ASSIST_LOG : d->assist_log_seen;
        for (uint64_t i = from; i < ka->log_total; i++) {
            const struct KeeperAssistCast *c = &ka->log[i % KEEPER_ASSIST_LOG];
            if ((c->target == 0) && ((c->power == PwrK_CALL2ARMS) || (c->power == PwrK_OBEY))) add_pending(d, "assist_gold_guard");
        }
    }
    d->assist_log_seen = ka->log_total;
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
    memcpy(d->latest_raised, d->pending_raised, sizeof(d->latest_raised));
    int severity = SEV_ROUTINE;
    for (int i = 0; i < d->pending_count; i++) {
        const int sev = reason_severity(d->pending[i]);
        if (sev < severity) severity = sev;
    }
    d->latest_severity = severity;
    d->latest_where_sev = d->where_sev;
    d->latest_where_x = d->where_x;
    d->latest_where_y = d->where_y;
    if (d->urgent) {
        memcpy(d->last_urgent, d->pending, sizeof(d->last_urgent));
        d->last_urgent_count = d->pending_count;
    }
    char joined[MAX_REASONS * REASON_LEN];
    char raised[MAX_REASONS * 24];
    joined[0] = 0;
    raised[0] = 0;
    for (int i = 0; i < d->pending_count; i++) {
        if (i > 0) strncat(joined, ",", sizeof(joined) - strlen(joined) - 1);
        strncat(joined, d->pending[i], sizeof(joined) - strlen(joined) - 1);
        str_appendf(raised, sizeof(raised), "%s%" PRId64, (i > 0) ? "," : "", d->pending_raised[i]);
    }
    d->pending_count = 0;
    d->urgent = false;
    d->wake_new = false;
    d->where_sev = SEV_NONE;
    d->last_emit_turn = d->latest_turn;
    memcpy(d->latest_losses, d->losses, sizeof(d->latest_losses));
    d->latest_loss_count = d->loss_count;
    d->loss_count = 0;
    d->loss_waiting = false;
    struct ApiEventData data[10];
    memset(data, 0, sizeof(data));
    data[0].name = "player"; data[0].type = API_EVENT_DATA_INT32; data[0].value.int32_value = p;
    data[1].name = "seq"; data[1].type = API_EVENT_DATA_INT64; data[1].value.int64_value = d->seq;
    data[2].name = "turn"; data[2].type = API_EVENT_DATA_INT64; data[2].value.int64_value = d->latest_turn;
    data[3].name = "reasons"; data[3].type = API_EVENT_DATA_STRING; data[3].value.string_value = joined;
    data[4].name = "quarter"; data[4].type = API_EVENT_DATA_INT32; data[4].value.int32_value = quarter_of(p);
    // In the push itself, so a client that only reads pushes still learns the level is over.
    data[5].name = "victory_state"; data[5].type = API_EVENT_DATA_STRING;
    data[5].value.string_value = victory_code(get_player(p)->victory_state);
    // When each reason was raised, in the order of reasons: with turn, how long the minimum interval held it back.
    data[6].name = "raised"; data[6].type = API_EVENT_DATA_STRING; data[6].value.string_value = raised;
    // How much it matters (0 critical .. 3 routine), and where its most severe located cause is (subtiles; -1: nowhere).
    data[7].name = "severity"; data[7].type = API_EVENT_DATA_INT32; data[7].value.int32_value = severity;
    const TbBool placed = (d->latest_where_sev != SEV_NONE);
    data[8].name = "where_x"; data[8].type = API_EVENT_DATA_INT32; data[8].value.int32_value = placed ? (int32_t)d->latest_where_x : -1;
    data[9].name = "where_y"; data[9].type = API_EVENT_DATA_INT32; data[9].value.int32_value = placed ? (int32_t)d->latest_where_y : -1;
    api_event_with_data("DECISION_DUE", data, 10);
    // Time to think, where pausing is fair (api_urgent_hold.h): an urgent decision holds the game until it is answered.
    if (severity <= SEV_COMBAT) api_urgent_hold_begin(p, d->seq);
}

/** Open ground behind the seat's walls (a wall built facing a slab it has not seen: somebody digging around the
 *  dungeon, the way a keeper watching the wall would notice it): raises wall_cue when a slab joins the set. */
static void note_open_behind_walls(PlayerNumber p, struct SeatDecision *d, TbBool baseline)
{
    int64_t xy[2 * OPEN_TRACKED];
    int64_t n = api_seat_open_behind_walls(p, xy, OPEN_TRACKED);
    if (n > OPEN_TRACKED) n = OPEN_TRACKED;
    if (!baseline) {
        for (int64_t i = 0; i < n; i++) {
            TbBool known = false;
            for (int64_t k = 0; (k < d->open_count) && !known; k++) {
                known = (d->open_xy[2 * k] == xy[2 * i]) && (d->open_xy[2 * k + 1] == xy[2 * i + 1]);
            }
            if (!known) { add_pending(d, "wall_cue"); break; }
        }
    }
    memcpy(d->open_xy, xy, sizeof(xy[0]) * 2 * n);
    d->open_count = n;
}

static void add_loss(struct SeatDecision *d, const char *what, RoomKind rkind, PlayerNumber by, MapSlabCoord x, MapSlabCoord y, int64_t now)
{
    for (int i = 0; i < d->loss_count; i++) {
        struct LossSeen *l = &d->losses[i];
        if ((strcmp(l->what, what) == 0) && (l->room_kind == rkind) && (l->by == by)) { l->count++; return; }
    }
    if (d->loss_count >= LOSS_KINDS) return;
    struct LossSeen *l = &d->losses[d->loss_count++];
    snprintf(l->what, sizeof(l->what), "%s", what);
    l->room_kind = rkind; l->by = by; l->count = 1; l->x = x; l->y = y; l->turn = now;
    note_where(d, (strcmp(what, "ground_captured") == 0) ? "ground_lost" : what, slab_subtile_center(x), slab_subtile_center(y));
}

/** Ground and rooms the seat lost since the last look, from its own map, not the engine's events: those cover only part
 *  of it (a room taken by enemy imps updates an event nearby rather than raising a new one; a vandalised room raises one
 *  only for the last slab of its kind; ground claimed or dug out raises none). A slab the seat owned and does not now is
 *  classified by what it is now: another keeper's -- captured (room_captured, or ground_captured for floor and walls);
 *  nobody's -- a room slab destroyed (room_destroyed), floor or wall neutralised or dug through by an enemy
 *  (ground_lost: the first sign of an enemy claiming its way in, or a breach). Not a loss: what the seat still owns
 *  (a room sold or rebuilt), a slab its own imps were marked to dig, and a bridge back to water or lava (sold).
 *  Reasons room_captured, room_destroyed and ground_lost; they wake the agent at once, skipping the minimum interval,
 *  and a loss within LOSS_URGENT_GAP turns of the last such wake-up goes out as soon as the gap has passed. The details
 *  go to the decision's losses. */
static void scan_losses(PlayerNumber p, struct SeatDecision *d, TbBool baseline, int64_t now);
static void scan_intruders(PlayerNumber p, struct SeatDecision *d, TbBool baseline, int64_t now);
static void note_losses(PlayerNumber p, struct SeatDecision *d, TbBool baseline, int64_t now)
{
    if (baseline || (d->loss_scan_turn != now)) { // once per game turn
        scan_losses(p, d, baseline, now);
        scan_intruders(p, d, baseline, now);
    }
    if (d->loss_waiting && (d->wake_new || (now - d->last_loss_urgent_turn >= LOSS_URGENT_GAP))) {
        d->urgent = true;
        d->last_loss_urgent_turn = now;
        d->loss_waiting = false;
        d->wake_new = false;
    }
}

/** Enemy creatures standing on the seat's ground (any slab it owns): an enemy walking in, or tunnelling in and claiming,
 *  is the "enemy in your base" a keeper sees before anything is lost. Not an intruder: the seat's own, neutral creatures,
 *  an ally's, a prisoner the seat holds, one knocked out, one in a hand. Reason intruder, on the same alarm rhythm as the
 *  losses, for one the seat has not seen on its ground in the last INTRUDER_FORGET_TURNS turns. */
static void scan_intruders(PlayerNumber p, struct SeatDecision *d, TbBool baseline, int64_t now)
{
    TbBool fresh = false;
    d->intruder_count = 0;
    d->intruder_total = 0;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t guard = 0; (idx > 0) && (guard < THINGS_COUNT); guard++) {
        struct Thing *thing = thing_get((ThingIndex)idx);
        if (thing_is_invalid(thing)) break;
        idx = thing->next_of_class;
        const PlayerNumber owner = thing->owner;
        if ((owner == p) || (owner == kfx_config_state.neutral_player_num) || (owner < 0) || (owner >= PLAYERS_COUNT)) continue;
        if (players_are_mutual_allies(p, owner) || thing_is_picked_up(thing)) continue;
        if (creature_is_kept_in_custody_by_player(thing, p) || creature_is_being_unconscious(thing)) continue;
        if (slabmap_owner(get_slabmap_for_subtile(thing->mappos.x.stl.num, thing->mappos.y.stl.num)) != p) continue;
        d->intruder_total++;
        if (d->intruder_count < INTRUDERS_LISTED) {
            struct IntruderNow *in = &d->intruders[d->intruder_count++];
            in->id = thing->index; in->model = thing->model; in->owner = owner;
            in->level = (int)creature_control_get_from_thing(thing)->exp_level + 1;
            in->x = thing->mappos.x.stl.num; in->y = thing->mappos.y.stl.num;
        }
        int k = 0;
        while ((k < d->remembered_count) && !((d->remembered[k].id == thing->index) && (d->remembered[k].born == thing->creation_turn))) k++;
        if (k == d->remembered_count) {
            if (!baseline && !fresh) note_where(d, "intruder", thing->mappos.x.stl.num, thing->mappos.y.stl.num);
            if (!baseline) fresh = true;
            if (d->remembered_count < INTRUDERS_REMEMBERED) {
                d->remembered_count++;
            } else {
                k = 0; // full: forget the one seen longest ago
                for (int j = 1; j < d->remembered_count; j++) if (d->remembered[j].last_seen < d->remembered[k].last_seen) k = j;
            }
            d->remembered[k].id = thing->index;
            d->remembered[k].born = thing->creation_turn;
        }
        d->remembered[k].last_seen = now;
    }
    for (int k = 0; k < d->remembered_count; ) {
        if (now - d->remembered[k].last_seen > INTRUDER_FORGET_TURNS) d->remembered[k] = d->remembered[--d->remembered_count];
        else k++;
    }
    if (fresh) {
        add_pending(d, "intruder");
        wake_soon(d, "intruder");
    }
}

static void scan_losses(PlayerNumber p, struct SeatDecision *d, TbBool baseline, int64_t now)
{
    d->loss_scan_turn = now;
    const int64_t w = (kfx_sim_state.map_tiles_x < MAX_TILES_X) ? kfx_sim_state.map_tiles_x : MAX_TILES_X;
    const int64_t h = (kfx_sim_state.map_tiles_y < MAX_TILES_Y) ? kfx_sim_state.map_tiles_y : MAX_TILES_Y;
    static unsigned char marked[MAX_TILES_X * MAX_TILES_Y];
    memset(marked, 0, sizeof(marked));
    const struct Dungeon *dungeon = get_players_num_dungeon(p);
    if (!dungeon_invalid(dungeon)) {
        const int64_t imax = (dungeon->highest_task_number < MAPTASKS_COUNT) ? dungeon->highest_task_number : MAPTASKS_COUNT;
        for (int64_t i = 0; i < imax; i++) {
            const struct MapTask *t = &dungeon->task_list[i];
            if (t->kind == 0) continue;
            const int64_t sx = subtile_slab(stl_num_decode_x(t->coords)), sy = subtile_slab(stl_num_decode_y(t->coords));
            if ((sx >= 0) && (sx < w) && (sy >= 0) && (sy < h)) marked[sy * MAX_TILES_X + sx] = 1;
        }
    }
    uint16_t *snap = s_slabs[p];
    const char *reasons[3] = { NULL, NULL, NULL };
    for (int64_t y = 0; y < h; y++) {
        for (int64_t x = 0; x < w; x++) {
            const struct SlabMap *slb = get_slabmap_block((MapSlabCoord)x, (MapSlabCoord)y);
            const PlayerNumber owner = slabmap_owner(slb);
            const struct Room *room = (slb->room_index > 0) ? room_get(slb->room_index) : NULL;
            uint16_t code = 0;
            if (owner == p) {
                code = SLAB_OWNED | (marked[y * MAX_TILES_X + x] ? SLAB_MARKED : 0);
                if ((room != NULL) && !room_is_invalid(room)) code |= (uint16_t)(room->kind & 0xFF);
            }
            const int64_t i = y * MAX_TILES_X + x;
            const uint16_t was = snap[i];
            snap[i] = code;
            if (baseline || !(was & SLAB_OWNED) || (code & SLAB_OWNED)) continue;
            const RoomKind was_room = (RoomKind)(was & 0xFF);
            if ((owner != kfx_config_state.neutral_player_num) && (owner >= 0) && (owner < PLAYERS_COUNT)) {
                add_loss(d, was_room ? "room_captured" : "ground_captured", was_room, owner, (MapSlabCoord)x, (MapSlabCoord)y, now);
                reasons[was_room ? 0 : 2] = was_room ? "room_captured" : "ground_lost";
            } else if (was_room) {
                if (room_role_matches(was_room, RoRoF_PassWater | RoRoF_PassLava | RoRoF_PassAbyss)) continue; // a bridge sold
                add_loss(d, "room_destroyed", was_room, -1, (MapSlabCoord)x, (MapSlabCoord)y, now);
                reasons[1] = "room_destroyed";
            } else if (!(was & SLAB_MARKED)) {
                add_loss(d, "ground_lost", 0, -1, (MapSlabCoord)x, (MapSlabCoord)y, now);
                reasons[2] = "ground_lost";
            }
        }
    }
    for (int k = 0; k < 3; k++) if (reasons[k] != NULL) { add_pending(d, reasons[k]); wake_soon(d, reasons[k]); }
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
        d->where_sev = SEV_NONE;
        d->latest_where_sev = SEV_NONE;
        d->latest_severity = SEV_ROUTINE;
        d->last_quarter = quarter_of(p);
        d->last_victory = get_player(p)->victory_state;
        note_objective(p, d, now); // the objective the level opened with is history, not news
        note_custody(p, d, true);
        note_sacrifices(p, d, true, now);
        note_open_behind_walls(p, d, true);
        note_assist(p, d, true);
        note_losses(p, d, true, now);
        d->last_loss_urgent_turn = now - LOSS_URGENT_GAP;
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
            if (why != NULL) add_pending_at(d, why, (MapSubtlCoord)coord_subtile(ev->mappos_x), (MapSubtlCoord)coord_subtile(ev->mappos_y));
            // An alarm trap going off is the early warning a human hears and sees at once (sound, flashing event icon):
            // it does not wait out the minimum interval. The engine already spaces alarm events (turns_between_events),
            // so it cannot become a stream.
            if (ev->kind == EvKind_AlarmTriggered) d->urgent = true;
            // A fight, an attack on the heart or a room, and a breach are settled in seconds: like a loss they go out at
            // once, then at most every LOSS_URGENT_GAP turns while they go on (note_losses), not after the minimum interval.
            if ((why != NULL) && event_is_fight(ev->kind)) wake_soon(d, why);
        }
        d->event_seen[i] = exists;
        d->event_kind[i] = ev->kind;
    }
    if (note_objective(p, d, now)) add_pending(d, "objective");
    note_custody(p, d, false);
    note_sacrifices(p, d, false, now);
    note_open_behind_walls(p, d, false);
    note_assist(p, d, false);
    note_sight(p, d);
    note_losses(p, d, false, now);
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
    api_urgent_hold_tick();
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

TbBool api_seat_decision_latest(PlayerNumber plyr_idx, int64_t *seq, int64_t *turn, TbBool *combat)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT) || !s_seat[plyr_idx].initialised) return false;
    const struct SeatDecision *d = &s_seat[plyr_idx];
    *seq = d->seq;
    *turn = d->latest_turn;
    *combat = false;
    static const char *const fight_reasons[] = { "enemy_fight", "heart_attacked", "room_under_attack", "breach" };
    for (int i = 0; i < d->latest_count; i++) {
        for (size_t k = 0; k < sizeof(fight_reasons) / sizeof(fight_reasons[0]); k++) {
            if (strcmp(d->latest[i], fight_reasons[k]) == 0) *combat = true;
        }
    }
    return true;
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
    value_init_int32(value_dict_add(dec, "severity"), d->latest_severity); // 0 critical, 1 combat, 2 problem, 3 routine
    if (d->latest_where_sev != SEV_NONE) {
        VALUE *wh = value_dict_add(dec, "where"); // the subtile of its most severe located cause
        value_init_array(wh);
        value_init_int64(value_array_append(wh), d->latest_where_x);
        value_init_int64(value_array_append(wh), d->latest_where_y);
    }
    {
        // The game held for this seat's answer (api_urgent_hold.h): paused or slowed, and for how long at most.
        enum UrgentHoldMode mode = UHM_None;
        int64_t ms_left = 0;
        if (api_urgent_hold_waiting(plyr_idx, &mode, &ms_left)) {
            VALUE *h = value_dict_add(dec, "hold");
            value_init_dict(h);
            value_init_string(value_dict_add(h, "mode"), api_urgent_hold_mode_name(mode));
            value_init_int64(value_dict_add(h, "ms_left"), ms_left);
        }
        value_init_string(value_dict_add(dec, "on_urgent"), api_urgent_hold_mode_name(api_urgent_hold_mode()));
    }
    VALUE *ra = value_dict_add(dec, "raised"); // the turn each of those reasons was first raised
    value_init_array(ra);
    for (int i = 0; i < d->latest_count; i++) value_init_int64(value_array_append(ra), d->latest_raised[i]);
    value_init_int32(value_dict_add(dec, "pending"), d->pending_count);
    VALUE *pr = value_dict_add(dec, "pending_reasons"); // what is waiting for the next decision
    value_init_array(pr);
    for (int i = 0; i < d->pending_count; i++) value_init_string(value_array_append(pr), d->pending[i]);
    // What this decision's room_captured / room_destroyed / ground_lost were about.
    VALUE *ls = value_dict_add(dec, "losses");
    value_init_array(ls);
    for (int i = 0; i < d->latest_loss_count; i++) {
        const struct LossSeen *l = &d->latest_losses[i];
        VALUE *e = value_array_append(ls);
        value_init_dict(e);
        value_init_string(value_dict_add(e, "what"), l->what);
        if (l->room_kind > 0) value_init_string(value_dict_add(e, "room"), room_code_name(l->room_kind));
        if (l->by >= 0) value_init_int32(value_dict_add(e, "by"), (int32_t)l->by);
        value_init_int64(value_dict_add(e, "slabs"), l->count);
        VALUE *pos = value_dict_add(e, "pos");
        value_init_array(pos);
        value_init_int64(value_array_append(pos), slab_subtile_center(l->x));
        value_init_int64(value_array_append(pos), slab_subtile_center(l->y));
        value_init_int64(value_dict_add(e, "turn"), l->turn);
    }
}

void api_seat_decision_add_intruders(VALUE *own, PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT)) return;
    const struct SeatDecision *d = &s_seat[plyr_idx];
    VALUE *in = value_dict_add(own, "intruders");
    value_init_dict(in);
    value_init_int64(value_dict_add(in, "count"), d->intruder_total);
    VALUE *arr = value_dict_add(in, "creatures");
    value_init_array(arr);
    for (int i = 0; i < d->intruder_count; i++) {
        const struct IntruderNow *c = &d->intruders[i];
        VALUE *e = value_array_append(arr);
        value_init_dict(e);
        value_init_int32(value_dict_add(e, "id"), (int32_t)c->id);
        value_init_string(value_dict_add(e, "kind"), creature_code_name(c->model));
        value_init_int32(value_dict_add(e, "owner"), (int32_t)c->owner);
        value_init_int32(value_dict_add(e, "level"), c->level);
        VALUE *pos = value_dict_add(e, "pos");
        value_init_array(pos);
        value_init_int64(value_array_append(pos), c->x);
        value_init_int64(value_array_append(pos), c->y);
    }
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
