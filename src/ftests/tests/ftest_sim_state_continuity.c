// Refactor pass 4, S07 (sim state audit): does the simulation carry on the same after a save and load, after a
// network resync, and after the level is started again in the same process? Each of those replaces the state
// structs wholesale (game_saves.c, net_resync.cpp, main_game.c's memset at level start); state the simulation
// keeps anywhere else (a global, a file-scope static, a function-scope static) is not carried, so where such
// state matters the game afterwards differs from the game that was never interrupted.
//
// sim_state_continuity: the computer keepers of original level 50 play by themselves (the local seat is handed to
// the built-in AI as in spectator mode). Phase A plays turns 1..T+N, saving the game and sending a resync blob
// (to the fake network, as the host) at turn T, and records a hash of the state at every turn after T. Phase B
// loads the save, plays T..T+N again and compares; phase C receives the resync blob (as a client), plays T..T+N
// again and compares. sim_state_continuity_restart runs the level twice in one process (the second run starts
// after the level was played and left) and compares the second run with the first from turn 1.
//
// Each turn hashes compute_replay_integrity() (what replays check: things' positions, the slabs, the dig tasks),
// the whole of kfx_sim_state and the regions below, kfx_game_state, Ariadne's navigation state, and the saved part of
// kfx_config_state. The test fails when the replay integrity or kfx_config_state's saved part differs (P4-F16);
// the other hashes say where a difference starts. Refactor pass 5, S04: with the drawing fields out of the state, the
// regions marked strict must be the same too (a difference is state lost or not cleared between levels).
//
// Environment, for investigating: KFX_CONT_T, KFX_CONT_N (turns; defaults 2000 and 2000); KFX_CONT_SLOT (save slot, 7); KFX_CONT_DIFF_TURN=D
// keeps a copy of kfx_sim_state at turn D in phase A (or in the first run) and logs the byte ranges that differ
// at turn D in the later phases to sim_state_diff_<D>_<phase>.txt (map offsets to fields with
// `ptype /o struct KfxSimState` in gdb); with KFX_CONT_DIFF_CONFIG=1, of kfx_config_state's saved part instead;
// with KFX_CONT_DIFF_REGION=<name>, of that region of the list below (offsets from the region's start).
#include "ftest_sim_state_continuity.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_net_fake.h"

#include "ariadne_points.h"
#include "config_keeperfx.h"
#include "ariadne_tringls.h"
#include "frontend.h"
#include "game_replay.h"
#include "game_saves.h"
#include "ariadne_update.h"
#include "dungeon_data.h"
#include "kfx_config_state.h"
#include "kfx_game_state.h"
#include "main_game.h"
#include "kfx_pathfinding_state.h"
#include "kfx_sim_state.h"
#include "net_main.h"
#include "net_resync.h"
#include "player_data.h"
#include "player_utils.h"
#include "slab_data.h"
#include "map_data.h"
#include "packet_data.h"
#include "tasks_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_SLOT env_int("KFX_CONT_SLOT", 7)

static int64_t env_int(const char *name, int64_t def);

struct Region
{
    const char *name;
    const void *ptr;
    size_t size;
    TbBool strict; /**< a difference after a load or a resync fails the test */
    TbBool strict_restart; /**< a difference in the second run of the level fails the test */
};

/** A region of kfx_sim_state that has to be the same in every phase (refactor pass 5, S04: nothing the drawing
 *  writes is in them any more). */
#define SIM_REGION(field) { #field, &kfx_sim_state.field, sizeof(kfx_sim_state.field), true, true }

enum ContinuityPhase { Ph_Start, Ph_Record, Ph_Load, Ph_Resync, Ph_Restart, Ph_Done };

static const char *phase_name[] = { "start", "uninterrupted", "after save/load", "after resync", "second run of the level", "done" };

#define MAX_REGIONS 24

struct ContinuityRun
{
    int64_t t_save, n_turns, diff_turn;
    enum ContinuityPhase phase;
    int64_t region_count;
    // per turn after t_save (index turn - t_save - 1): the integrity, then the region hashes
    uint64_t *integrity;
    uint64_t *hashes;
    int64_t first_diff[MAX_REGIONS + 1];   // per region (and [0]: integrity) in the phase being compared
    void *diff_snapshot;
    const void *diff_base;  /**< what KFX_CONT_DIFF_TURN diffs: kfx_sim_state, kfx_config_state's saved part or a region */
    size_t diff_size;
    const char *diff_name;  /**< the region KFX_CONT_DIFF_REGION named, or NULL */
    int64_t failures;
};

static struct ContinuityRun s_run;

/** A fast hash of a block: four independent 8-byte lanes (the state is ~140 MB, hashed every turn). */
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

static int64_t regions(struct Region *r)
{
    int64_t n = 0;
    // The whole of it after a load or a resync (refactor pass 5, S11); not in a second run of the level, which starts
    // at another wall-clock time (timerstarttime, the level statistics' start times)
    r[n++] = (struct Region){ "kfx_sim_state", &kfx_sim_state, sizeof(kfx_sim_state), true, false };
    r[n++] = (struct Region)SIM_REGION(things_data);
    r[n++] = (struct Region)SIM_REGION(cctrl_data);
    r[n++] = (struct Region)SIM_REGION(map);
    r[n++] = (struct Region)SIM_REGION(columns_data);
    r[n++] = (struct Region)SIM_REGION(slabmap);
    r[n++] = (struct Region)SIM_REGION(slabset);
    r[n++] = (struct Region)SIM_REGION(rooms);
    // the level statistics' start times are the wall clock's: a second run of the level starts at another time
    r[n++] = (struct Region){ "dungeon", &kfx_sim_state.dungeon, sizeof(kfx_sim_state.dungeon), true, false };
    r[n++] = (struct Region)SIM_REGION(players);
    r[n++] = (struct Region)SIM_REGION(user_states);
    r[n++] = (struct Region)SIM_REGION(computer);
    r[n++] = (struct Region)SIM_REGION(computer_task);
    r[n++] = (struct Region)SIM_REGION(event);
    r[n++] = (struct Region)SIM_REGION(battles);
    r[n++] = (struct Region)SIM_REGION(light_registry); // only the simulation writes it (refactor pass 5, S11)
    r[n++] = (struct Region){ "kfx_game_state", &kfx_game_state, sizeof(kfx_game_state), true, true };
    r[n++] = (struct Region){ "kfx_pathfinding_state", &kfx_pathfinding_state, sizeof(kfx_pathfinding_state), true, true };
    r[n++] = (struct Region){ "Triangles", Triangles, sizeof(struct Triangle) * (size_t)(count_Triangles > 0 ? count_Triangles : 0), true, true };
    r[n++] = (struct Region){ "ari_Points", ari_Points, sizeof(struct Point) * POINTS_COUNT, true, true };
    // P4-F16: the configuration as the level's script left it, the payday progress, the slabs' texture packs
    r[n++] = (struct Region){ "kfx_config_state saved", KFX_CONFIG_STATE_SAVED_PTR, KFX_CONFIG_STATE_SAVED_LEN, true, true };
    return n;
}

static int64_t env_int(const char *name, int64_t def)
{
    const char *v = getenv(name);
    return (v != NULL && *v != '\0') ? strtoll(v, NULL, 10) : def;
}

static void run_reset(int64_t t_save, int64_t n_turns)
{
    free(s_run.integrity);
    free(s_run.hashes);
    free(s_run.diff_snapshot);
    memset(&s_run, 0, sizeof(s_run));
    s_run.t_save = t_save;
    s_run.n_turns = n_turns;
    s_run.diff_turn = env_int("KFX_CONT_DIFF_TURN", -1);
    s_run.diff_base = env_int("KFX_CONT_DIFF_CONFIG", 0) ? KFX_CONFIG_STATE_SAVED_PTR : (const void *)&kfx_sim_state;
    s_run.diff_size = env_int("KFX_CONT_DIFF_CONFIG", 0) ? KFX_CONFIG_STATE_SAVED_LEN : sizeof(kfx_sim_state);
    struct Region r[MAX_REGIONS];
    s_run.region_count = regions(r);
    const char *diff_region = getenv("KFX_CONT_DIFF_REGION");
    for (int64_t i = 0; (diff_region != NULL) && (i < s_run.region_count); i++)
    {
        if (strcmp(r[i].name, diff_region) == 0)
        {
            s_run.diff_base = r[i].ptr;
            s_run.diff_size = r[i].size;
            s_run.diff_name = r[i].name;
        }
    }
    s_run.integrity = (uint64_t *)calloc((size_t)n_turns, sizeof(uint64_t));
    s_run.hashes = (uint64_t *)calloc((size_t)(n_turns * s_run.region_count), sizeof(uint64_t));
}

static void reset_first_diffs(void)
{
    for (int64_t i = 0; i <= MAX_REGIONS; i++)
        s_run.first_diff[i] = -1;
}

/** The byte ranges of kfx_sim_state that differ from the copy kept at the same turn, written to
 * sim_state_diff_<turn>_<phase>.txt (offset and length per line; name them with the struct's gdb layout). */
static void write_snapshot_diff(enum ContinuityPhase phase)
{
    const unsigned char *a = (const unsigned char *)s_run.diff_snapshot;
    const unsigned char *b = (const unsigned char *)s_run.diff_base;
    char fname[64];
    snprintf(fname, sizeof(fname), "sim_state_diff_%" PRId64 "_%d.txt", (int64_t)get_gameturn(), (int)phase);
    FILE *f = fopen(fname, "w");
    if (f == NULL)
        return;
    int64_t ranges = 0;
    size_t i = 0;
    while (i < s_run.diff_size)
    {
        if (a[i] == b[i]) { i++; continue; }
        size_t j = i;
        while ((j < s_run.diff_size) && (a[j] != b[j]))
            j++;
        fprintf(f, "%" PRIu64 " %" PRIu64, (uint64_t)i, (uint64_t)(j - i));
        // the bytes before and after, for short ranges (8-byte aligned around the range)
        const size_t w0 = i & ~(size_t)7, w1 = ((j + 7) & ~(size_t)7) < s_run.diff_size ? ((j + 7) & ~(size_t)7) : s_run.diff_size;
        if (w1 - w0 <= 16)
        {
            fprintf(f, " @%" PRIu64 " ", (uint64_t)w0);
            for (size_t k = w0; k < w1; k++) fprintf(f, "%02x", a[k]);
            fprintf(f, " ");
            for (size_t k = w0; k < w1; k++) fprintf(f, "%02x", b[k]);
        }
        fprintf(f, "\n");
        ranges++;
        i = j;
    }
    fclose(f);
    FTESTLOG("%" PRId64 " differing ranges in %s at turn %" PRId64 ", in %s", ranges, (s_run.diff_name != NULL) ? s_run.diff_name : env_int("KFX_CONT_DIFF_CONFIG", 0) ? "kfx_config_state's saved part" : "kfx_sim_state", (int64_t)get_gameturn(), fname);
}

/** Records (phase A, first run) or compares (later phases) the state at the start of this turn. */
static void record_or_compare(enum ContinuityPhase phase, int64_t first_turn)
{
    const int64_t t = (int64_t)get_gameturn();
    const int64_t k = t - first_turn;
    if ((k < 0) || (k >= s_run.n_turns))
        return;
    struct Region r[MAX_REGIONS];
    regions(r);
    const uint64_t integrity = compute_replay_integrity();
    uint64_t *row = &s_run.hashes[k * s_run.region_count];
    if (phase == Ph_Record)
    {
        s_run.integrity[k] = integrity;
        for (int64_t i = 0; i < s_run.region_count; i++)
            row[i] = block_hash(r[i].ptr, r[i].size, 1469598103934665603ULL);
        if (t == s_run.diff_turn)
        {
            s_run.diff_snapshot = malloc(s_run.diff_size);
            if (s_run.diff_snapshot != NULL)
                memcpy(s_run.diff_snapshot, s_run.diff_base, s_run.diff_size);
        }
        return;
    }
    if ((s_run.first_diff[0] < 0) && (integrity != s_run.integrity[k]))
        s_run.first_diff[0] = t;
    for (int64_t i = 0; i < s_run.region_count; i++)
    {
        if ((s_run.first_diff[i + 1] < 0) && (block_hash(r[i].ptr, r[i].size, 1469598103934665603ULL) != row[i]))
            s_run.first_diff[i + 1] = t;
    }
    if ((t == s_run.diff_turn) && (s_run.diff_snapshot != NULL))
        write_snapshot_diff(phase);
}

/** Logs where the phase being compared first differed; a different replay integrity fails the test. */
static void report(enum ContinuityPhase phase, int64_t first_turn)
{
    struct Region r[MAX_REGIONS];
    regions(r);
    if (s_run.first_diff[0] < 0)
        FTESTLOG("%s: the replay integrity matches the uninterrupted game at all %" PRId64 " turns from turn %" PRId64,
            phase_name[phase], s_run.n_turns, first_turn);
    else
    {
        FTESTLOG("%s: the replay integrity first differs at turn %" PRId64 " (%" PRId64 " turns after turn %" PRId64 ")",
            phase_name[phase], s_run.first_diff[0], s_run.first_diff[0] - first_turn, first_turn);
        s_run.failures++;
    }
    for (int64_t i = 0; i < s_run.region_count; i++)
    {
        if (s_run.first_diff[i + 1] < 0)
            FTESTLOG("%s:   %-22s same", phase_name[phase], r[i].name);
        else
        {
            FTESTLOG("%s:   %-22s first differs at turn %" PRId64, phase_name[phase], r[i].name, s_run.first_diff[i + 1]);
            if ((phase == Ph_Restart) ? r[i].strict_restart : r[i].strict)
            {
                FTESTLOG("%s: %s must not differ", phase_name[phase], r[i].name);
                s_run.failures++;
            }
        }
    }
}

/** The objects returned for an invalid index: nothing may write them (a write is a bug in the caller). */
static uint64_t sentinels_hash(void)
{
    uint64_t h = 1469598103934665603ULL;
    h = block_hash(&bad_map_block, sizeof(bad_map_block), h);
    h = block_hash(&bad_slabmap_block, sizeof(bad_slabmap_block), h);
    h = block_hash(&bad_dungeon, sizeof(bad_dungeon), h);
    h = block_hash(&bad_map_task, sizeof(bad_map_task), h);
    h = block_hash(&bad_player, sizeof(bad_player), h);
    h = block_hash(&bad_user_state, sizeof(bad_user_state), h);
    h = block_hash(&bad_packet, sizeof(bad_packet), h);
    h = block_hash(&bad_triangle, sizeof(bad_triangle), h);
    return h;
}

static uint64_t s_sentinels_at_start;

static void check_sentinels(void)
{
    if (sentinels_hash() != s_sentinels_at_start)
    {
        FTESTLOG("an invalid-index sentinel (bad_dungeon, bad_player, bad_map_block, ...) was written during the game");
        s_run.failures++;
    }
}

/** One hash of the whole recorded integrity sequence: equal in two processes when the game is deterministic. */
static void log_sequence_digest(void)
{
    FTESTLOG("uninterrupted game: integrity digest %016" PRIx64 ", kfx_sim_state digest %016" PRIx64 " over turns %" PRId64 "..%" PRId64,
        block_hash(s_run.integrity, (size_t)s_run.n_turns * sizeof(uint64_t), 1),
        s_run.n_turns > 0 ? s_run.hashes[(s_run.n_turns - 1) * s_run.region_count] : 0,
        s_run.t_save + 1, s_run.t_save + s_run.n_turns);
}

void ftest_sim_state_continuity_pre_start()
{
    fe_computer_players = 1;
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

static void hand_local_seat_to_ai(void)
{
    if (!player_enter_spectator_mode(my_player_number))
        FTEST_FAIL_TEST("could not hand the local seat to the built-in AI");
}

static void restore_local_seat(void)
{
    // consecutive ftests share the process: give the seat back (as spectator_handoff does)
    clear_flag(get_player(my_player_number)->allocflags, PlaF_CompCtrl);
}

static TbBool save_and_send(void)
{
    fill_game_catalogue_slot(SAVE_SLOT, "sim_state_continuity");
    set_flag(kfx_sim_state.operation_flags, GOF_Paused); // games are saved paused (as the console `save` does)
    const TbBool saved = save_game(SAVE_SLOT);
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    if (!saved)
    {
        FTEST_FAIL_TEST("save_game failed");
        return false;
    }
    // the host's half of a resync, into the fake network's mailbox, where it waits for phase C
    const struct NetSP *real_sp = netstate.sp;
    ftest_net_fake_reset();
    netstate.sp = ftest_net_fake_sp();
    memset(netstate.users, 0, sizeof(netstate.users));
    netstate.users[1].id = 1;
    netstate.users[1].progress = USER_LOGGEDIN;
    ftest_net_fake_set_role_host();
    const TbBool sent = send_resync_game();
    netstate.sp = real_sp;
    memset(netstate.users, 0, sizeof(netstate.users));
    if (!sent)
    {
        FTEST_FAIL_TEST("send_resync_game failed");
        return false;
    }
    return true;
}

static TbBool load(void)
{
    if (!load_game(SAVE_SLOT))
    {
        FTEST_FAIL_TEST("load_game failed");
        return false;
    }
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    return true;
}

/* LensManager's C entry points (LensManager.h is C++). */
void* LensManager_GetInstance(void);
TbBool LensManager_IsReady(void* mgr);

static TbBool receive_resync(void)
{
    // the client's half: resync_game() as a client (netstate.my_id not the server's) reading the host's blob
    const struct NetSP *real_sp = netstate.sp;
    const NetUserId real_id = netstate.my_id;
    netstate.sp = ftest_net_fake_sp();
    netstate.my_id = 1;
    ftest_net_fake_set_role_client(1);
    const GameTurn before = get_gameturn();
    const TbBool lens_ready = LensManager_IsReady(LensManager_GetInstance());
    resync_game();
    // the eye lens system is this machine's: a resync must leave it set up (refactor pass 4, P4-F13)
    const TbBool lens_ready_after = LensManager_IsReady(LensManager_GetInstance());
    if (lens_ready && !lens_ready_after)
    {
        FTESTLOG("the resync left the eye lens system shut down");
        s_run.failures++;
    }
    FTESTLOG("eye lens system set up before the resync: %d, after: %d", (int)lens_ready, (int)lens_ready_after);
    netstate.my_id = real_id;
    netstate.sp = real_sp;
    ftest_net_fake_reset();
    if (get_gameturn() == before)
    {
        FTEST_FAIL_TEST("the resync didn't restore the game at turn %" PRId64, s_run.t_save);
        return false;
    }
    return true;
}

FTestActionResult ftest_sim_state_continuity_action001(struct FTestActionArgs* const args)
{
    const int64_t t = (int64_t)get_gameturn();
    const int64_t first = s_run.t_save + 1;
    const int64_t last = s_run.t_save + s_run.n_turns;
    switch (s_run.phase)
    {
    case Ph_Start:
        run_reset(env_int("KFX_CONT_T", 2000), env_int("KFX_CONT_N", 2000));
        s_sentinels_at_start = sentinels_hash();
        hand_local_seat_to_ai();
        FTESTLOG("%" PRId64 " keepers, all computer-controlled; save and resync at turn %" PRId64 ", then %" PRId64 " turns compared",
            computer_keepers(), s_run.t_save, s_run.n_turns);
        s_run.phase = Ph_Record;
        return FTRs_Repeat_Current_Action;
    case Ph_Record:
        if (t == s_run.t_save)
        {
            if (!save_and_send())
                break;
            // experiment: what a load or resync does on top of restoring the state, done in the uninterrupted game too
            const int64_t reinit = env_int("KFX_CONT_REINIT", 0);
            if (reinit == 1)
                init_navigation();
            else if (reinit == 2)
                reinit_level_after_load();
            if (reinit != 0)
                FTESTLOG("KFX_CONT_REINIT=%" PRId64 ": the uninterrupted game is reinitialised at turn %" PRId64 " as well", reinit, t);
            return FTRs_Repeat_Current_Action;
        }
        record_or_compare(Ph_Record, first);
        if (t < last)
            return FTRs_Repeat_Current_Action;
        log_sequence_digest();
        if (!load())
            break;
        reset_first_diffs();
        s_run.phase = Ph_Load;
        return FTRs_Repeat_Current_Action;
    case Ph_Load:
        record_or_compare(Ph_Load, first);
        if (t < last)
            return FTRs_Repeat_Current_Action;
        report(Ph_Load, first);
        if (!receive_resync())
            break;
        reset_first_diffs();
        s_run.phase = Ph_Resync;
        return FTRs_Repeat_Current_Action;
    case Ph_Resync:
        record_or_compare(Ph_Resync, first);
        if (t < last)
            return FTRs_Repeat_Current_Action;
        report(Ph_Resync, first);
        s_run.phase = Ph_Done;
        break;
    default:
        break;
    }
    restore_local_seat();
    check_sentinels();
    if (s_run.failures > 0)
        FTEST_FAIL_TEST("%" PRId64 " check(s) failed", s_run.failures);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_sim_state_continuity_init()
{
    s_run.phase = Ph_Start;
    ftest_append_action(ftest_sim_state_continuity_action001, 0, NULL);
    return true;
}

// --- the level started again in the same process

static int64_t s_restart_run = 0;

FTestActionResult ftest_sim_state_continuity_restart_action001(struct FTestActionArgs* const args)
{
    const int64_t t = (int64_t)get_gameturn();
    if (s_run.phase == Ph_Start)
    {
        if (s_restart_run == 0)
        {
            run_reset(0, env_int("KFX_CONT_N", 2000));
            s_sentinels_at_start = sentinels_hash();
        }
        else
            reset_first_diffs();
        s_restart_run++;
        hand_local_seat_to_ai();
        s_run.phase = (s_restart_run == 1) ? Ph_Record : Ph_Restart;
        FTESTLOG("run %" PRId64 " of the level, from turn %" PRId64, s_restart_run, t);
    }
    record_or_compare(s_run.phase, 1);
    if (t < s_run.n_turns)
        return FTRs_Repeat_Current_Action;
    if (s_run.phase == Ph_Record)
        log_sequence_digest();
    else
    {
        report(Ph_Restart, 1);
        check_sentinels();
        if (s_run.failures > 0)
            FTEST_FAIL_TEST("%" PRId64 " check(s) failed", s_run.failures);
    }
    restore_local_seat();
    s_run.phase = Ph_Start;
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_sim_state_continuity_restart_init()
{
    s_run.phase = Ph_Start;
    ftest_append_action(ftest_sim_state_continuity_restart_action001, 0, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
