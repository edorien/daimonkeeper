// Refactor pass 4, S09: golden hashes of what each legacy script command does when it runs. The legacy commands
// (lvl_script_commands_old.c's command_*() and script_process_value()'s switch) become check/process pairs in the
// command table; these hashes must not change when they do. (What the checks store is pass 3's
// script_command_checks golden; this is what running them does, on a real level.)
//
// keeporig level 11 is saved once; each case loads that save, runs one script line the way Lua's
// Run_DKScript_command and the API do (script_scan_line(line, false, version)), and hashes kfx_sim_state,
// kfx_game_state, kfx_config_state and the intralevel data. Lines take the level's own names (creatures, rooms,
// powers, traps, doors, action points), valid and not, one player and all players; and every creature property,
// set and cleared. ALLY_PLAYERS also runs after a line that puts a locked door of player 0 on the level and allies
// player 0 with the heroes (the only other player there: an absent player can't be an ally): a locked door's
// navigation colour for another player depends on their alliance, so that case sees the navigation update
// ALLY_PLAYERS does.
//
// Each line also runs a second time inside an IF that is true (IF(PLAYER0,GAME_TURN >= 0) ... ENDIF), so it is stored
// rather than run: "store:" hashes what it stored (a script VALUE or trigger: saved games hold these, so their layout
// must not change either), "run:" what the level script processing (process_level_script()) then does with it.
//
// The hashing is ftest_golden.h's. To print the hashes (only on a commit meant to change what a command does):
// KFX_FTEST_GOLDEN_PRINT=1; the lines are logged with the prefix "GOLDEN:".
#include "ftest_script_legacy_golden.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_golden.h"

#include "config_crtrmodel.h"
#include "config_keeperfx.h"
#include "thing_doors.h"
#include "kfx_sim_state.h"
#include "player_data.h"
#include "lvl_script.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GOLDEN_SAVE_SLOT 6

struct GoldenCase {
    const char *line;
    int64_t version;
    const char *setup; /**< a line run at once before the case (and its IF), or NULL */
};

static const struct GoldenCase golden_cases[] = {
#define V1(l) {l, 1, NULL},
#define V01(l) {l, 0, NULL}, {l, 1, NULL},
    // availability and research
    V1("ROOM_AVAILABLE(PLAYER0,GUARD_POST,1,1)") V1("ROOM_AVAILABLE(ALL_PLAYERS,TEMPLE,0,0)") V1("ROOM_AVAILABLE(PLAYER0,NO_ROOM,1,1)")
    V01("CREATURE_AVAILABLE(PLAYER0,DRAGON,1,1)") V01("CREATURE_AVAILABLE(ALL_PLAYERS,WIZARD,0,1)") V1("CREATURE_AVAILABLE(PLAYER0,NOBODY,1,1)")
    V1("MAGIC_AVAILABLE(PLAYER0,POWER_LIGHTNING,1,1)") V1("MAGIC_AVAILABLE(ALL_PLAYERS,POWER_HEAL_CREATURE,0,1)") V1("MAGIC_AVAILABLE(PLAYER0,NO_POWER,1,1)")
    V1("TRAP_AVAILABLE(PLAYER0,BOULDER,1,2)") V1("TRAP_AVAILABLE(ALL_PLAYERS,ALARM,0,0)") V1("TRAP_AVAILABLE(PLAYER0,NO_TRAP,1,1)")
    V1("DOOR_AVAILABLE(PLAYER0,BRACED,1,3)") V1("DOOR_AVAILABLE(ALL_PLAYERS,WOOD,0,1)") V1("DOOR_AVAILABLE(PLAYER0,NO_DOOR,1,1)")
    V1("RESEARCH(PLAYER0,ROOM,WORKSHOP,5000)") V1("RESEARCH(PLAYER0,MAGIC,POWER_SPEED,100)") V1("RESEARCH(PLAYER0,NOTHING,LIBRARY,1)")
    V1("RESEARCH_ORDER(PLAYER0,ROOM,WORKSHOP,200)") V1("RESEARCH_ORDER(ALL_PLAYERS,MAGIC,POWER_CALL_TO_ARMS,300)")
    // money, limits, flags, timers
    V1("START_MONEY(PLAYER0,12345)") V1("START_MONEY(ALL_PLAYERS,500)") V1("ADD_GOLD_TO_PLAYER(PLAYER0,777)") V1("ADD_GOLD_TO_PLAYER(PLAYER0,-300)")
    V1("MAX_CREATURES(PLAYER0,5)") V1("MAX_CREATURES(ALL_PLAYERS,40)")
    V1("SET_FLAG(PLAYER0,FLAG3,9)") V1("SET_FLAG(ALL_PLAYERS,FLAG1,2)") V1("SET_FLAG(PLAYER0,FLAG3,3000000000)") V1("ADD_TO_FLAG(PLAYER0,FLAG3,4)") V1("SET_FLAG(PLAYER0,NOT_A_FLAG,1)")
    V1("SET_CAMPAIGN_FLAG(PLAYER0,CAMPAIGN_FLAG2,8)") V1("ADD_TO_CAMPAIGN_FLAG(PLAYER0,CAMPAIGN_FLAG2,3)")
    V1("EXPORT_VARIABLE(PLAYER0,MONEY,CAMPAIGN_FLAG4)") V1("EXPORT_VARIABLE(PLAYER0,NO_VAR,CAMPAIGN_FLAG4)")
    V1("RANDOMISE_FLAG(PLAYER0,FLAG5,10)") V1("RANDOMISE_FLAG(ALL_PLAYERS,FLAG5,0)")
    V1("COMPUTE_FLAG(PLAYER0,FLAG6,SET,PLAYER0,MONEY,0)") V1("COMPUTE_FLAG(PLAYER0,FLAG6,INCREASE,PLAYER0,TOTAL_CREATURES,1)")
    V1("COMPUTE_FLAG(PLAYER0,FLAG6,NO_OP,PLAYER0,MONEY,0)")
    V1("SET_TIMER(PLAYER0,TIMER2)") V1("SET_TIMER(ALL_PLAYERS,TIMER0)") V1("SET_TIMER(PLAYER0,NO_TIMER)")
    V1("BONUS_LEVEL_TIME(2000)") V1("BONUS_LEVEL_TIME(1500,1)") V1("BONUS_LEVEL_TIME(0)")
    V1("DEAD_CREATURES_RETURN_TO_POOL(1)") V1("DEAD_CREATURES_RETURN_TO_POOL(5)") V1("DEAD_CREATURES_RETURN_TO_POOL(0)")
    V1("CREATURE_ENTRANCE_LEVEL(PLAYER0,3)") V1("MAKE_SAFE(PLAYER0)") V1("MAKE_UNSAFE(PLAYER0)") V1("LOCATE_HIDDEN_WORLD")
    V1("ALLY_PLAYERS(PLAYER0,PLAYER1,1)") V1("ALLY_PLAYERS(PLAYER0,PLAYER1,0)")
    V1("SET_CREATURE_TENDENCIES(PLAYER0,IMPRISON,1)") V1("SET_CREATURE_TENDENCIES(PLAYER0,FLEE,0)") V1("SET_CREATURE_TENDENCIES(PLAYER0,NOTHING,1)")
    V1("REVEAL_MAP_RECT(PLAYER0,40,40,10,8)") V1("REVEAL_MAP_RECT(ALL_PLAYERS,120,90,30,30)")
    // creature configuration and pool
    V1("ADD_CREATURE_TO_POOL(TROLL,3)") V1("ADD_CREATURE_TO_POOL(NOBODY,3)")
    V1("SET_CREATURE_HEALTH(TROLL,900)") V1("SET_CREATURE_STRENGTH(TROLL,77)") V1("SET_CREATURE_ARMOUR(TROLL,66)")
    V01("SET_CREATURE_FEAR_WOUNDED(TROLL,40)") V1("SET_CREATURE_FEAR_STRONGER(TROLL,5000)") V1("SET_CREATURE_FEARSOME_FACTOR(TROLL,150)")
    V1("SET_CREATURE_PROPERTY(TROLL,BLEEDS,0)") V1("SET_CREATURE_PROPERTY(TROLL,FLYING,1)") V1("SET_CREATURE_PROPERTY(TROLL,NO_PROPERTY,1)")
    V1("SET_CREATURE_HEALTH(NOBODY,900)")
    // creatures in the level
    V1("KILL_CREATURE(PLAYER0,ANY_CREATURE,MOST_EXPERIENCED,1)") V1("KILL_CREATURE(PLAYER0,ANY_CREATURE,LEAST_EXPERIENCED,2)") V1("KILL_CREATURE(ALL_PLAYERS,ANY_CREATURE,ANY,1)") V1("KILL_CREATURE(PLAYER0,NOBODY,ANY,1)")
    V1("LEVEL_UP_CREATURE(PLAYER0,ANY_CREATURE,LEAST_EXPERIENCED,2)") V1("LEVEL_UP_CREATURE(ALL_PLAYERS,ANY_CREATURE,ANY,1)")
    V1("CHANGE_CREATURE_OWNER(PLAYER0,ANY_CREATURE,ANY,PLAYER1)") V1("CHANGE_CREATURE_OWNER(PLAYER0,NOBODY,ANY,PLAYER1)")
    V1("USE_POWER_ON_CREATURE(PLAYER0,ANY_CREATURE,ANY,PLAYER0,POWER_SPEED,3,1)") V1("USE_POWER_ON_CREATURE(PLAYER0,ANY_CREATURE,MOST_EXPERIENCED,PLAYER0,POWER_HEAL_CREATURE,1,0)")
    V1("USE_SPELL_ON_CREATURE(PLAYER0,ANY_CREATURE,ANY,SPELL_SPEED,3)") V1("USE_SPELL_ON_CREATURE(PLAYER0,IMP,ANY,NO_SPELL,3)")
    V1("USE_POWER_AT_POS(PLAYER0,60,60,POWER_LIGHTNING,2,1)") V1("USE_POWER_AT_POS(PLAYER0,60,60,NO_POWER,2,1)")
    V1("USE_POWER_AT_LOCATION(PLAYER0,PLAYER0,POWER_CALL_TO_ARMS,1,1)") V1("USE_POWER_AT_LOCATION(PLAYER0,1,POWER_LIGHTNING,1,1)")
    V1("USE_POWER(PLAYER0,POWER_OBEY,1)") V1("USE_POWER(PLAYER0,POWER_ARMAGEDDON,0)") V1("USE_POWER(PLAYER0,NO_POWER,1)")
    V1("USE_SPECIAL_INCREASE_LEVEL(PLAYER0,1)") V1("USE_SPECIAL_INCREASE_LEVEL(PLAYER0,-1)") V1("USE_SPECIAL_INCREASE_LEVEL(ALL_PLAYERS,2)") V1("USE_SPECIAL_MULTIPLY_CREATURES(PLAYER0,1)")
    V1("COMPUTER_DIG_TO_LOCATION(PLAYER1,PLAYER1,PLAYER0)")
    // adding creatures and parties
    V1("ADD_CREATURE_TO_LEVEL(PLAYER0,TROLL,PLAYER0,2,3,100)") V1("ADD_CREATURE_TO_LEVEL(PLAYER_GOOD,KNIGHT,1,1,5,0)")
    V1("ADD_CREATURE_TO_LEVEL(PLAYER0,NOBODY,PLAYER0,1,1,0)") V1("ADD_CREATURE_TO_LEVEL(PLAYER0,TROLL,NOWHERE,1,1,0)")
    V1("ADD_TUNNELLER_TO_LEVEL(PLAYER_GOOD,1,DUNGEON,0,2,100)") V1("ADD_TUNNELLER_TO_LEVEL(PLAYER_GOOD,-1,DUNGEON_HEART,0,1,0)")
    V1("CREATE_PARTY(GOLDEN_PARTY)")
    // parse-time commands
    V1("WIN_GAME") V1("LOSE_GAME") V1("NEXT_COMMAND_REUSABLE") V1("RUN_AFTER_VICTORY(1)") V1("LEVEL_VERSION(1)")
    V1("PRINT(\"golden\")") V1("IF_ACTION_POINT(1,PLAYER0)") V1("IF_SLAB_OWNER(10,10,PLAYER0)") V1("IF_SLAB_TYPE(10,10,DIRT)") V1("ENDIF")
#undef V1
#undef V01
};

#define GOLDEN_CASES_MAX 256

static struct FTestGolden s_golden;
static int64_t s_next;
/** The cases: golden_cases[], then SET_CREATURE_PROPERTY for each property, set and cleared, on the imp. */
static struct GoldenCase s_cases[GOLDEN_CASES_MAX];
static char s_case_lines[GOLDEN_CASES_MAX][96];
static int64_t s_ncases;
/** PLACE_DOOR of a locked door of player 0, on the first slab (in row order) where one can go. */
static char s_locked_door_line[96];

static TbBool golden_find_locked_door_line(void)
{
    for (MapSlabCoord slb_y = 0; slb_y < kfx_sim_state.map_tiles_y; slb_y++)
        for (MapSlabCoord slb_x = 0; slb_x < kfx_sim_state.map_tiles_x; slb_x++)
            if (door_placement_allowed(PLAYER0, slab_subtile_center(slb_x), slab_subtile_center(slb_y)))
            {
                snprintf(s_locked_door_line, sizeof(s_locked_door_line), "PLACE_DOOR(PLAYER0,WOOD,%d,%d,LOCKED,FREE)",
                    (int)slb_x, (int)slb_y);
                return true;
            }
    return false;
}

static void golden_make_cases(void)
{
    s_ncases = 0;
    for (size_t i = 0; (i < sizeof(golden_cases) / sizeof(golden_cases[0])) && (s_ncases < GOLDEN_CASES_MAX); i++)
        s_cases[s_ncases++] = golden_cases[i];
    for (int ally = 1; (ally >= 0) && (s_ncases < GOLDEN_CASES_MAX); ally--)
    {
        snprintf(s_case_lines[s_ncases], sizeof(s_case_lines[0]), "ALLY_PLAYERS(PLAYER0,PLAYER_GOOD,%d)", ally);
        s_cases[s_ncases].line = s_case_lines[s_ncases];
        s_cases[s_ncases].version = 1;
        s_cases[s_ncases].setup = s_locked_door_line;
        s_ncases++;
    }
    for (const struct NamedCommand *p = creatmodel_properties_commands; p->name != NULL; p++)
    {
        for (int on = 1; (on >= 0) && (s_ncases < GOLDEN_CASES_MAX); on--)
        {
            snprintf(s_case_lines[s_ncases], sizeof(s_case_lines[0]), "SET_CREATURE_PROPERTY(IMP,%s,%d)", p->name, on);
            s_cases[s_ncases].line = s_case_lines[s_ncases];
            s_cases[s_ncases].version = 1;
            s_cases[s_ncases].setup = NULL;
            s_ncases++;
        }
    }
}

static const struct FTestGoldenExpected golden_expected[] = {
#include "ftest_script_legacy_golden.inc"
    {NULL, 0}
};

/** Loads the save, runs the case's line (stored, inside a true IF, if store) and checks the hash; stored, also
 *  checks what running the level script does with it. */
static TbBool golden_run_case(const struct GoldenCase *c, TbBool store)
{
    if (!ftest_golden_selected(c->line))
        return true;
    if (!ftest_golden_load(&s_golden))
        return false;
    char line[256];
    char name[400];
    char setup[120] = "";
    if (c->setup != NULL)
    {
        snprintf(line, sizeof(line), "%s", c->setup);
        script_scan_line(line, false, 1);
        snprintf(setup, sizeof(setup), " after %s", c->setup);
    }
    if (store)
    {
        snprintf(line, sizeof(line), "IF(PLAYER0,GAME_TURN >= 0)");
        script_scan_line(line, false, c->version);
    }
    snprintf(line, sizeof(line), "%s", c->line);
    script_scan_line(line, false, c->version);
    if (store)
    {
        snprintf(line, sizeof(line), "ENDIF");
        script_scan_line(line, false, c->version);
    }
    snprintf(name, sizeof(name), "%sv%d:%s%s", store ? "store:" : "", (int)c->version, c->line, setup);
    ftest_golden_check(&s_golden, name);
    if (store)
    {
        process_level_script();
        snprintf(name, sizeof(name), "run:v%d:%s%s", (int)c->version, c->line, setup);
        ftest_golden_check(&s_golden, name);
    }
    return true;
}

FTestActionResult ftest_script_legacy_golden_action001(struct FTestActionArgs* const args)
{
    if (!s_golden.saved)
    {
        if (!ftest_golden_begin(&s_golden, GOLDEN_SAVE_SLOT, golden_expected))
            return FTRs_Go_To_Next_Action;
        if (!golden_find_locked_door_line())
        {
            FTEST_FAIL_TEST("no slab for a door of player 0");
            return FTRs_Go_To_Next_Action;
        }
        golden_make_cases();
        FTESTLOG("keeporig 11: %" PRId64 " action points, player 0 has %" PRId64 " creatures",
            (int64_t)(action_point_exists_idx(1) + action_point_exists_idx(2) + action_point_exists_idx(3)),
            (int64_t)get_players_num_dungeon(0)->num_active_creatrs);
    }
    // a batch of cases per turn (each loads the save, so the turn's own processing doesn't matter); all of them run
    // at once, then all of them stored
    const int64_t ncases = s_ncases;
    for (int64_t batch = 0; (batch < 8) && (s_next < 2 * ncases); batch++, s_next++)
    {
        if (!golden_run_case(&s_cases[s_next % ncases], (s_next >= ncases)))
        {
            FTEST_FAIL_TEST("load_game failed");
            return FTRs_Go_To_Next_Action;
        }
    }
    if (s_next < 2 * ncases)
        return FTRs_Repeat_Current_Action;
    ftest_golden_finish(&s_golden, "legacy script command");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_script_legacy_golden_init()
{
    memset(&s_golden, 0, sizeof(s_golden));
    s_next = 0;
    s_ncases = 0;
    ftest_append_action(ftest_script_legacy_golden_action001, 30, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
