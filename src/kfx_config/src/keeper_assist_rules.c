/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file keeper_assist_rules.c
 *     The combat assistant's rule vocabulary and validation (keeper_assist_rules.h).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "keeper_assist_rules.h"

#include <string.h>
#include <strings.h>

#include "config_creature.h"
#include "config_magic.h"
#include "kfx_config_state.h"
#include "config.h"
#include "value_util.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static const char *const fact_names[KAF_Count] = {
    "", "health_pct", "health", "level", "force", "is_digger", "on_own_ground", "fleeing", "kind",
    "battle_ally_force", "battle_enemy_force", "force_ratio", "battle_size", "allies_near",
    "gold", "gold_after_wages", "payday_turns", "fights", "fighters", "cta_active", "cta_paying", "obey_active",
    "heart_health_pct",
};
static const char *const target_names[KAT_Count] = { "", "ally_in_fight", "ally_any", "enemy_in_fight" };
static const char *const pick_names[KAP_Count] = { "", "lowest_health", "strongest", "highest_level", "most_valuable" };
static const char *const op_names[KAO_Count] = { "", "<", "<=", ">", ">=", "==", "!=" };

static int64_t by_name(const char *const *names, int64_t count, const char *name)
{
    if ((name == NULL) || (name[0] == 0)) return 0;
    for (int64_t i = 1; i < count; i++) {
        if (strcasecmp(names[i], name) == 0) return i;
    }
    return 0;
}

const char *keeper_assist_fact_name(enum KeeperAssistFact fact) { return ((fact > 0) && (fact < KAF_Count)) ? fact_names[fact] : ""; }
enum KeeperAssistFact keeper_assist_fact_by_name(const char *name) { return (enum KeeperAssistFact)by_name(fact_names, KAF_Count, name); }
const char *keeper_assist_target_name(enum KeeperAssistTarget t) { return ((t > 0) && (t < KAT_Count)) ? target_names[t] : ""; }
enum KeeperAssistTarget keeper_assist_target_by_name(const char *name) { return (enum KeeperAssistTarget)by_name(target_names, KAT_Count, name); }
const char *keeper_assist_pick_name(enum KeeperAssistPick p) { return ((p > 0) && (p < KAP_Count)) ? pick_names[p] : ""; }
enum KeeperAssistPick keeper_assist_pick_by_name(const char *name) { return (enum KeeperAssistPick)by_name(pick_names, KAP_Count, name); }
const char *keeper_assist_op_name(enum KeeperAssistOp op) { return ((op > 0) && (op < KAO_Count)) ? op_names[op] : ""; }
enum KeeperAssistOp keeper_assist_op_by_name(const char *name) { return (enum KeeperAssistOp)by_name(op_names, KAO_Count, name); }

TbBool keeper_assist_fact_needs_target(enum KeeperAssistFact fact)
{
    return (fact > KAF_None) && (fact < KAF_FirstDungeonFact);
}

TbBool keeper_assist_compare(enum KeeperAssistOp op, int64_t lhs, int64_t rhs)
{
    switch (op) {
    case KAO_Lt: return lhs < rhs;
    case KAO_Le: return lhs <= rhs;
    case KAO_Gt: return lhs > rhs;
    case KAO_Ge: return lhs >= rhs;
    case KAO_Eq: return lhs == rhs;
    case KAO_Ne: return lhs != rhs;
    default: return false;
    }
}

enum KeeperAssistTarget keeper_assist_power_target_class(PowerKind power, TbBool *power_off)
{
    *power_off = false;
    switch (power) {
    case PwrK_HEALCRTR:
    case PwrK_SPEEDCRTR:
    case PwrK_PROTECT:
    case PwrK_CONCEAL:
        return KAT_AllyInFight;
    case PwrK_CHICKEN:
    case PwrK_DISEASE:
    case PwrK_LIGHTNING:
    case PwrK_CAVEIN:
        return KAT_EnemyInFight;
    case PwrK_CALL2ARMS:
    case PwrK_OBEY:
        *power_off = true;
        return KAT_None;
    default:
        return KAT_None;
    }
}

void keeper_assist_default_globals(struct KeeperAssistGlobals *g)
{
    memset(g, 0, sizeof(*g));
    g->interval = KEEPER_ASSIST_DEFAULT_INTERVAL;
    g->charge_time = 1;
    g->min_gap = KEEPER_ASSIST_DEFAULT_MIN_GAP;
}

void keeper_assist_default_rule(struct KeeperAssistRule *r, enum KeeperAssistAction action, PowerKind power,
    enum KeeperAssistTarget target)
{
    memset(r, 0, sizeof(*r));
    r->used = 1;
    r->action = (uint8_t)action;
    r->power = (uint16_t)power;
    r->target = (uint8_t)target;
    r->pick = KAP_Default;
    r->cooldown = (action == KAA_Cast) ? KEEPER_ASSIST_DEFAULT_COOLDOWN : 0;
    r->target_cooldown = ((target == KAT_AllyInFight) || (target == KAT_AllyAny)) ? KEEPER_ASSIST_DEFAULT_ALLY_TARGET_COOLDOWN : 0;
}

static const char *const hand_names[] = { "evacuate_diggers", "lift_wounded", "reinforce" };

TbBool keeper_assist_action_is_hand(uint8_t action)
{
    return (action >= KAA_EvacuateDiggers) && (action < KAA_Count);
}

const char *keeper_assist_hand_name(uint8_t action)
{
    return keeper_assist_action_is_hand(action) ? hand_names[action - KAA_EvacuateDiggers] : "";
}

/** The target set a hand rule's candidates come from (its conditions' target facts are theirs). */
static enum KeeperAssistTarget hand_target(uint8_t action)
{
    return (action == KAA_EvacuateDiggers) ? KAT_AllyAny : KAT_AllyInFight;
}

static const char *validate_conds(const struct KeeperAssistRule *r)
{
    if (r->cond_count > KEEPER_ASSIST_CONDS) return "TOO_MANY_CONDITIONS";
    for (int i = 0; i < r->cond_count; i++) {
        const struct KeeperAssistCond *c = &r->cond[i];
        if ((c->fact <= KAF_None) || (c->fact >= KAF_Count)) return "UNKNOWN_FACT";
        if ((c->op <= KAO_None) || (c->op >= KAO_Count)) return "BAD_OP";
        if (keeper_assist_fact_needs_target((enum KeeperAssistFact)c->fact) && (r->target == KAT_None)) return "FACT_NEEDS_TARGET";
        if ((c->fact == KAF_Kind) && (c->op != KAO_Eq) && (c->op != KAO_Ne)) return "BAD_OP";
    }
    return NULL;
}

static const char *validate_hand(const struct KeeperAssistRule *r)
{
    if (r->power != 0) return "BAD_RULE";
    if (r->target != hand_target(r->action)) return "UNKNOWN_TARGET";
    if ((r->count < 1) || (r->count > KEEPER_ASSIST_HAND_MAX)) return "VALUE_OUT_OF_RANGE";
    if ((r->radius < 1) || (r->radius > 15) || (r->min_match > 200) || (r->cooldown > 10000)) return "VALUE_OUT_OF_RANGE";
    if ((r->action == KAA_EvacuateDiggers) && (r->dest_x == 0) && (r->dest_y == 0)) return "MISSING_DESTINATION";
    if ((r->dest_x >= 3 * MAX_TILES_X) || (r->dest_y >= 3 * MAX_TILES_Y)) return "POSITION_OFF_MAP";
    return validate_conds(r);
}

const char *keeper_assist_rule_validate(const struct KeeperAssistRule *r)
{
    if (keeper_assist_action_is_hand(r->action)) return validate_hand(r);
    if ((r->power < 1) || ((int64_t)r->power >= kfx_config_state.conf.magic_conf.power_types_count)) return "UNKNOWN_POWER";
    TbBool power_off = false;
    const enum KeeperAssistTarget cls = keeper_assist_power_target_class(r->power, &power_off);
    if (r->action == KAA_PowerOff) {
        if (!power_off) return "POWER_NOT_SUPPORTED";
        if (r->target != KAT_None) return "BAD_TARGET_FOR_POWER";
    } else if (r->action == KAA_Cast) {
        if (cls == KAT_None) return "POWER_NOT_SUPPORTED";
        if ((r->target <= KAT_None) || (r->target >= KAT_Count)) return "UNKNOWN_TARGET";
        const TbBool ally_target = (r->target == KAT_AllyInFight) || (r->target == KAT_AllyAny);
        if (ally_target != (cls == KAT_AllyInFight)) return "BAD_TARGET_FOR_POWER";
        if (r->level > KEEPER_ASSIST_MAX_LEVEL) return "BAD_LEVEL";
    } else {
        return "UNKNOWN_ACTION";
    }
    if (r->pick >= KAP_Count) return "UNKNOWN_PICK";
    if ((r->budget < 0) || (r->max_allies_hit > 15) || (r->cooldown > 10000) || (r->target_cooldown > 10000)) return "VALUE_OUT_OF_RANGE";
    return validate_conds(r);
}

const char *keeper_assist_globals_validate(const struct KeeperAssistGlobals *g)
{
    if ((g->interval < 1) || (g->interval > 20)) return "VALUE_OUT_OF_RANGE";
    if ((g->min_gap < 1) || (g->min_gap > 200)) return "VALUE_OUT_OF_RANGE";
    if ((g->reserve < 0) || (g->max_spend < 0)) return "VALUE_OUT_OF_RANGE";
    return NULL;
}

// ---- the rules as an agent or a profile writes them (JSON or TOML) ------------------------------------------------

static TbBool get_int(VALUE *v, int64_t *out)
{
    switch (value_type(v)) {
    case VALUE_INT32: case VALUE_UINT32: case VALUE_INT64: case VALUE_UINT64:
        *out = value_int64(v);
        return true;
    case VALUE_BOOL:
        *out = value_bool(v) ? 1 : 0;
        return true;
    default:
        return false;
    }
}

/** An optional integer field in [lo, hi]: absent leaves *out alone. */
static const char *opt_int(VALUE *dict, const char *key, int64_t lo, int64_t hi, int64_t *out)
{
    VALUE *v = value_dict_get(dict, key);
    if (v == NULL) return NULL;
    int64_t x;
    if (!get_int(v, &x)) return "BAD_VALUE";
    if ((x < lo) || (x > hi)) return "VALUE_OUT_OF_RANGE";
    *out = x;
    return NULL;
}

const char *keeper_assist_parse_globals(VALUE *root, struct KeeperAssistGlobals *g)
{
    int64_t v;
    const char *e;
    v = g->enabled;     if ((e = opt_int(root, "enabled", 0, 1, &v)) != NULL) return e;            g->enabled = (uint8_t)v;
    v = g->interval;    if ((e = opt_int(root, "interval", 1, 20, &v)) != NULL) return e;          g->interval = (uint8_t)v;
    v = g->charge_time; if ((e = opt_int(root, "charge_time", 0, 1, &v)) != NULL) return e;        g->charge_time = (uint8_t)v;
    v = g->min_gap;     if ((e = opt_int(root, "min_gap", 1, 200, &v)) != NULL) return e;          g->min_gap = (uint16_t)v;
    v = g->reserve;     if ((e = opt_int(root, "reserve", 0, 1000000000, &v)) != NULL) return e;   g->reserve = (int32_t)v;
    v = g->max_spend;   if ((e = opt_int(root, "max_spend", 0, 1000000000, &v)) != NULL) return e; g->max_spend = (int32_t)v;
    return NULL;
}

static const char *parse_conds(VALUE *rv, struct KeeperAssistRule *r);

/** A hand rule: {"hand": "evacuate_diggers" | "lift_wounded" | "reinforce", "to": [x, y] (subtiles), "count", "radius"
 *  (slabs), "min_match" (reinforce, %), "cooldown", "when"}. */
static const char *parse_hand_rule(VALUE *rv, const char *hand, struct KeeperAssistRule *r)
{
    int64_t action = -1;
    for (int64_t i = 0; i < (int64_t)(sizeof(hand_names) / sizeof(hand_names[0])); i++) {
        if (strcmp(hand, hand_names[i]) == 0) action = KAA_EvacuateDiggers + i;
    }
    if (action < 0) return "UNKNOWN_HAND";
    if ((value_dict_get(rv, "power") != NULL) || (value_dict_get(rv, "power_off") != NULL) || (value_dict_get(rv, "target") != NULL)) return "BAD_RULE";
    memset(r, 0, sizeof(*r));
    r->used = 1;
    r->action = (uint8_t)action;
    r->target = (uint8_t)hand_target(r->action);
    r->count = (action == KAA_Reinforce) ? 4 : KEEPER_ASSIST_HAND_MAX;
    r->radius = KEEPER_ASSIST_DEFAULT_HAND_RADIUS;
    r->min_match = KEEPER_ASSIST_DEFAULT_MIN_MATCH;
    r->cooldown = KEEPER_ASSIST_DEFAULT_HAND_COOLDOWN;
    const char *e;
    int64_t v;
    v = r->count;     if ((e = opt_int(rv, "count", 1, KEEPER_ASSIST_HAND_MAX, &v)) != NULL) return e;  r->count = (uint8_t)v;
    v = r->radius;    if ((e = opt_int(rv, "radius", 1, 15, &v)) != NULL) return e;                      r->radius = (uint8_t)v;
    v = r->min_match; if ((e = opt_int(rv, "min_match", 0, 200, &v)) != NULL) return e;                  r->min_match = (uint8_t)v;
    v = r->cooldown;  if ((e = opt_int(rv, "cooldown", 0, 10000, &v)) != NULL) return e;                 r->cooldown = (uint16_t)v;
    VALUE *to = value_dict_get(rv, "to");
    if (to != NULL) {
        int64_t x = 0, y = 0;
        if ((value_type(to) != VALUE_ARRAY) || (value_array_size(to) != 2) || !get_int(value_array_get(to, 0), &x) || !get_int(value_array_get(to, 1), &y)) return "BAD_POSITION";
        if ((x < 0) || (y < 0) || (x >= 3 * MAX_TILES_X) || (y >= 3 * MAX_TILES_Y) || ((x == 0) && (y == 0))) return "POSITION_OFF_MAP";
        r->dest_x = (uint16_t)x; r->dest_y = (uint16_t)y;
    }
    if ((e = parse_conds(rv, r)) != NULL) return e;
    return keeper_assist_rule_validate(r);
}

const char *keeper_assist_parse_rule(VALUE *rv, struct KeeperAssistRule *r)
{
    if (value_type(rv) != VALUE_DICT) return "BAD_RULE";
    const char *hand = value_string(value_dict_get(rv, "hand"));
    if (hand != NULL) return parse_hand_rule(rv, hand, r);
    const char *cast = value_string(value_dict_get(rv, "power"));
    const char *off = value_string(value_dict_get(rv, "power_off"));
    if ((cast == NULL) == (off == NULL)) return "MISSING_POWER"; // exactly one of them
    const int64_t power = power_model_id(cast ? cast : off);
    if (power < 1) return "UNKNOWN_POWER";
    TbBool power_off = false;
    const enum KeeperAssistTarget cls = keeper_assist_power_target_class(power, &power_off);
    enum KeeperAssistTarget target = KAT_None;
    if (cast != NULL) {
        const char *tname = value_string(value_dict_get(rv, "target"));
        target = (tname != NULL) ? keeper_assist_target_by_name(tname) : cls;
        if ((tname != NULL) && (target == KAT_None)) return "UNKNOWN_TARGET";
        if (target == KAT_None) return "POWER_NOT_SUPPORTED";
    } else if (value_dict_get(rv, "target") != NULL) {
        return "BAD_TARGET_FOR_POWER";
    }
    keeper_assist_default_rule(r, cast ? KAA_Cast : KAA_PowerOff, power, target);
    const char *e;
    int64_t v;
    v = r->level;
    if ((e = opt_int(rv, "level", 0, KEEPER_ASSIST_MAX_LEVEL, &v)) != NULL) return (strcmp(e, "VALUE_OUT_OF_RANGE") == 0) ? "BAD_LEVEL" : e;
    r->level = (uint8_t)v;
    v = r->cooldown;        if ((e = opt_int(rv, "cooldown", 0, 10000, &v)) != NULL) return e;        r->cooldown = (uint16_t)v;
    v = r->target_cooldown; if ((e = opt_int(rv, "target_cooldown", 0, 10000, &v)) != NULL) return e; r->target_cooldown = (uint16_t)v;
    v = r->budget;          if ((e = opt_int(rv, "budget", 0, 1000000000, &v)) != NULL) return e;     r->budget = (int32_t)v;
    v = r->max_allies_hit;  if ((e = opt_int(rv, "max_allies_hit", 0, 15, &v)) != NULL) return e;     r->max_allies_hit = (uint8_t)v;
    const char *pname = value_string(value_dict_get(rv, "pick"));
    if (pname != NULL) {
        r->pick = (uint8_t)keeper_assist_pick_by_name(pname);
        if (r->pick == KAP_Default) return "UNKNOWN_PICK";
    }
    if ((e = parse_conds(rv, r)) != NULL) return e;
    return keeper_assist_rule_validate(r);
}

static const char *parse_conds(VALUE *rv, struct KeeperAssistRule *r)
{
    VALUE *when = value_dict_get(rv, "when");
    if (when != NULL) {
        if (value_type(when) != VALUE_ARRAY) return "BAD_CONDITION";
        if (value_array_size(when) > KEEPER_ASSIST_CONDS) return "TOO_MANY_CONDITIONS";
        for (size_t i = 0; i < value_array_size(when); i++) {
            VALUE *c = value_array_get(when, i);
            if ((value_type(c) != VALUE_ARRAY) || (value_array_size(c) != 3)) return "BAD_CONDITION";
            const enum KeeperAssistFact fact = keeper_assist_fact_by_name(value_string(value_array_get(c, 0)));
            if (fact == KAF_None) return "UNKNOWN_FACT";
            const enum KeeperAssistOp op = keeper_assist_op_by_name(value_string(value_array_get(c, 1)));
            if (op == KAO_None) return "BAD_OP";
            VALUE *vv = value_array_get(c, 2);
            int64_t value = 0;
            if ((fact == KAF_Kind) && (value_type(vv) == VALUE_STRING)) {
                value = creature_model_id(value_string(vv)); // "DRAGON" as well as its number
                if (value < 1) return "UNKNOWN_KIND";
            } else if (!get_int(vv, &value) || (value < INT32_MIN) || (value > INT32_MAX)) {
                return "BAD_VALUE";
            }
            r->cond[r->cond_count].fact = (uint8_t)fact;
            r->cond[r->cond_count].op = (uint8_t)op;
            r->cond[r->cond_count].value = (int32_t)value;
            r->cond_count++;
        }
    }
    return NULL;
}

const char *keeper_assist_parse_set(VALUE *root, const struct KeeperAssistRule *current_rules, const struct KeeperAssistGlobals *current,
    struct KeeperAssistRule *rules, struct KeeperAssistGlobals *globals, int *bad_rule)
{
    *bad_rule = -1;
    *globals = *current;
    if (current->interval == 0) {
        keeper_assist_default_globals(globals); // never set: start from the defaults
        globals->enabled = current->enabled;
    }
    const char *e = keeper_assist_parse_globals(root, globals);
    if (e != NULL) return e;
    VALUE *rv = value_dict_get(root, "rules");
    if (rv == NULL) {
        memcpy(rules, current_rules, sizeof(struct KeeperAssistRule) * KEEPER_ASSIST_RULES);
        return NULL;
    }
    if (value_type(rv) != VALUE_ARRAY) return "BAD_RULES";
    if (value_array_size(rv) > KEEPER_ASSIST_RULES) return "TOO_MANY_RULES";
    memset(rules, 0, sizeof(struct KeeperAssistRule) * KEEPER_ASSIST_RULES);
    for (size_t i = 0; i < value_array_size(rv); i++) {
        e = keeper_assist_parse_rule(value_array_get(rv, i), &rules[i]);
        if (e != NULL) {
            *bad_rule = (int)i;
            return e;
        }
    }
    return NULL;
}

void keeper_assist_rule_to_value(VALUE *e, const struct KeeperAssistRule *r)
{
    if (keeper_assist_action_is_hand(r->action)) {
        value_init_string(value_dict_add(e, "hand"), keeper_assist_hand_name(r->action));
        if ((r->dest_x != 0) || (r->dest_y != 0)) {
            VALUE *to = value_dict_add(e, "to");
            value_init_array(to);
            value_init_int64(value_array_append(to), r->dest_x);
            value_init_int64(value_array_append(to), r->dest_y);
        }
        value_init_int32(value_dict_add(e, "count"), r->count);
        value_init_int32(value_dict_add(e, "radius"), r->radius);
        if (r->action == KAA_Reinforce) value_init_int32(value_dict_add(e, "min_match"), r->min_match);
        value_init_int32(value_dict_add(e, "cooldown"), r->cooldown);
    } else {
        value_init_string(value_dict_add(e, (r->action == KAA_PowerOff) ? "power_off" : "power"), power_code_name(r->power));
    }
    if (r->action == KAA_Cast) {
        value_init_int32(value_dict_add(e, "level"), r->level);
        value_init_string(value_dict_add(e, "target"), keeper_assist_target_name((enum KeeperAssistTarget)r->target));
        if (r->pick != KAP_Default) value_init_string(value_dict_add(e, "pick"), keeper_assist_pick_name((enum KeeperAssistPick)r->pick));
        value_init_int32(value_dict_add(e, "cooldown"), r->cooldown);
        value_init_int32(value_dict_add(e, "target_cooldown"), r->target_cooldown);
        if (r->budget > 0) value_init_int64(value_dict_add(e, "budget"), r->budget);
        if (r->max_allies_hit > 0) value_init_int32(value_dict_add(e, "max_allies_hit"), r->max_allies_hit);
    }
    VALUE *when = value_dict_add(e, "when");
    value_init_array(when);
    for (int c = 0; c < r->cond_count; c++) {
        VALUE *ce = value_array_append(when);
        value_init_array(ce);
        value_init_string(value_array_append(ce), keeper_assist_fact_name((enum KeeperAssistFact)r->cond[c].fact));
        value_init_string(value_array_append(ce), keeper_assist_op_name((enum KeeperAssistOp)r->cond[c].op));
        if (r->cond[c].fact == KAF_Kind) value_init_string(value_array_append(ce), creature_code_name((ThingModel)r->cond[c].value));
        else value_init_int64(value_array_append(ce), r->cond[c].value);
    }
}

TbBool keeper_assist_load_presets(const char *path, VALUE *out)
{
    const char *fname = (path != NULL) ? path : prepare_file_path(FGrp_FxData, "combat_assist_presets.toml");
    if (!load_toml_file(fname, out, CnfLd_IgnoreErrors) || (value_type(out) != VALUE_DICT)) {
        value_init_dict(out);
        return false;
    }
    return true;
}

const char *keeper_assist_preset_set(VALUE *presets, const char *name, struct KeeperAssistRule *rules,
    struct KeeperAssistGlobals *globals, int *bad_rule)
{
    *bad_rule = -1;
    VALUE *p = (name != NULL) ? value_dict_get(presets, name) : NULL;
    if ((p == NULL) || (value_type(p) != VALUE_DICT)) return "UNKNOWN_PRESET";
    static const struct KeeperAssistRule none[KEEPER_ASSIST_RULES];
    const struct KeeperAssistGlobals zero = {0};
    return keeper_assist_parse_set(p, none, &zero, rules, globals, bad_rule);
}

#ifdef __cplusplus
}
#endif
