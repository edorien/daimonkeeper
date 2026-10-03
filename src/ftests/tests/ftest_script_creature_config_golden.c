// Refactor pass 4, S03: golden hashes of what SET_CREATURE_CONFIGURATION does to a creature model and the creatures
// of it. Its process becomes table-driven (each key assigned through the creature file's NamedField row, the keys
// that do more through a hook); these hashes must not change when it does.
//
// keeporig level 11 is saved once; each case loads that save and runs one SET_CREATURE_CONFIGURATION line on the
// imp (player 0 has imps on the map, so the keys that update the creatures of the model have some to update). The
// keys are the script's own, block by block, as it looks them up; each gets numbers (ordinary, zero, negative, out
// of range, with the extra values the multi-value keys take) and, where the key's row in the creature file takes
// names, names from that row's list with the set/add/clear argument; then lines with names from the level's own
// lists, and, for each model player 0 has creatures of, the keys that update those creatures (a lair object
// change removes their lairs). The hashing is ftest_golden.h's.
//
// To print the hashes (only on a commit meant to change what a key does): KFX_FTEST_GOLDEN_PRINT=1.
#include "ftest_script_creature_config_golden.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_golden.h"

#include "config.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
#include "config_keeperfx.h"
#include "config_objects.h"
#include "config_terrain.h"
#include "creature_control.h"
#include "kfx_sim_state.h"
#include "lvl_script.h"
#include "thing_data.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GOLDEN_SAVE_SLOT 7
#define GOLDEN_LINES_MAX 1024

/** The script's key tables, in the order SET_CREATURE_CONFIGURATION looks a key up. */
static const struct NamedCommand *const golden_key_tables[] = {
    creatmodel_attributes_commands, creatmodel_jobs_commands, creatmodel_attraction_commands, creatmodel_sounds_commands,
    creature_graphics_desc, creatmodel_annoyance_commands, creatmodel_experience_commands, creatmodel_appearance_commands,
    creatmodel_senses_commands,
};

/** The creature file's rows, to find the names a key takes. */
static const struct NamedField *const golden_row_tables[] = {
    creaturemodel_attributes_named_fields, creaturemodel_attraction_named_fields, creaturemodel_annoyance_named_fields,
    creaturemodel_senses_named_fields, creaturemodel_appearance_named_fields, creaturemodel_experience_named_fields,
    creaturemodel_jobs_named_fields, creaturemodel_sprites_named_fields, creaturemodel_sounds_named_fields,
};

static const char *const golden_numbers[] = {"7,3,2", "0", "-5,-5,-5", "100000,70000,70000"};

/** Lines for the keys whose values aren't plain numbers or a name from their row's list. */
static const char *const golden_extra_lines[] = {
    "SET_CREATURE_CONFIGURATION(IMP,HOSTILETOWARDS,ANY_CREATURE)",
    "SET_CREATURE_CONFIGURATION(IMP,HOSTILETOWARDS,NULL)",
    "SET_CREATURE_CONFIGURATION(IMP,LAIRENEMY,TROLL,ANY_CREATURE,NULL)",
    "SET_CREATURE_CONFIGURATION(IMP,LAIRENEMY,NOT_A_CREATURE)",
    "SET_CREATURE_CONFIGURATION(IMP,GROWUP,5000,TROLL,3)",
    "SET_CREATURE_CONFIGURATION(IMP,GROWUP,5000,NULL)",
    "SET_CREATURE_CONFIGURATION(IMP,SLEEPEXPERIENCE,PATH,10,EARTH,5,LAVA,3)",
    "SET_CREATURE_CONFIGURATION(IMP,SLEEPEXPERIENCE,NOT_A_SLAB,10)",
    "SET_CREATURE_CONFIGURATION(IMP,POWERS,FIREBALL,2)",
    "SET_CREATURE_CONFIGURATION(IMP,POWERS,NOT_AN_INSTANCE,2)",
    "SET_CREATURE_CONFIGURATION(IMP,ENTRANCEROOM,TEMPLE,LAIR,NOT_A_ROOM)",
    "SET_CREATURE_CONFIGURATION(IMP,NOT_A_KEY,1)",
    "SET_CREATURE_CONFIGURATION(NOBODY,HEALTH,500)",
    "SET_CREATURE_CONFIGURATION(TROLL,HEALTH,500)",
    "SET_CREATURE_CONFIGURATION(TROLL,BASESPEED,40)",
};

static struct FTestGolden s_golden;
static int64_t s_next;
static int64_t s_count;
static char s_lines[GOLDEN_LINES_MAX][128];

static const struct FTestGoldenExpected golden_expected[] = {
#include "ftest_script_creature_config_golden.inc"
    {NULL, 0}
};

/** The name list of the creature file's row named key, if it has one. */
static const struct NamedCommand *golden_row_names(const char *key)
{
    for (size_t t = 0; t < sizeof(golden_row_tables) / sizeof(golden_row_tables[0]); t++)
        for (const struct NamedField *f = golden_row_tables[t]; f->name != NULL; f++)
            if ((strcasecmp(f->name, key) == 0) && (f->namedCommand != NULL))
                return f->namedCommand;
    return NULL;
}

static void golden_add_line(const char *key, const char *values)
{
    if (s_count < GOLDEN_LINES_MAX)
        snprintf(s_lines[s_count++], sizeof(s_lines[0]), "SET_CREATURE_CONFIGURATION(IMP,%s,%s)", key, values);
}

static void golden_make_lines(void)
{
    s_count = 0;
    for (size_t t = 0; t < sizeof(golden_key_tables) / sizeof(golden_key_tables[0]); t++)
    {
        for (const struct NamedCommand *k = golden_key_tables[t]; k->name != NULL; k++)
        {
            // a key in two tables is found in the first: its lines are made there
            TbBool seen = false;
            for (size_t u = 0; (u < t) && !seen; u++)
                seen = (get_id(golden_key_tables[u], k->name) != -1);
            if (seen)
                continue;
            for (size_t n = 0; n < sizeof(golden_numbers) / sizeof(golden_numbers[0]); n++)
                golden_add_line(k->name, golden_numbers[n]);
            const struct NamedCommand *names = golden_row_names(k->name);
            if ((names != NULL) && (names[0].name != NULL))
            {
                const char *name = (names[1].name != NULL) ? names[1].name : names[0].name;
                char values[96];
                golden_add_line(k->name, name);
                snprintf(values, sizeof(values), "%s,1", name);
                golden_add_line(k->name, values);
                snprintf(values, sizeof(values), "%s,0", name);
                golden_add_line(k->name, values);
                if ((names[1].name != NULL) && (names[2].name != NULL))
                    golden_add_line(k->name, names[2].name);
            }
        }
    }
    // names from the level's lists
    char values[128];
    if ((room_desc[2].name != NULL) && (room_desc[3].name != NULL) && (room_desc[4].name != NULL))
    {
        snprintf(values, sizeof(values), "%s,%s,%s", room_desc[2].name, room_desc[3].name, room_desc[4].name);
        golden_add_line("ENTRANCEROOM", values);
    }
    if ((slab_desc[1].name != NULL) && (slab_desc[2].name != NULL) && (slab_desc[3].name != NULL))
    {
        snprintf(values, sizeof(values), "%s,10,%s,5,%s,3", slab_desc[1].name, slab_desc[2].name, slab_desc[3].name);
        golden_add_line("SLEEPEXPERIENCE", values);
    }
    // the models of player 0's creatures (up to four), with the keys that update the creatures of the model
    ThingModel models[4];
    int64_t nmodels = 0;
    for (int64_t i = 1; (i < THINGS_COUNT) && (nmodels < 4); i++)
    {
        const struct Thing *thing = thing_get(i);
        if (!thing_exists(thing) || !thing_is_creature(thing) || (thing->owner != PLAYER0))
            continue;
        int64_t m = 0;
        while ((m < nmodels) && (models[m] != thing->model))
            m++;
        if (m == nmodels)
            models[nmodels++] = thing->model;
    }
    for (int64_t m = 0; (m < nmodels) && (s_count + 3 <= GOLDEN_LINES_MAX); m++)
    {
        const char *model = creature_code_name(models[m]);
        snprintf(s_lines[s_count++], sizeof(s_lines[0]), "SET_CREATURE_CONFIGURATION(%s,LAIROBJECT,%s)", model,
            (object_desc[1].name != NULL) ? object_desc[1].name : "1");
        snprintf(s_lines[s_count++], sizeof(s_lines[0]), "SET_CREATURE_CONFIGURATION(%s,HEALTH,500)", model);
        snprintf(s_lines[s_count++], sizeof(s_lines[0]), "SET_CREATURE_CONFIGURATION(%s,BASESPEED,40)", model);
    }
    for (size_t i = 0; (i < sizeof(golden_extra_lines) / sizeof(golden_extra_lines[0])) && (s_count < GOLDEN_LINES_MAX); i++)
        snprintf(s_lines[s_count++], sizeof(s_lines[0]), "%s", golden_extra_lines[i]);
}

FTestActionResult ftest_script_creature_config_golden_action001(struct FTestActionArgs* const args)
{
    if (!s_golden.saved)
    {
        if (!ftest_golden_begin(&s_golden, GOLDEN_SAVE_SLOT, golden_expected))
            return FTRs_Go_To_Next_Action;
        golden_make_lines();
        FTESTLOG("%" PRId64 " SET_CREATURE_CONFIGURATION lines", s_count);
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
    ftest_golden_finish(&s_golden, "SET_CREATURE_CONFIGURATION");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_script_creature_config_golden_init()
{
    memset(&s_golden, 0, sizeof(s_golden));
    s_next = 0;
    s_count = 0;
    ftest_append_action(ftest_script_creature_config_golden_action001, 30, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
