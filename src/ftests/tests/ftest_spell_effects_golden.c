// Refactor pass 5, S07: golden hashes of what the spell effects do to a creature. set_thing_spell_flags_f() and
// clear_thing_spell_flags_f() become one table of apply/remove pairs; these hashes must not change when they do.
//
// keeporig level 11 is saved once; each case loads that save, makes a creature of player 0 next to its dungeon heart
// and casts on it. The creatures: one that walks, one that flies, and the walker made immune to every effect. Each
// spell of magic.cfg is applied (at levels 0 and SPELL_MAX_LEVEL), applied twice, applied and terminated, and applied
// with five game turns run after it; then each effect flag on its own is set and cleared through the two functions
// directly (so the effects no spell has, such as MAD_KILLING, are covered too), and set on the walker, left five game
// turns, then cleared (what a remove undoes, such as the mad killer's fights, has happened by then). The hashing is
// ftest_golden.h's.
//
// To print the hashes (only on a commit meant to change what an effect does): KFX_FTEST_GOLDEN_PRINT=1.
#include "ftest_spell_effects_golden.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_golden.h"

#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "thing_physics.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GOLDEN_SAVE_SLOT 10
#define GOLDEN_CASES_MAX 1024
#define GOLDEN_TURNS 5
#define GOLDEN_FLAGS_COUNT 19 // CSAfF_Slow .. CSAfF_SpellBlocks

enum GoldenCaseKind {
    GCK_Apply,      // apply_spell_effect_to_thing
    GCK_Reapply,    // the same twice
    GCK_Terminate,  // applied, then terminate_thing_spell_effect
    GCK_Turns,      // applied, then GOLDEN_TURNS game turns
    GCK_SetFlag,    // set_thing_spell_flags with the spell's flags replaced by one flag
    GCK_ClearFlag,  // that, then clear_thing_spell_flags with the flag
    GCK_FlagTurnsClear, // set as GCK_SetFlag, GOLDEN_TURNS game turns, then cleared
};

enum GoldenCreature {
    GCr_Walker,
    GCr_Flier,
    GCr_Immune,
    GCr_COUNT
};

static const char *const golden_kind_names[] = {"apply", "reapply", "terminate", "turns", "set_flag", "clear_flag", "flag_turns_clear"};
static const char *const golden_creature_names[] = {"walker", "flier", "immune"};

struct GoldenCase {
    unsigned char kind;
    unsigned char creature;
    unsigned char level;
    int64_t spell_or_flag;
    char name[96];
};

static struct FTestGolden s_golden;
static struct GoldenCase s_cases[GOLDEN_CASES_MAX];
static int64_t s_count;
static int64_t s_next;
static int64_t s_turns_left;
static struct Thing *s_turns_thing;
static TbBool s_turns_affected;
static ThingModel s_models[GCr_COUNT];

static const struct FTestGoldenExpected golden_expected[] = {
#include "ftest_spell_effects_golden.inc"
    {NULL, 0}
};

/** The first creature model with no immunities, not illuminated, that flies or not (and, when it doesn't fly, can
 *  be frightened); 0 if there's none. */
static ThingModel golden_pick_model(TbBool flying)
{
    for (ThingModel model = 1; model < kfx_config_state.conf.crtr_conf.model_count; model++)
    {
        const struct CreatureModelConfig *crconf = creature_stats_get(model);
        if ((crconf->immunity_flags != 0) || crconf->illuminated || (!crconf->flying != !flying))
            continue;
        if (!flying && (crconf->fear_stronger <= 0))
            continue;
        return model;
    }
    return 0;
}

static void golden_add_case(unsigned char kind, unsigned char creature, unsigned char level, int64_t spell_or_flag)
{
    if ((s_count >= GOLDEN_CASES_MAX) || (s_models[creature] == 0))
        return;
    struct GoldenCase *c = &s_cases[s_count++];
    c->kind = kind;
    c->creature = creature;
    c->level = level;
    c->spell_or_flag = spell_or_flag;
    if ((kind == GCK_SetFlag) || (kind == GCK_ClearFlag) || (kind == GCK_FlagTurnsClear))
        snprintf(c->name, sizeof(c->name), "%s 0x%06" PRIx64 " %s L%d", golden_kind_names[kind], (uint64_t)spell_or_flag,
            golden_creature_names[creature], (int)level);
    else
        snprintf(c->name, sizeof(c->name), "%s %s %s L%d", golden_kind_names[kind], spell_code_name(spell_or_flag),
            golden_creature_names[creature], (int)level);
}

static void golden_make_cases(void)
{
    s_models[GCr_Walker] = golden_pick_model(false);
    s_models[GCr_Flier] = golden_pick_model(true);
    s_models[GCr_Immune] = s_models[GCr_Walker];
    for (int c = 0; c < GCr_COUNT; c++)
        FTESTLOG("%s: %s", golden_creature_names[c], (s_models[c] != 0) ? creature_code_name(s_models[c]) : "none");
    s_count = 0;
    for (const struct NamedCommand *n = spell_desc; n->name != NULL; n++)
    {
        const SpellKind spell = n->num;
        if ((spell <= 0) || (n->name[0] == '\0'))
            continue;
        for (unsigned char creature = 0; creature < GCr_COUNT; creature++)
        {
            golden_add_case(GCK_Apply, creature, 0, spell);
            golden_add_case(GCK_Apply, creature, SPELL_MAX_LEVEL, spell);
            golden_add_case(GCK_Terminate, creature, 0, spell);
        }
        golden_add_case(GCK_Reapply, GCr_Walker, 0, spell);
        golden_add_case(GCK_Reapply, GCr_Flier, 0, spell);
        golden_add_case(GCK_Turns, GCr_Walker, 0, spell);
    }
    for (int f = 0; f < GOLDEN_FLAGS_COUNT; f++)
    {
        for (unsigned char creature = 0; creature < GCr_COUNT; creature++)
        {
            golden_add_case(GCK_SetFlag, creature, 0, (int64_t)1 << f);
            golden_add_case(GCK_ClearFlag, creature, 0, (int64_t)1 << f);
        }
        golden_add_case(GCK_FlagTurnsClear, GCr_Walker, 0, (int64_t)1 << f);
    }
}

/** A creature of the case's model and level, owned by player 0, next to its dungeon heart. */
static struct Thing *golden_make_creature(const struct GoldenCase *c)
{
    const struct Dungeon *dungeon = get_players_num_dungeon(PLAYER0);
    const struct Thing *heart = thing_get(dungeon->dnheart_idx);
    if (!thing_exists(heart))
        return INVALID_THING;
    struct Coord3d pos = heart->mappos;
    pos.x.val += 4 * COORD_PER_STL;
    pos.z.val = 0;
    if (c->creature == GCr_Immune)
        creature_stats_get(s_models[c->creature])->immunity_flags = ~(uint64_t)0;
    struct Thing *thing = create_creature(&pos, s_models[c->creature], PLAYER0);
    if (thing_is_invalid(thing))
        return INVALID_THING;
    pos.z.val = get_thing_height_at(thing, &pos);
    if (thing_in_wall_at(thing, &pos))
    {
        delete_thing_structure(thing, 0);
        return INVALID_THING;
    }
    thing->mappos = pos;
    remove_first_creature(thing);
    set_first_creature(thing);
    set_start_state(thing);
    set_creature_level(thing, c->level);
    return thing;
}

/** Runs the case up to its check (or, for GCK_Turns, up to the turns it waits); false if it couldn't start. */
static TbBool golden_run_case(const struct GoldenCase *c)
{
    if (!ftest_golden_load(&s_golden))
    {
        FTEST_FAIL_TEST("load_game failed");
        return false;
    }
    struct Thing *thing = golden_make_creature(c);
    if (thing_is_invalid(thing))
    {
        FTEST_FAIL_TEST("no %s creature next to player 0's heart", golden_creature_names[c->creature]);
        return false;
    }
    char result[48] = "";
    switch (c->kind)
    {
    case GCK_Apply:
    case GCK_Turns:
        apply_spell_effect_to_thing(thing, c->spell_or_flag, c->level, PLAYER1);
        break;
    case GCK_Reapply:
        apply_spell_effect_to_thing(thing, c->spell_or_flag, c->level, PLAYER1);
        apply_spell_effect_to_thing(thing, c->spell_or_flag, c->level, PLAYER1);
        break;
    case GCK_Terminate:
        apply_spell_effect_to_thing(thing, c->spell_or_flag, c->level, PLAYER1);
        terminate_thing_spell_effect(thing, c->spell_or_flag);
        break;
    case GCK_SetFlag:
    case GCK_ClearFlag:
    case GCK_FlagTurnsClear:
    {
        // the first spell's config carries the one flag while it's set (its other values are what the effects read)
        struct SpellConfig *spconf = get_spell_config(1);
        const uint64_t kept = spconf->spell_flags;
        spconf->spell_flags = (uint64_t)c->spell_or_flag;
        const TbBool affected = set_thing_spell_flags(thing, 1, 100, c->level);
        spconf->spell_flags = kept;
        snprintf(result, sizeof(result), "set %d", (int)affected);
        s_turns_affected = affected;
        if (c->kind == GCK_ClearFlag)
        {
            const TbBool cleared = clear_thing_spell_flags(thing, (uint64_t)c->spell_or_flag);
            snprintf(result, sizeof(result), "set %d clear %d", (int)affected, (int)cleared);
        }
        break;
    }
    }
    if ((c->kind == GCK_Turns) || (c->kind == GCK_FlagTurnsClear))
    {
        s_turns_thing = thing;
        s_turns_left = GOLDEN_TURNS;
        return true;
    }
    if (result[0] != '\0')
        ftest_golden_check_result(&s_golden, c->name, result);
    else
        ftest_golden_check(&s_golden, c->name);
    return true;
}

FTestActionResult ftest_spell_effects_golden_action001(struct FTestActionArgs* const args)
{
    if (!s_golden.saved)
    {
        if (!ftest_golden_begin(&s_golden, GOLDEN_SAVE_SLOT, golden_expected))
            return FTRs_Go_To_Next_Action;
        golden_make_cases();
        FTESTLOG("%" PRId64 " spell effect cases", s_count);
    }
    // a case waiting for its game turns: one passes per call
    if (s_turns_left > 0)
    {
        if (--s_turns_left > 0)
            return FTRs_Repeat_Current_Action;
        const struct GoldenCase *c = &s_cases[s_next];
        if (c->kind == GCK_FlagTurnsClear)
        {
            // the creature may be gone (killed in the fights, or exploded)
            TbBool cleared = false;
            if (thing_exists(s_turns_thing) && thing_is_creature(s_turns_thing))
                cleared = clear_thing_spell_flags(s_turns_thing, (uint64_t)c->spell_or_flag);
            char result[48];
            snprintf(result, sizeof(result), "set %d clear %d", (int)s_turns_affected, (int)cleared);
            ftest_golden_check_result(&s_golden, c->name, result);
        }
        else
        {
            ftest_golden_check(&s_golden, c->name);
        }
        s_next++;
    }
    // a batch of cases per turn (each loads the save, so the turn's own processing doesn't matter)
    for (int64_t batch = 0; (batch < 8) && (s_next < s_count); batch++)
    {
        if (!ftest_golden_selected(s_cases[s_next].name))
        {
            s_next++;
            continue;
        }
        if (!golden_run_case(&s_cases[s_next]))
            return FTRs_Go_To_Next_Action;
        if (s_turns_left > 0)
            return FTRs_Repeat_Current_Action;
        s_next++;
    }
    if (s_next < s_count)
        return FTRs_Repeat_Current_Action;
    ftest_golden_finish(&s_golden, "spell effect");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_spell_effects_golden_init()
{
    memset(&s_golden, 0, sizeof(s_golden));
    s_count = 0;
    s_next = 0;
    s_turns_left = 0;
    s_turns_thing = NULL;
    s_turns_affected = false;
    ftest_append_action(ftest_spell_effects_golden_action001, 30, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
