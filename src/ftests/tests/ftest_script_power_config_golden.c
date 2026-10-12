// Refactor pass 5, S06: golden hashes of what SET_POWER_CONFIGURATION does to a keeper power's configuration. Its
// check and process become table-driven (each key found by name, assigned through the power file's row, the keys that
// do more through a hook); these hashes must not change when they do.
//
// keeporig level 11 is saved once; each case loads that save and runs one SET_POWER_CONFIGURATION line on
// POWER_LIGHTNING. Every key of magic_power_commands gets numbers (ordinary, zero, negative, large; Power and Cost
// with a slot) and, where its value is a name, names from that key's list (the flag keys with the set and clear
// argument too) and a name that isn't in it; then an unknown power, an unknown key, the slots Power and Cost refuse.
// The hashing is ftest_golden.h's.
//
// To print the hashes (only on a commit meant to change what a key does): KFX_FTEST_GOLDEN_PRINT=1.
#include "ftest_script_power_config_golden.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_golden.h"

#include "config.h"
#include "config_creature.h"
#include "config_effects.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_objects.h"
#include "config_players.h"
#include "lvl_script.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GOLDEN_SAVE_SLOT 4
#define GOLDEN_LINES_MAX 512
#define GOLDEN_POWER "POWER_LIGHTNING"

static const char *const golden_numbers[] = {"7", "0", "-5", "100000"};
static const char *const golden_slot_numbers[] = {"7,1", "0,5", "-5,9", "100000,2"};

/** The names a key's value takes (NULL: numbers only). */
static const struct NamedCommand *golden_key_names(const char *key)
{
    if (strcasecmp(key, "ARTIFACT") == 0) return object_desc;
    if (strcasecmp(key, "PROPERTIES") == 0) return powermodel_properties_commands;
    if (strcasecmp(key, "CASTEXPANDFUNC") == 0) return powermodel_expand_check_func_type;
    if (strcasecmp(key, "PLAYERSTATE") == 0) return player_state_commands;
    if (strcasecmp(key, "PARENTPOWER") == 0) return power_desc;
    if (strcasecmp(key, "SPELL") == 0) return spell_desc;
    if (strcasecmp(key, "EFFECT") == 0) return effect_desc;
    if (strcasecmp(key, "USEFUNCTION") == 0) return magic_use_func_commands;
    if (strcasecmp(key, "CREATURETYPE") == 0) return creature_desc;
    if (strcasecmp(key, "COSTFORMULA") == 0) return magic_cost_formula_commands;
    return NULL;
}

/** Lines the generator doesn't make. */
static const char *const golden_extra_lines[] = {
    "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",SYMBOLSPRITES,10,20)",
    "SET_POWER_CONFIGURATION(NOT_A_POWER,COST,500,1)",
    "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",NOT_A_KEY,1)",
    "SET_POWER_CONFIGURATION(POWER_HAND,DURATION,300)",
    // a slot out of Power's 10 or Cost's 9, or none: refused (P5-F25; they wrote past the arrays)
    "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",POWER,7)",
    "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",POWER,7,0)",
    "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",POWER,7,11)",
    "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",POWER,7,10)",
    "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",COST,7,10)",
    "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",COST,7,9)",
    // a sprite by name, as the power file takes it (P5-F26)
    "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",POINTERSPRITES,NOT_A_SPRITE)",
};

static struct FTestGolden s_golden;
static int64_t s_next;
static int64_t s_count;
static char s_lines[GOLDEN_LINES_MAX][128];

static const struct FTestGoldenExpected golden_expected[] = {
#include "ftest_script_power_config_golden.inc"
    {NULL, 0}
};

static void golden_add_line(const char *key, const char *values)
{
    if (s_count < GOLDEN_LINES_MAX)
        snprintf(s_lines[s_count++], sizeof(s_lines[0]), "SET_POWER_CONFIGURATION(" GOLDEN_POWER ",%s,%s)", key, values);
}

/** A key whose value is a name: two names from its list (the flag keys also set and cleared) and one that isn't. */
static void golden_add_name_lines(const char *key, const char *name1, const char *name2, TbBool flags)
{
    char values[96];
    if (name1 != NULL)
    {
        golden_add_line(key, name1);
        if (flags)
        {
            snprintf(values, sizeof(values), "%s,1", name1);
            golden_add_line(key, values);
            snprintf(values, sizeof(values), "%s,0", name1);
            golden_add_line(key, values);
        }
    }
    if (name2 != NULL)
        golden_add_line(key, name2);
    golden_add_line(key, "NOT_A_NAME");
}

static void golden_make_lines(void)
{
    s_count = 0;
    for (const struct NamedCommand *k = magic_power_commands; k->name != NULL; k++)
    {
        const TbBool slot = (strcasecmp(k->name, "POWER") == 0) || (strcasecmp(k->name, "COST") == 0);
        for (size_t n = 0; n < sizeof(golden_numbers) / sizeof(golden_numbers[0]); n++)
            golden_add_line(k->name, slot ? golden_slot_numbers[n] : golden_numbers[n]);
        if (strcasecmp(k->name, "CASTABILITY") == 0)
        {
            golden_add_name_lines(k->name, powermodel_castability_commands[1].name, powermodel_castability_commands[2].name, true);
            continue;
        }
        const struct NamedCommand *names = golden_key_names(k->name);
        if ((names != NULL) && (names[0].name != NULL))
        {
            const TbBool flags = (strcasecmp(k->name, "PROPERTIES") == 0);
            const char *name1 = (names[1].name != NULL) ? names[1].name : names[0].name;
            const char *name2 = ((names[1].name != NULL) && (names[2].name != NULL)) ? names[2].name : NULL;
            golden_add_name_lines(k->name, name1, name2, flags);
        }
    }
    for (size_t i = 0; (i < sizeof(golden_extra_lines) / sizeof(golden_extra_lines[0])) && (s_count < GOLDEN_LINES_MAX); i++)
        snprintf(s_lines[s_count++], sizeof(s_lines[0]), "%s", golden_extra_lines[i]);
}

FTestActionResult ftest_script_power_config_golden_action001(struct FTestActionArgs* const args)
{
    if (!s_golden.saved)
    {
        if (!ftest_golden_begin(&s_golden, GOLDEN_SAVE_SLOT, golden_expected))
            return FTRs_Go_To_Next_Action;
        golden_make_lines();
        FTESTLOG("%" PRId64 " SET_POWER_CONFIGURATION lines", s_count);
    }
    // a batch of cases per turn (each loads the save, so the turn's own processing doesn't matter)
    for (int64_t batch = 0; (batch < 8) && (s_next < s_count); batch++, s_next++)
    {
        if (!ftest_golden_selected(s_lines[s_next]))
            continue;
        if (!ftest_golden_load(&s_golden))
        {
            FTEST_FAIL_TEST("load_game failed");
            return FTRs_Go_To_Next_Action;
        }
        char line[128];
        snprintf(line, sizeof(line), "%s", s_lines[s_next]);
        script_scan_line(line, false, 1);
        ftest_golden_check(&s_golden, s_lines[s_next]);
    }
    if (s_next < s_count)
        return FTRs_Repeat_Current_Action;
    ftest_golden_finish(&s_golden, "SET_POWER_CONFIGURATION");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_script_power_config_golden_init()
{
    memset(&s_golden, 0, sizeof(s_golden));
    s_next = 0;
    s_count = 0;
    ftest_append_action(ftest_script_power_config_golden_action001, 30, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
