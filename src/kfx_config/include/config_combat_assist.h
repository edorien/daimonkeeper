/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_combat_assist.h
 *     A human player's combat assistant profile: save/combat_assistant.toml (docs/newFeatures/llm/01 section 10).
 * @par Purpose:
 *     One profile per install: on or off, a preset (fxdata/combat_assist_presets.toml) with the few values the Options
 *     screen's sliders change, or a full rule list written by hand. keeper_assist_profile_expand() turns it into the
 *     rule set the game is sent (kfx_game's keeper_assist_outbox.c), at the start of each level and on every change.
 */
/******************************************************************************/
#ifndef DK_CONFIG_COMBAT_ASSIST_H
#define DK_CONFIG_COMBAT_ASSIST_H

#include "globals.h"
#include "keeper_assist_rules.h"
#include <json-dom.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEEPER_ASSIST_PRESET_NAME_LEN 32

/** What the Options screen's sliders change in a preset's rules, found by the rules' names. */
struct KeeperAssistTuning {
    int32_t heal_below_pct;   /**< "heal": its health_pct < condition */
    int32_t heal_level;       /**< "heal": its level */
    int32_t buff_force_ratio; /**< "buff": its force_ratio > condition */
    int32_t cta_floor;        /**< "cta_floor": its gold < condition */
    int32_t obey_floor;       /**< "obey_floor": its gold < condition */
    uint32_t powers;          /**< powers the rules may use, a bit per PowerKind; a preset rule with another is left out */
};

struct KeeperAssistProfile {
    struct KeeperAssistGlobals globals;  /**< enabled, reserve, interval, min_gap, max_spend, charge_time */
    char preset[KEEPER_ASSIST_PRESET_NAME_LEN];
    struct KeeperAssistTuning tuning;
    int custom_count;                    /**< > 0: a hand-written rule list ([[rules]]) used instead of the preset */
    struct KeeperAssistRule custom[KEEPER_ASSIST_RULES];
};

/** Every power the assistant drives, as a KeeperAssistTuning.powers mask. */
uint32_t keeper_assist_all_powers_mask(void);
/** A profile with nothing set: off, the guardian preset with its own values, every power allowed. */
void keeper_assist_profile_defaults(struct KeeperAssistProfile *p);

/** Reads a profile file. False (and `p` the defaults) when it is missing; rules that do not validate are dropped with a
 *  warning, the rest kept. */
TbBool keeper_assist_profile_load_from(const char *path, struct KeeperAssistProfile *p);
/** Writes a profile file. */
TbBool keeper_assist_profile_save_to(const char *path, const struct KeeperAssistProfile *p);

/** This install's profile (save/combat_assistant.toml), read the first time it is asked for. */
struct KeeperAssistProfile *keeper_assist_profile(void);
/** Writes this install's profile back. */
TbBool keeper_assist_profile_save(void);

/** The rule set and globals a profile stands for: its hand-written rules, or its preset's from `presets` (as
 *  keeper_assist_load_presets loads them) with the tuning applied by rule name and the powers it does not allow left
 *  out. NULL on success, else an error code (an unknown preset gives no rules, off). */
const char *keeper_assist_profile_expand(const struct KeeperAssistProfile *p, VALUE *presets, struct KeeperAssistRule *rules,
    struct KeeperAssistGlobals *globals);
/** A preset's own values as tuning (what selecting it on the Options screen sets the sliders to), and its reserve. */
TbBool keeper_assist_tuning_from_preset(VALUE *presets, const char *name, struct KeeperAssistTuning *t, int32_t *reserve);

#ifdef __cplusplus
}
#endif
#endif
