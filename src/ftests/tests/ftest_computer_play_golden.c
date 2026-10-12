// Refactor pass 5, S03 target 4: what the computer players do, pinned. The keepers of a multiplayer level play by
// themselves (the local seat is handed to the built-in AI, as in spectator mode); right after the hand-over the level
// is saved, and at fixed turns after it the state that changed since the save is hashed (ftest_golden.h: bytes that
// differ between two loads of the save, pointers and wall-clock times, are left out) and compared with a table. The
// game is deterministic (sim_state_continuity's integrity digest is the same in every process), so a hash differs
// only when the computer players, or what they act through, behave differently. The first checkpoint that differs
// says from when on.
//
// computer_arena_golden: the same on a level set up for a fight -- original 50's two keepers, both the attacking
// computer (keepcompp.cfg's computer0), with gold, every room and power, a squad of levelled creatures at each heart
// and a corridor dug from one heart to the other (a Lua chunk in the level's state, before the save). The natural
// run of computer_play_golden builds dungeons; this one reaches what fighting runs: attacks, call to arms, the
// attack spells, prisoners and torture, rebuilding lost rooms.
//
// Environment, for investigating: KFX_COMP_TURNS=<t1>,<t2>,... (checkpoints, turns after the save); KFX_COMP_LEVEL=<n>
// (another level of the test's map pack: no hashes for it, the kinds of computer task its keepers start are logged);
// and the golden ones (KFX_FTEST_GOLDEN_PRINT, _DUMP, _ONLY; a run is one case, named after its level).
#include "ftest_computer_play_golden.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ftest.h"
#include "../ftest_golden.h"

#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "lua_base.h"
#include <lauxlib.h>
#include "slab_data.h"
#include "config_terrain.h"
#include "frontend.h"
#include "kfx_sim_state.h"
#include "player_data.h"
#include "player_computer.h"
#include "player_utils.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_SLOT 7
#define MAX_CHECKPOINTS 32

static const struct FTestGoldenExpected expected[] = {
#include "ftest_computer_play_golden.inc"
    {NULL, 0}
};

static struct FTestGolden s_golden;
static GameTurn s_saved_at;
static int64_t s_checkpoints[MAX_CHECKPOINTS];
static int64_t s_checkpoint_count;
static int64_t s_next;
static char s_case[64];
static uint64_t s_task_kinds; // bit per CTT_* kind a computer started
static TbBool s_arena;

/** The arena's dungeon, built for each keeper the way levels/dungeon_architect's Lua builds its start rooms (after
 *  "local P, HX, HY, DIR = 'PLAYERn', <heart slab x>, <heart slab y>, <1 or -1: towards the other keeper>"): a
 *  claimed floor around the heart, three rows of 5x5 rooms on it towards the other keeper. */
static const char arena_dungeon_lua[] =
    "local rows = {{0, {'LAIR_AREA', 'HATCHERY', nil, 'TREASURY_AREA', 'TRAINING_AREA'}},\n"
    "              {8, {'BOOK_SHELVES', 'PRISON_AREA', 'TORTURE_AREA', 'TEMPLE_POOL', 'GRAVE_AREA'}},\n"
    "              {16, {nil, 'WORKSHOP_AREA', 'BARRACK_AREA', 'SCAVENGE_AREA', nil}}}\n"
    "local function is_heart(x, y) return math.abs(x - HX) <= 2 and math.abs(y - HY) <= 2 end\n"
    "for x = HX - 19, HX + 19 do\n"
    "  for y = math.min(HY, HY + DIR * 19), math.max(HY, HY + DIR * 19) do\n"
    "    if not is_heart(x, y) then ChangeSlabType(x, y, 'PRETTY_PATH', 'NONE') ChangeSlabOwner(x, y, P, 'NONE') end\n"
    "  end\n"
    "end\n"
    "for _, row in ipairs(rows) do\n"
    "  for i = 1, 5 do\n"
    "    local kind = row[2][i]\n"
    "    if kind ~= nil then\n"
    "      local cx, cy = HX + (i - 3) * 8, HY + DIR * row[1]\n"
    "      for x = cx - 2, cx + 2 do for y = cy - 2, cy + 2 do ChangeSlabType(x, y, kind, 'NONE') end end\n"
    "    end\n"
    "  end\n"
    "end\n";

/** The arena's keeper, run after its dungeon (after "local P = 'PLAYERn'"). */
static const char arena_keeper_lua[] =
    "ComputerPlayer(P, 0)\n"
    "RunDKScriptCommand('ADD_GOLD_TO_PLAYER(' .. P .. ',30000)')\n"
    "for _, r in ipairs({'TREASURE','LAIR','GARDEN','RESEARCH','TRAINING','PRISON','TORTURE','BARRACKS','TEMPLE',\n"
    "                    'GRAVEYARD','WORKSHOP','SCAVENGER','BRIDGE','GUARD_POST'}) do RoomAvailable(P, r, 1, true) end\n"
    "for _, m in ipairs({'POWER_SLAP','POWER_SIGHT','POWER_CALL_TO_ARMS','POWER_CAVE_IN','POWER_HEAL_CREATURE',\n"
    "                    'POWER_HOLD_AUDIENCE','POWER_LIGHTNING','POWER_SPEED','POWER_PROTECT','POWER_CONCEAL',\n"
    "                    'POWER_DISEASE','POWER_CHICKEN','POWER_DESTROY_WALLS','POWER_FLIGHT','POWER_VISION'}) do\n"
    "    MagicAvailable(P, m, true, true)\n"
    "end\n"
    // keepcompp.cfg's attack processes wait for 80% of the creatures a keeper may attract, and 55% of those able to
    // fight to be at least 6
    "MaxCreatures(P, 12)\n"
    "for _, c in ipairs({{'DRAGON',5},{'BILE_DEMON',5},{'TROLL',4},{'TROLL',4},{'ORC',4},{'ORC',4},{'ORC',3},\n"
    "                    {'DARK_MISTRESS',5},{'SORCEROR',4},{'SPIDER',3},{'DEMONSPAWN',4},{'SKELETON',3}}) do\n"
    "    AddCreatureToLevel(P, c[1], P, c[2], 0)\n"
    "end\n";

static TbBool run_lua(const char *chunk)
{
    lua_State *L = Lvl_script;
    if (L == NULL)
    {
        FTEST_FAIL_TEST("the level has no Lua state");
        return false;
    }
    const int top = lua_gettop(L);
    const TbBool ok = (luaL_dostring(L, chunk) == 0);
    if (!ok)
        FTEST_FAIL_TEST("arena set-up: %s", lua_tostring(L, -1));
    lua_settop(L, top);
    return ok;
}

/** A slab a creature can't walk through, which the corridor replaces with path; rooms stay (the heart's pedestal
 *  blocks too, and a keeper without its heart room does nothing). */
static TbBool slab_blocks_corridor(MapSlabCoord slb_x, MapSlabCoord slb_y)
{
    const struct SlabMap *slb = get_slabmap_block(slb_x, slb_y);
    if (slb->room_index != 0)
        return false;
    const struct SlabConfigStats *slabst = get_slab_kind_stats(slb->kind);
    return ((slabst->block_flags & SlbAtFlg_Blocking) != 0) || (slb->kind == SlbT_LAVA) || (slb->kind == SlbT_WATER);
}

/** Digs a corridor (path, x first, then y) from one heart to the other, and sets each keeper up. */
static TbBool set_up_arena(void)
{
    PlayerNumber keepers[2];
    int64_t n = 0;
    for (PlayerNumber i = 0; (i < PLAYERS_COUNT) && (n < 2); i++)
    {
        const struct PlayerInfo *player = get_player(i);
        if (player_exists(player) && is_active_keeper(player) && !thing_is_invalid(get_player_soul_container(i)))
            keepers[n++] = i;
    }
    if (n < 2)
    {
        FTEST_FAIL_TEST("the arena needs two keepers with hearts, the level has %" PRId64, n);
        return false;
    }
    const struct Thing *heart0 = get_player_soul_container(keepers[0]);
    const struct Thing *heart1 = get_player_soul_container(keepers[1]);
    const MapSlabCoord x0 = subtile_slab(heart0->mappos.x.stl.num), y0 = subtile_slab(heart0->mappos.y.stl.num);
    const MapSlabCoord x1 = subtile_slab(heart1->mappos.x.stl.num), y1 = subtile_slab(heart1->mappos.y.stl.num);
    TbBool ok = true;
    for (int64_t k = 0; ok && (k < 2); k++)
    {
        const MapSlabCoord hx = (k == 0) ? x0 : x1, hy = (k == 0) ? y0 : y1;
        const int dir = (((k == 0) ? y1 : y0) > hy) ? 1 : -1;
        char setup[4096];
        snprintf(setup, sizeof(setup), "local P, HX, HY, DIR = 'PLAYER%d', %ld, %ld, %d\n%s", (int)keepers[k], (long)hx,
            (long)hy, dir, arena_dungeon_lua);
        ok = run_lua(setup);
    }
    char line[96];
    char *chunk = (char *)calloc(1, 64 * 1024);
    size_t len = 0;
    int64_t dug = 0;
    const MapSlabCoord dx = (x1 > x0) ? 1 : -1, dy = (y1 > y0) ? 1 : -1;
    for (MapSlabCoord x = x0, y = y0; ; )
    {
        if (slab_blocks_corridor(x, y))
        {
            snprintf(line, sizeof(line), "ChangeSlabType(%ld, %ld, 'PATH')\n", (long)x, (long)y);
            if (len + strlen(line) + 1 < 64 * 1024)
            {
                strcpy(chunk + len, line);
                len += strlen(line);
                dug++;
            }
        }
        if (x != x1)
            x += dx;
        else if (y != y1)
            y += dy;
        else
            break;
    }
    ok = ok && run_lua(chunk);
    free(chunk);
    for (int64_t k = 0; ok && (k < 2); k++)
    {
        char setup[2048];
        snprintf(setup, sizeof(setup), "local P = 'PLAYER%d'\n%s", (int)keepers[k], arena_keeper_lua);
        ok = run_lua(setup);
    }
    FTESTLOG("arena: players %d and %d, hearts at slabs (%ld,%ld) and (%ld,%ld), %" PRId64 " slabs dug between them",
        (int)keepers[0], (int)keepers[1], (long)x0, (long)y0, (long)x1, (long)y1, dug);
    return ok;
}

static void read_checkpoints(void)
{
    static const int64_t defaults[] = { 250, 500, 1000, 2000, 3000, 4000, 6000, 8000 };
    s_checkpoint_count = 0;
    const char *env = getenv("KFX_COMP_TURNS");
    if ((env != NULL) && (*env != '\0'))
    {
        const char *p = env;
        while ((*p != '\0') && (s_checkpoint_count < MAX_CHECKPOINTS))
        {
            char *end;
            const int64_t t = strtoll(p, &end, 10);
            if (end == p)
                break;
            if ((t > 0) && ((s_checkpoint_count == 0) || (t > s_checkpoints[s_checkpoint_count - 1])))
                s_checkpoints[s_checkpoint_count++] = t;
            p = (*end == ',') ? end + 1 : end;
        }
        if (s_checkpoint_count > 0)
            return;
    }
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
        s_checkpoints[s_checkpoint_count++] = defaults[i];
}

static int64_t computer_keepers(void)
{
    int64_t n = 0;
    for (PlayerNumber i = 0; i < PLAYERS_COUNT; i++)
    {
        const struct PlayerInfo *player = get_player(i);
        if (player_exists(player) && is_active_keeper(player) && flag_is_set(player->allocflags, PlaF_CompCtrl))
            n++;
    }
    return n;
}

void ftest_computer_play_golden_pre_start()
{
    fe_computer_players = 1;
    const char *level = getenv("KFX_COMP_LEVEL");
    if ((level != NULL) && (*level != '\0'))
        set_selected_level_number(strtol(level, NULL, 10));
}

static void note_task_kinds(void)
{
    for (int64_t i = 1; i < COMPUTER_TASKS_COUNT; i++)
    {
        const struct ComputerTask *ctask = &kfx_sim_state.computer_task[i];
        if (((ctask->flags & ComTsk_Unkn0001) != 0) && (ctask->ttype < 64))
            s_task_kinds |= (1ULL << ctask->ttype);
    }
}

static void log_task_kinds(void)
{
    char text[1200];
    size_t n = 0;
    text[0] = '\0';
    for (int64_t k = 1; k < 64; k++)
        if ((s_task_kinds & (1ULL << k)) != 0)
            n += (size_t)snprintf(text + n, (n < sizeof(text)) ? sizeof(text) - n : 0, " %s", computer_task_code_name(k));
    FTESTLOG("%s: computer task kinds started:%s", s_case, text);
}

static void restore_local_seat(void)
{
    // consecutive ftests share the process: give the seat back (as spectator_handoff does)
    clear_flag(get_player(my_player_number)->allocflags, PlaF_CompCtrl);
}

FTestActionResult ftest_computer_play_golden_action001(struct FTestActionArgs* const args)
{
    const struct FTestConfig *conf = ftest_get_current_test_config();
    snprintf(s_case, sizeof(s_case), "%s%s %ld", s_arena ? "arena " : "", (conf != NULL) ? conf->level_file : "?",
        (long)get_loaded_level_number());
    if (!ftest_golden_selected(s_case))
    {
        FTESTLOG("%s: not in this run (KFX_FTEST_GOLDEN_ONLY / _SHARD)", s_case);
        return FTRs_Go_To_Next_Action;
    }
    if (!player_enter_spectator_mode(my_player_number))
    {
        FTEST_FAIL_TEST("could not hand the local seat to the built-in AI");
        return FTRs_Go_To_Next_Action;
    }
    read_checkpoints();
    if (s_arena && !set_up_arena())
        return FTRs_Go_To_Next_Action;
    if (!ftest_golden_begin(&s_golden, SAVE_SLOT, expected))
        return FTRs_Go_To_Next_Action;
    s_saved_at = get_gameturn();
    s_next = 0;
    s_task_kinds = 0;
    FTESTLOG("%s: %" PRId64 " keepers, all computer-controlled; saved at turn %" PRId64 ", %" PRId64 " checkpoints up to %" PRId64 " turns after",
        s_case, computer_keepers(), (int64_t)s_saved_at, s_checkpoint_count, s_checkpoints[s_checkpoint_count - 1]);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_computer_play_golden_action002(struct FTestActionArgs* const args)
{
    if (s_golden.expected == NULL) // not selected, or the save failed
    {
        restore_local_seat();
        return FTRs_Go_To_Next_Action;
    }
    const int64_t played = (int64_t)get_gameturn() - (int64_t)s_saved_at;
    note_task_kinds();
    if (played < s_checkpoints[s_next])
        return FTRs_Repeat_Current_Action;
    char name[96];
    snprintf(name, sizeof(name), "%s turn %" PRId64, s_case, s_checkpoints[s_next]);
    if (played != s_checkpoints[s_next])
        FTESTLOG("%s: checked %" PRId64 " turns late", name, played - s_checkpoints[s_next]);
    ftest_golden_check(&s_golden, name);
    s_next++;
    if (s_next < s_checkpoint_count)
        return FTRs_Repeat_Current_Action;
    log_task_kinds();
    ftest_golden_finish(&s_golden, s_case);
    restore_local_seat();
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_computer_play_golden_init()
{
    s_arena = false;
    memset(&s_golden, 0, sizeof(s_golden));
    ftest_append_action(ftest_computer_play_golden_action001, 20, NULL);
    ftest_append_action(ftest_computer_play_golden_action002, 0, NULL);
    return true;
}

TbBool ftest_computer_arena_golden_init()
{
    ftest_computer_play_golden_init();
    s_arena = true;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
