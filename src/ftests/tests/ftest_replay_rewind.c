// Replay rewinding (upstream #5450/#5470, re-implemented over the state blobs: kfx_game's replay_rewind.c).
//
// replay_rewind: the computer keepers of original level 60 play while the game is recorded as a replay (the local
// keeper idles: its packets are empty, so the replay is the computer keepers' game). The replay is then played back
// in the same process. On the way the state is hashed at a few turns (each part of what a resync carries, the Lua
// state and the replay integrity); at the last of them the replay is rewound to each earlier one in turn -- two
// single steps back, then further -- and the hashes must match. Played forward again from the earliest, the hashes
// must match at every check turn once more. Then the Undo Pickup key is held, as a player rewinds (the session loop
// steps a turn back at a time), for 40 turns, let go, and the replay's own checksum verification (a turn whose state
// differs from the recording's is "Out of sync") must pass to near the end of the replay.
//
// replay_rewind_resync: the same, with a resync in the recording (made where a network host resyncs, inside
// process_packets(), and recorded as a host's is: upstream #5376). The earliest check turn is before it, so the replay
// played forward after the rewind applies the recorded resync again; another is just after it, rewound to from a
// keyframe at or before the resync turn.
//
// What the hashes leave out: this machine's pacing (kfx_net_state's turn clock and the GUI flags, kept across a
// restore as across a resync) and frame rate (kfx_frontend_state.time_delta, kept too), and the things' sound emitters
// (this machine's, dropped by a restore). Lua's Game table is hashed with its keys sorted: the binser bytes follow the
// table's iteration order, which a deserialised table doesn't share with the one the script built.
#include "ftest_replay_rewind.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ftest.h"
#include "../ftest_net_fake.h"

#include "bflib_datetm.h"
#include "bflib_fileio.h"
#include "bflib_keybrd.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "frontend.h"
#include "game_commands.h"
#include "game_replay.h"
#include "game_session_loop.h"
#include "kfx_frontend_state.h"
#include "kfx_net_state.h"
#include "lua_base.h"
#include <lauxlib.h>
#include "kfx_sim_state.h"
#include "local_state.h"
#include "main_game.h"
#include "net_main.h"
#include "net_resync.h"
#include "packet_data.h"
#include "player_data.h"
#include "replay_rewind.h"
#include "thing_data.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RR_FILE "ftest_replay_rewind.fxpkt"
#define RR_MAX_CHECKS 8

enum { RR_PART_LUA = RSPart_Count, RR_PART_INTEGRITY, RR_PARTS };
static const char *rr_part_name[RR_PARTS] = { "game", "kfx_sim_state", "kfx_net_state", "ariadne", "config", "frontend", "lua", "integrity" };

enum RewindPhase { RPh_Start, RPh_Record, RPh_Play, RPh_PlayAgain, RPh_HoldKey, RPh_PlayOn, RPh_Done };

/* the key held for the last phase: the Undo Pickup key, as a player rewinds */
#define RR_HOLD_TURNS 40

struct RewindRun
{
    TbBool with_resync;
    enum RewindPhase phase;
    GameTurn resync_turn;     /**< the turn the recording resyncs at (with_resync) */
    GameTurn record_turns;    /**< the recording stops at this turn */
    GameTurn hold_turn;       /**< the Undo Pickup key is held from here, for RR_HOLD_TURNS turns back */
    GameTurn final_turn;      /**< the replay is checked to here */
    TbBool seen_rewinding;    /**< local_state.rewinding was on while the key was held */
    GameTurn lowest_held;
    int64_t check_count;      /**< check turns, ascending; the last is where the rewinds start */
    GameTurn check[RR_MAX_CHECKS];
    uint64_t hash[RR_MAX_CHECKS][RR_PARTS];
    TbBool resynced;
    int64_t failures;
};

static struct RewindRun s_rr;

/* the frontend state at each check turn as first played: where it differs is logged */
static unsigned char *s_fe_copy[RR_MAX_CHECKS];

static void frontend_piece(void *ctx, int part, void *data, size_t size)
{
    if (part == RSPart_Frontend)
    {
        void **out = (void **)ctx;
        out[0] = data;
        out[1] = (void *)size;
    }
}

static void keep_frontend(int64_t i)
{
    void *fe[2] = {NULL, NULL};
    resync_visit_state(frontend_piece, fe);
    free(s_fe_copy[i]);
    s_fe_copy[i] = (unsigned char *)malloc((size_t)fe[1]);
    if (s_fe_copy[i] != NULL)
        memcpy(s_fe_copy[i], fe[0], (size_t)fe[1]);
}

static void log_frontend_diff(int64_t i)
{
    void *fe[2] = {NULL, NULL};
    resync_visit_state(frontend_piece, fe);
    const unsigned char *a = s_fe_copy[i];
    const unsigned char *b = (const unsigned char *)fe[0];
    for (size_t k = 0; (a != NULL) && (k < (size_t)fe[1]); k++)
    {
        if (a[k] == b[k])
            continue;
        size_t j = k;
        while ((j < (size_t)fe[1]) && (a[j] != b[j]))
            j++;
        FTESTLOG("  kfx_frontend_state differs at offset %" PRIu64 ", %" PRIu64 " bytes (first played %02x, now %02x)",
            (uint64_t)k, (uint64_t)(j - k), a[k], b[k]);
        k = j;
    }
}

/* Lua's Game table written out with its keys sorted: binser writes a table in its iteration order, which a
 * deserialised table (a resync's, a load's, a rewind's) doesn't share with the one the script built. */
static const char rr_lua_canon[] =
    "local function key_less(a, b)\n"
    "    local ta, tb = type(a), type(b)\n"
    "    if ta ~= tb then return ta < tb end\n"
    "    if ta == 'number' or ta == 'string' then return a < b end\n"
    "    return tostring(a) < tostring(b)\n"
    "end\n"
    "local function canon(v, seen)\n"
    "    local t = type(v)\n"
    "    if t == 'table' then\n"
    "        if seen[v] then return '<cycle>' end\n"
    "        seen[v] = true\n"
    "        local keys = {}\n"
    "        for k in pairs(v) do keys[#keys + 1] = k end\n"
    "        table.sort(keys, key_less)\n"
    "        local parts = {}\n"
    "        for _, k in ipairs(keys) do parts[#parts + 1] = canon(k, seen) .. '=' .. canon(v[k], seen) end\n"
    "        seen[v] = nil\n"
    "        return '{' .. table.concat(parts, ',') .. '}'\n"
    "    elseif t == 'string' then return string.format('%q', v)\n"
    "    elseif t == 'function' then return '<function>'\n"
    "    end\n"
    "    return t .. ':' .. tostring(v)\n"
    "end\n"
    "return canon(Game, {})\n";

static uint64_t block_hash(const void *p, size_t n, uint64_t h);

static uint64_t lua_game_hash(uint64_t h)
{
    lua_State *L = Lvl_script;
    if (L == NULL)
        return h;
    const int top = lua_gettop(L);
    if (luaL_dostring(L, rr_lua_canon) == 0)
    {
        size_t len = 0;
        const char *text = lua_tolstring(L, -1, &len);
        if (text != NULL)
            h = block_hash(text, len, h);
    } else
    {
        FTESTLOG("Lua's Game table could not be written out: %s", lua_tostring(L, -1));
        h = 0;
    }
    lua_settop(L, top);
    return h;
}

/** A fast hash of a block: four independent 8-byte lanes (the state is ~165 MB). */
static uint64_t block_hash(const void *p, size_t n, uint64_t h)
{
    const unsigned char *b = (const unsigned char *)p;
    uint64_t l0 = h, l1 = h ^ 0x9e3779b97f4a7c15ULL, l2 = h ^ 0xc2b2ae3d27d4eb4fULL, l3 = h ^ 0x165667b19e3779f9ULL;
    size_t i = 0;
    for (; i + 32 <= n; i += 32)
    {
        uint64_t w[4];
        memcpy(w, b + i, 32);
        l0 = (l0 ^ w[0]) * 0x100000001b3ULL;
        l1 = (l1 ^ w[1]) * 0x100000001b3ULL;
        l2 = (l2 ^ w[2]) * 0x100000001b3ULL;
        l3 = (l3 ^ w[3]) * 0x100000001b3ULL;
    }
    h = l0 ^ (l1 * 31) ^ (l2 * 961) ^ (l3 * 29791);
    for (; i < n; i++)
        h = (h ^ b[i]) * 0x100000001b3ULL;
    return h ^ (h >> 29);
}

static void hash_piece(void *ctx, int part, void *data, size_t size)
{
    uint64_t *h = (uint64_t *)ctx;
    h[part] = block_hash(data, size, h[part]);
}

/** The hashes of what a resync carries (resync_visit_state()), the Lua state and the replay integrity. */
static void hash_state(uint64_t h[RR_PARTS])
{
    static unsigned char emitters[THINGS_COUNT];
    for (int64_t i = 0; i < RR_PARTS; i++)
        h[i] = 1469598103934665603ULL + (uint64_t)i;
    // this machine's, not the game's: left out
    const struct LocalPacingState pacing = local_pacing_keep();
    const struct LocalPacingState neutral = { 0.0, 0, 0 };
    local_pacing_restore(&neutral);
    const uint64_t time_delta = kfx_frontend_state.time_delta;
    kfx_frontend_state.time_delta = 0;
    for (ThingIndex i = 0; i < THINGS_COUNT; i++)
    {
        emitters[i] = thing_get(i)->snd_emitter_id;
        thing_get(i)->snd_emitter_id = 0;
    }
    resync_visit_state(hash_piece, h);
    for (ThingIndex i = 0; i < THINGS_COUNT; i++)
        thing_get(i)->snd_emitter_id = emitters[i];
    local_pacing_restore(&pacing);
    kfx_frontend_state.time_delta = time_delta;
    h[RR_PART_LUA] = lua_game_hash(h[RR_PART_LUA]);
    h[RR_PART_INTEGRITY] = compute_replay_integrity();
}

/** Compares the state with check i's hashes; logs and counts each part that differs. */
static void compare_state(int64_t i, const char *when)
{
    uint64_t h[RR_PARTS];
    hash_state(h);
    int64_t bad = 0;
    for (int64_t p = 0; p < RR_PARTS; p++)
    {
        if (h[p] == s_rr.hash[i][p])
            continue;
        FTESTLOG("%s, turn %" PRIu64 ": %s differs (%016" PRIx64 ", first played %016" PRIx64 ")", when,
            (uint64_t)s_rr.check[i], rr_part_name[p], h[p], s_rr.hash[i][p]);
        bad++;
    }
    if (bad > 0)
    {
        s_rr.failures++;
        if (h[RSPart_Frontend] != s_rr.hash[i][RSPart_Frontend])
            log_frontend_diff(i);
    }
    else
        FTESTLOG("%s, turn %" PRIu64 ": every part as first played", when, (uint64_t)s_rr.check[i]);
}

static int64_t check_index(GameTurn t)
{
    for (int64_t i = 0; i < s_rr.check_count; i++)
        if (s_rr.check[i] == t)
            return i;
    return -1;
}

/** The recording's resync, where a network host resyncs (process_packets()): the host's half sends it (and records
 *  it into the replay, as a host's resync is), then this machine applies it as a client would, as the replay's
 *  playback will (apply_recorded_resync()). */
static void rr_resync_hook(void)
{
    ftest_process_packets_resync_hook = NULL;
    const struct NetSP *real_sp = netstate.sp;
    const NetUserId real_id = netstate.my_id;
    static struct NetUser real_users[MAX_NET_USERS];
    memcpy(real_users, netstate.users, sizeof(real_users));
    ftest_net_fake_reset();
    netstate.sp = ftest_net_fake_sp();
    memset(netstate.users, 0, sizeof(netstate.users));
    netstate.users[1].id = 1;
    netstate.users[1].progress = USER_LOGGEDIN;
    ftest_net_fake_set_role_host();
    const TbBool sent = send_resync_game();
    // the client's half: what the replay's playback does with the recorded resync. Not recorded again.
    const unsigned char save_enable = replay.save_enable;
    replay.save_enable = false;
    netstate.my_id = 1;
    ftest_net_fake_set_role_client(1);
    if (sent)
        resync_game();
    replay.save_enable = save_enable;
    netstate.my_id = real_id;
    netstate.sp = real_sp;
    memcpy(netstate.users, real_users, sizeof(real_users));
    ftest_net_fake_reset();
    s_rr.resynced = sent;
    FTESTLOG("the recording resynced at turn %" PRIu64 ": %s", (uint64_t)get_gameturn(), sent ? "sent and applied" : "FAILED");
}

void ftest_replay_rewind_pre_start()
{
    // the level's computer keepers play (as Skirmish's Play sets up)
    fe_computer_players = 1;
    // recorded as -packetsave records, with the integrity checksum every turn
    start_params.packet_save_enable = true;
    snprintf(start_params.packet_fname, sizeof(start_params.packet_fname), "%s", RR_FILE);
    replay.save_enable = true;
    replay.load_enable = false;
    replay.checksum_verify = true;
    snprintf(replay.fname, sizeof(replay.fname), "%s", RR_FILE);
}

static TbBool start_playback(void)
{
    close_packet_file();
    replay.save_enable = false;
    start_params.packet_save_enable = false;
    replay.load_enable = true;
    replay.checksum_verify = true;
    snprintf(replay.fname, sizeof(replay.fname), "%s", RR_FILE);
    if (!startup_saved_packet_game())
    {
        FTEST_FAIL_TEST("the recorded replay doesn't start");
        return false;
    }
    get_my_player()->display_flags &= ~PlaF6_PlyrHasQuit;
    FTESTLOG("playing the replay back: %" PRIu64 " turns recorded, checksum verification %s", (uint64_t)replay.turns_stored,
        replay.checksum_verify ? "on" : "OFF");
    if (replay.turns_stored + 1 < s_rr.record_turns)
    {
        FTEST_FAIL_TEST("the replay has %" PRIu64 " turns, not %" PRIu64, (uint64_t)replay.turns_stored, (uint64_t)s_rr.record_turns);
        return false;
    }
    if (!replay.checksum_verify)
    {
        FTEST_FAIL_TEST("the replay has no checksums to verify");
        return false;
    }
    return true;
}

static void log_rewind_buffer(void)
{
    GameTurn oldest = 0;
    const size_t keyframes = replay_rewind_keyframe_count(&oldest);
    struct ReplayRewindStats st;
    replay_rewind_get_stats(&st);
    FTESTLOG("rewind buffer at turn %" PRIu64 ": %" PRIu64 " keyframes back to turn %" PRIu64 ", %.1f kB of %.0f MB used "
        "(largest keyframe %.1f kB), snapshot %.1f MB; a keyframe's diff took %.2f ms on average, %.2f ms at most",
        (uint64_t)get_gameturn(), (uint64_t)keyframes, (uint64_t)oldest, st.ring_used / 1024.0,
        st.ring_bytes / (1024.0 * 1024.0), st.largest_record / 1024.0, st.snapshot_bytes / (1024.0 * 1024.0),
        (st.captures > 1) ? st.diff_ns_total / 1e6 / (double)(st.captures - 1) : 0.0, st.diff_ns_max / 1e6);
}

/** At the last check turn: back to each earlier one, newest first, comparing each. */
static TbBool rewind_through_checks(void)
{
    log_rewind_buffer();
    for (int64_t i = s_rr.check_count - 2; i >= 0; i--)
    {
        const GameTurn from = get_gameturn();
        const int64_t start = get_time_tick_ns();
        const TbBool ok = replay_rewind_to_turn(s_rr.check[i]);
        const double ms = (get_time_tick_ns() - start) / 1e6;
        if (!ok || (get_gameturn() != s_rr.check[i]))
        {
            FTEST_FAIL_TEST("rewinding from turn %" PRIu64 " to %" PRIu64 " ended at turn %" PRIu64, (uint64_t)from,
                (uint64_t)s_rr.check[i], (uint64_t)get_gameturn());
            return false;
        }
        // what the functional test framework sets at the start of every turn
        ftest_srand();
        FTESTLOG("rewound from turn %" PRIu64 " to %" PRIu64 " in %.1f ms", (uint64_t)from, (uint64_t)s_rr.check[i], ms);
        compare_state(i, "after rewinding");
    }
    return true;
}

static void finish(void)
{
    lbKeyOn[settings.kbkeys[Gkey_DumpToOldPos].code] = 0;
    if (replay.load_enable)
        disable_packet_mode();
    ftest_process_packets_resync_hook = NULL;
    start_params.packet_save_enable = false;
    LbFileDelete(RR_FILE);
}

FTestActionResult ftest_replay_rewind_action001(struct FTestActionArgs* const args)
{
    const GameTurn t = get_gameturn();
    switch (s_rr.phase)
    {
    case RPh_Start:
        if (!replay.save_enable || !replay.fopened)
        {
            FTEST_FAIL_TEST("the level isn't being recorded");
            break;
        }
        FTESTLOG("recording %" PRIu64 " turns%s", (uint64_t)s_rr.record_turns, s_rr.with_resync ? ", with a resync" : "");
        s_rr.phase = RPh_Record;
        return FTRs_Repeat_Current_Action;
    case RPh_Record:
        if (s_rr.with_resync && (t == s_rr.resync_turn))
            ftest_process_packets_resync_hook = rr_resync_hook;
        if (t < s_rr.record_turns)
            return FTRs_Repeat_Current_Action;
        if (s_rr.with_resync && !s_rr.resynced)
        {
            FTEST_FAIL_TEST("the recording didn't resync");
            break;
        }
        if (!start_playback())
            break;
        s_rr.phase = RPh_Play;
        return FTRs_Repeat_Current_Action;
    case RPh_Play:
    {
        const int64_t i = check_index(t);
        if (i < 0)
            return FTRs_Repeat_Current_Action;
        hash_state(s_rr.hash[i]);
        keep_frontend(i);
        FTESTLOG("turn %" PRIu64 " first played: kfx_sim_state %016" PRIx64 ", integrity %016" PRIx64, (uint64_t)t,
            s_rr.hash[i][RSPart_Sim], s_rr.hash[i][RR_PART_INTEGRITY]);
        if (i < s_rr.check_count - 1)
            return FTRs_Repeat_Current_Action;
        if (replay_checksum_mismatch_count() != 0)
        {
            FTEST_FAIL_TEST("the replay went out of sync before any rewind (%" PRIu64 " turns)", replay_checksum_mismatch_count());
            break;
        }
        if (!rewind_through_checks())
            break;
        s_rr.phase = RPh_PlayAgain;
        return FTRs_Repeat_Current_Action;
    }
    case RPh_PlayAgain:
    {
        const int64_t i = check_index(t);
        if (i > 0)
            compare_state(i, "played again after the rewind");
        if (t < s_rr.hold_turn)
            return FTRs_Repeat_Current_Action;
        // the way a player rewinds: the Undo Pickup key held (gameplay_loop_logic(), a turn back per step)
        lbKeyOn[settings.kbkeys[Gkey_DumpToOldPos].code] = 1;
        s_rr.lowest_held = t;
        s_rr.phase = RPh_HoldKey;
        return FTRs_Repeat_Current_Action;
    }
    case RPh_HoldKey:
        if (local_state.rewinding)
            s_rr.seen_rewinding = true;
        if (t < s_rr.lowest_held)
        {
            if (t != s_rr.lowest_held - 1)
                FTESTLOG("holding the key went from turn %" PRIu64 " to %" PRIu64, (uint64_t)s_rr.lowest_held, (uint64_t)t);
            s_rr.lowest_held = t;
        }
        if ((t > s_rr.hold_turn - RR_HOLD_TURNS) && (t <= s_rr.hold_turn))
            return FTRs_Repeat_Current_Action;
        lbKeyOn[settings.kbkeys[Gkey_DumpToOldPos].code] = 0;
        FTESTLOG("held the Undo Pickup key from turn %" PRIu64 " back to %" PRIu64 " (rewinding seen: %d)",
            (uint64_t)s_rr.hold_turn, (uint64_t)t, (int)s_rr.seen_rewinding);
        if (!s_rr.seen_rewinding || (t != s_rr.hold_turn - RR_HOLD_TURNS))
        {
            FTEST_FAIL_TEST("holding the Undo Pickup key didn't rewind the replay turn by turn (at turn %" PRIu64 ")", (uint64_t)t);
            break;
        }
        s_rr.phase = RPh_PlayOn;
        return FTRs_Repeat_Current_Action;
    case RPh_PlayOn:
        if (local_state.rewinding)
        {
            FTEST_FAIL_TEST("still rewinding after the key was let go");
            break;
        }
        if (t < s_rr.final_turn)
            return FTRs_Repeat_Current_Action;
        log_rewind_buffer();
        if (!replay.load_enable) {
            FTEST_FAIL_TEST("the replay stopped before turn %" PRIu64, (uint64_t)s_rr.final_turn);
        } else if (replay_checksum_mismatch_count() != 0) {
            FTEST_FAIL_TEST("the replay went out of sync after the rewind (%" PRIu64 " turns)", replay_checksum_mismatch_count());
        } else {
            FTESTLOG("the replay's checksums verified to turn %" PRIu64, (uint64_t)t);
        }
        s_rr.phase = RPh_Done;
        break;
    default:
        break;
    }
    if (s_rr.failures > 0)
        FTEST_FAIL_TEST("%" PRId64 " state comparison(s) failed", s_rr.failures);
    finish();
    return FTRs_Go_To_Next_Action;
}

static void run_reset(TbBool with_resync)
{
    memset(&s_rr, 0, sizeof(s_rr));
    s_rr.with_resync = with_resync;
    s_rr.phase = RPh_Start;
    s_rr.record_turns = 900;
    s_rr.hold_turn = 840;
    s_rr.final_turn = 880;
    if (with_resync)
    {
        // before the resync, just after it (from a keyframe at or before it), two single steps, and the start
        s_rr.resync_turn = 400;
        const GameTurn checks[] = { 333, 410, 698, 699, 700 };
        s_rr.check_count = sizeof(checks) / sizeof(checks[0]);
        memcpy(s_rr.check, checks, sizeof(checks));
    } else
    {
        // a keyframe's own turn, turns between keyframes, two single steps, and the start
        const GameTurn checks[] = { 160, 437, 698, 699, 700 };
        s_rr.check_count = sizeof(checks) / sizeof(checks[0]);
        memcpy(s_rr.check, checks, sizeof(checks));
    }
}

TbBool ftest_replay_rewind_init()
{
    run_reset(false);
    ftest_append_action(ftest_replay_rewind_action001, 0, NULL);
    return true;
}

TbBool ftest_replay_rewind_resync_init()
{
    run_reset(true);
    ftest_append_action(ftest_replay_rewind_action001, 0, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
