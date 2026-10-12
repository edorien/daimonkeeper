/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_combat_assist.c
 *     A human player's combat assistant profile (config_combat_assist.h).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "config_combat_assist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bflib_basics.h"
#include "config.h"
#include "config_magic.h"
#include "kfx_config_state.h"
#include "bflib_dernc.h"
#include "value_util.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static const PowerKind assist_powers[] = { PwrK_HEALCRTR, PwrK_SPEEDCRTR, PwrK_PROTECT, PwrK_CONCEAL, PwrK_CHICKEN,
    PwrK_DISEASE, PwrK_LIGHTNING, PwrK_CAVEIN, PwrK_CALL2ARMS, PwrK_OBEY };

uint32_t keeper_assist_all_powers_mask(void)
{
    uint32_t m = 0;
    for (size_t i = 0; i < sizeof(assist_powers) / sizeof(assist_powers[0]); i++) m |= 1u << assist_powers[i];
    return m;
}

void keeper_assist_profile_defaults(struct KeeperAssistProfile *p)
{
    memset(p, 0, sizeof(*p));
    keeper_assist_default_globals(&p->globals);
    snprintf(p->preset, sizeof(p->preset), "guardian");
    p->tuning.heal_below_pct = 35;
    p->tuning.heal_level = 2;
    p->tuning.buff_force_ratio = 110;
    p->tuning.cta_floor = 1000;
    p->tuning.obey_floor = 800;
    p->tuning.powers = keeper_assist_all_powers_mask();
}

static int32_t clamp32(int64_t v, int64_t lo, int64_t hi)
{
    return (int32_t)((v < lo) ? lo : ((v > hi) ? hi : v));
}

static void read_int(VALUE *dict, const char *key, int32_t *out, int64_t lo, int64_t hi)
{
    VALUE *v = value_dict_get(dict, key);
    if ((v == NULL) || ((value_type(v) != VALUE_INT32) && (value_type(v) != VALUE_INT64) && (value_type(v) != VALUE_UINT32) && (value_type(v) != VALUE_UINT64))) return;
    *out = clamp32(value_int64(v), lo, hi);
}

TbBool keeper_assist_profile_load_from(const char *path, struct KeeperAssistProfile *p)
{
    keeper_assist_profile_defaults(p);
    VALUE root;
    if (!load_toml_file(path, &root, CnfLd_IgnoreErrors)) {
        return false;
    }
    struct KeeperAssistGlobals g = p->globals;
    const char *err = keeper_assist_parse_globals(&root, &g);
    if (err == NULL) {
        p->globals = g;
    } else {
        WARNLOG("Combat assistant profile %s: settings ignored (%s)", path, err);
    }
    const char *preset = value_string(value_dict_get(&root, "preset"));
    if (preset != NULL) snprintf(p->preset, sizeof(p->preset), "%s", preset);
    VALUE *t = value_dict_get(&root, "tuning");
    if (value_type(t) == VALUE_DICT) {
        read_int(t, "heal_below_pct", &p->tuning.heal_below_pct, 1, 99);
        read_int(t, "heal_level", &p->tuning.heal_level, 0, KEEPER_ASSIST_MAX_LEVEL);
        read_int(t, "buff_force_ratio", &p->tuning.buff_force_ratio, 0, 10000);
        read_int(t, "cta_floor", &p->tuning.cta_floor, 0, 1000000000);
        read_int(t, "obey_floor", &p->tuning.obey_floor, 0, 1000000000);
        VALUE *pw = value_dict_get(t, "powers");
        if (value_type(pw) == VALUE_ARRAY) {
            p->tuning.powers = 0;
            for (size_t i = 0; i < value_array_size(pw); i++) {
                const int64_t k = power_model_id(value_string(value_array_get(pw, i)) ? value_string(value_array_get(pw, i)) : "");
                if ((k > 0) && (k < 32)) p->tuning.powers |= 1u << k;
            }
        }
    }
    VALUE *rules = value_dict_get(&root, "rules");
    if (value_type(rules) == VALUE_ARRAY) {
        for (size_t i = 0; (i < value_array_size(rules)) && (p->custom_count < KEEPER_ASSIST_RULES); i++) {
            struct KeeperAssistRule r;
            err = keeper_assist_parse_rule(value_array_get(rules, i), &r);
            if (err != NULL) {
                WARNLOG("Combat assistant profile %s: rule %d dropped (%s)", path, (int)i, err);
                continue;
            }
            p->custom[p->custom_count++] = r;
        }
    }
    value_fini(&root);
    return true;
}

/** A power's code name from the power config itself: the table power_model_id() reads it back from. */
static const char *power_name(PowerKind k)
{
    return ((k > 0) && (k < kfx_config_state.conf.magic_conf.power_types_count)) ? kfx_config_state.conf.magic_conf.power_cfgstats[k].code_name : "";
}

#define OUT(...) do { if (len < cap) len += snprintf(buf + len, cap - len, __VA_ARGS__); } while (0)

TbBool keeper_assist_profile_save_to(const char *path, const struct KeeperAssistProfile *p)
{
    const size_t cap = 16384;
    char *buf = (char *)malloc(cap);
    if (buf == NULL) return false;
    size_t len = 0;
    OUT("# The combat assistant: your standing orders for fights (docs/newFeatures/llm/).\n");
    OUT("# Written by the game's Options screen. A [[rules]] list, if you write one, replaces the preset.\n");
    OUT("enabled = %s\n", p->globals.enabled ? "true" : "false");
    OUT("preset = \"%s\"\n", p->preset);
    OUT("reserve = %d\n", (int)p->globals.reserve);
    OUT("interval = %d\n", (int)p->globals.interval);
    OUT("min_gap = %d\n", (int)p->globals.min_gap);
    OUT("max_spend = %d\n", (int)p->globals.max_spend);
    OUT("charge_time = %s\n", p->globals.charge_time ? "true" : "false");
    OUT("\n[tuning]\n");
    OUT("heal_below_pct = %d\nheal_level = %d\nbuff_force_ratio = %d\ncta_floor = %d\nobey_floor = %d\n",
        (int)p->tuning.heal_below_pct, (int)p->tuning.heal_level, (int)p->tuning.buff_force_ratio,
        (int)p->tuning.cta_floor, (int)p->tuning.obey_floor);
    OUT("powers = [");
    int n = 0;
    for (size_t i = 0; i < sizeof(assist_powers) / sizeof(assist_powers[0]); i++) {
        if ((p->tuning.powers & (1u << assist_powers[i])) == 0) continue;
        OUT("%s\"%s\"", n++ ? ", " : "", power_name(assist_powers[i]));
    }
    OUT("]\n");
    for (int i = 0; i < p->custom_count; i++) {
        const struct KeeperAssistRule *r = &p->custom[i];
        OUT("\n[[rules]]\n");
        if (r->action == KAA_PowerOff) {
            OUT("power_off = \"%s\"\n", power_name(r->power));
        } else {
            OUT("power = \"%s\"\nlevel = %d\ntarget = \"%s\"\n", power_name(r->power), (int)r->level,
                keeper_assist_target_name((enum KeeperAssistTarget)r->target));
            if (r->pick != KAP_Default) OUT("pick = \"%s\"\n", keeper_assist_pick_name((enum KeeperAssistPick)r->pick));
            OUT("cooldown = %d\ntarget_cooldown = %d\n", (int)r->cooldown, (int)r->target_cooldown);
            if (r->budget > 0) OUT("budget = %d\n", (int)r->budget);
            if (r->max_allies_hit > 0) OUT("max_allies_hit = %d\n", (int)r->max_allies_hit);
        }
        OUT("when = [");
        for (int c = 0; c < r->cond_count; c++) {
            OUT("%s[\"%s\", \"%s\", %d]", c ? ", " : "", keeper_assist_fact_name((enum KeeperAssistFact)r->cond[c].fact),
                keeper_assist_op_name((enum KeeperAssistOp)r->cond[c].op), (int)r->cond[c].value);
        }
        OUT("]\n");
    }
    const TbBool ok = (len < cap) && LbFileSaveAtomic(path, buf, len); // never a half-written profile
    free(buf);
    return ok;
}

#undef OUT

static struct KeeperAssistProfile s_profile;
static TbBool s_profile_loaded = false;

static const char *profile_path(void)
{
    return prepare_file_path(FGrp_Save, "combat_assistant.toml");
}

struct KeeperAssistProfile *keeper_assist_profile(void)
{
    if (!s_profile_loaded) {
        keeper_assist_profile_load_from(profile_path(), &s_profile);
        s_profile_loaded = true;
    }
    return &s_profile;
}

TbBool keeper_assist_profile_save(void)
{
    return keeper_assist_profile_save_to(profile_path(), keeper_assist_profile());
}

/** The name the preset gives the rule in each slot (parse order = the preset's list order). */
static const char *preset_rule_name(VALUE *presets, const char *preset, int i)
{
    VALUE *arr = value_dict_get(value_dict_get(presets, preset), "rules");
    if ((value_type(arr) != VALUE_ARRAY) || ((size_t)i >= value_array_size(arr))) return "";
    const char *n = value_string(value_dict_get(value_array_get(arr, i), "name"));
    return n ? n : "";
}

static void set_cond(struct KeeperAssistRule *r, enum KeeperAssistFact fact, int32_t value)
{
    for (int c = 0; c < r->cond_count; c++) {
        if (r->cond[c].fact == fact) r->cond[c].value = value;
    }
}

const char *keeper_assist_profile_expand(const struct KeeperAssistProfile *p, VALUE *presets, struct KeeperAssistRule *rules,
    struct KeeperAssistGlobals *globals)
{
    *globals = p->globals;
    memset(rules, 0, sizeof(struct KeeperAssistRule) * KEEPER_ASSIST_RULES);
    if (p->custom_count > 0) {
        memcpy(rules, p->custom, sizeof(struct KeeperAssistRule) * p->custom_count);
        return NULL;
    }
    struct KeeperAssistRule from[KEEPER_ASSIST_RULES];
    struct KeeperAssistGlobals ignored;
    int bad = -1;
    const char *err = keeper_assist_preset_set(presets, p->preset, from, &ignored, &bad);
    if (err != NULL) return err;
    int n = 0;
    for (int i = 0; i < KEEPER_ASSIST_RULES; i++) {
        struct KeeperAssistRule r = from[i];
        if (!r.used) continue;
        if ((r.power >= 32) || ((p->tuning.powers & (1u << r.power)) == 0)) continue; // a power the player turned off
        const char *name = preset_rule_name(presets, p->preset, i);
        if (strcmp(name, "heal") == 0) {
            r.level = (uint8_t)p->tuning.heal_level;
            set_cond(&r, KAF_HealthPct, p->tuning.heal_below_pct);
        } else if (strcmp(name, "buff") == 0) {
            set_cond(&r, KAF_ForceRatio, p->tuning.buff_force_ratio);
        } else if (strcmp(name, "cta_floor") == 0) {
            set_cond(&r, KAF_Gold, p->tuning.cta_floor);
        } else if (strcmp(name, "obey_floor") == 0) {
            set_cond(&r, KAF_Gold, p->tuning.obey_floor);
        }
        rules[n++] = r;
    }
    return NULL;
}

static int32_t cond_value(const struct KeeperAssistRule *r, enum KeeperAssistFact fact, int32_t dflt)
{
    for (int c = 0; c < r->cond_count; c++) {
        if (r->cond[c].fact == fact) return r->cond[c].value;
    }
    return dflt;
}

TbBool keeper_assist_tuning_from_preset(VALUE *presets, const char *name, struct KeeperAssistTuning *t, int32_t *reserve)
{
    struct KeeperAssistRule rules[KEEPER_ASSIST_RULES];
    struct KeeperAssistGlobals g;
    int bad = -1;
    if (keeper_assist_preset_set(presets, name, rules, &g, &bad) != NULL) return false;
    for (int i = 0; i < KEEPER_ASSIST_RULES; i++) {
        if (!rules[i].used) continue;
        const char *n = preset_rule_name(presets, name, i);
        if (strcmp(n, "heal") == 0) { t->heal_level = rules[i].level; t->heal_below_pct = cond_value(&rules[i], KAF_HealthPct, t->heal_below_pct); }
        else if (strcmp(n, "buff") == 0) t->buff_force_ratio = cond_value(&rules[i], KAF_ForceRatio, t->buff_force_ratio);
        else if (strcmp(n, "cta_floor") == 0) t->cta_floor = cond_value(&rules[i], KAF_Gold, t->cta_floor);
        else if (strcmp(n, "obey_floor") == 0) t->obey_floor = cond_value(&rules[i], KAF_Gold, t->obey_floor);
    }
    *reserve = g.reserve;
    return true;
}

#ifdef __cplusplus
}
#endif
