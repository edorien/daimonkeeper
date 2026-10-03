// Phase 1 of docs/refactor/todo/ftest-fake-multiplayer.md: proves out the
// fake NetSP (../ftest_net_fake.h) by driving a real, running ftest
// game's host-send / client-receive resync round trip in a single
// process. Targets the functions net_resync_test.cpp's Catch2 suite
// explicitly stops short of (its own file comment says so) because they
// need a live netstate.sp moving bytes between two endpoints:
// send_resync_game()/receive_resync_game() (and, transitively,
// send_resync_data()/receive_resync_data()) -- all previously at 0%
// coverage per docs/refactor/todo/ftest-fake-multiplayer.md's table.
//
// Approach: rather than actually running two roles concurrently (which
// the single process-wide `netstate` global doesn't allow -- see the
// plan doc), this test plays both roles of the same process
// *sequentially* against one fake wire: snapshot the current real state,
// send it as the "host", corrupt the live state the same way the debug
// console's "desync" command does (net_resync.cpp::intentional_desync()),
// then receive it back as the "client" and prove the live state was
// restored byte-for-byte. The fake wire is genuinely exercised end to
// end (real zlib compress/decompress, real CRC verification, real
// ResyncHeader framing, real ScriptPort lua_resync_export/import) --
// only the transport (sockets, ENet) is faked.
#include "ftest_net_resync_fake_multiplayer.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdlib.h>
#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_net_fake.h"

#include "net_main.h"
#include "net_resync.h"
#include "config_keeperfx.h"
#include "game_legacy.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "kfx_game_state.h"
#include "kfx_frontend_state.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ftest_net_resync_fake_multiplayer__variables
{
    const struct NetSP *saved_netstate_sp;

    void *snapshot_game;
    void *snapshot_kfx_sim_state;
    void *snapshot_kfx_net_state;
    void *snapshot_kfx_game_state;
    void *snapshot_kfx_frontend_state;

    uint64_t instance_remain_before;
};
struct ftest_net_resync_fake_multiplayer__variables ftest_net_resync_fake_multiplayer__vars = {
    .saved_netstate_sp = NULL,
    .snapshot_game = NULL,
    .snapshot_kfx_sim_state = NULL,
    .snapshot_kfx_net_state = NULL,
    .snapshot_kfx_game_state = NULL,
    .snapshot_kfx_frontend_state = NULL,
    .instance_remain_before = 0,
};

FTestActionResult ftest_net_resync_fake_multiplayer_action001__host_client_roundtrip(struct FTestActionArgs* const args);

TbBool ftest_net_resync_fake_multiplayer_init()
{
    ftest_append_action(ftest_net_resync_fake_multiplayer_action001__host_client_roundtrip, 0, &ftest_net_resync_fake_multiplayer__vars);

    return true;
}

// Deliberately a single action, not split across a game-turn boundary
// like most other ftests: several kfx_net_state fields (input_lag_turns
// and friends, net_input_lag.c) are legitimately re-derived every game
// turn regardless of resync activity, so snapshotting before one turn
// and comparing after the next would report a false failure that has
// nothing to do with whether the resync round trip itself is correct.
FTestActionResult ftest_net_resync_fake_multiplayer_action001__host_client_roundtrip(struct FTestActionArgs* const args)
{
    struct ftest_net_resync_fake_multiplayer__variables* const vars = args->data;

    // Install the fake NetSP as the host, with exactly one fake logged-in
    // "client" (id 1) so send_resync_data()'s per-user broadcast loop has
    // somewhere to send.
    vars->saved_netstate_sp = netstate.sp;
    ftest_net_fake_reset();
    netstate.sp = ftest_net_fake_sp();
    memset(netstate.users, 0, sizeof(netstate.users));
    netstate.users[1].id = 1;
    netstate.users[1].progress = USER_LOGGEDIN;
    ftest_net_fake_set_role_host();

    if (!send_resync_game())
    {
        FTEST_FAIL_TEST("send_resync_game() failed while playing the host role");
        return FTRs_Go_To_Next_Action;
    }

    // Snapshot every struct send_resync_game()/receive_resync_game() serialize
    // wholesale (same list as net_resync.cpp's own fixed_state_size), so the
    // client half below can prove the round trip restored them byte-for-byte.
    // Deliberately taken *after* send_resync_game() returns, not before:
    // send_resync_game() itself legitimately mutates kfx_net_state as part of
    // packing it (pack_desync_history_for_resync(), net_checksums.c, freshly
    // computes host_checksums/log_snapshot every call) -- that's the state
    // that actually went out on the wire, so it's the state the round trip
    // is expected to restore, not whatever kfx_net_state held a moment
    // earlier.
    vars->snapshot_game = malloc(sizeof(game));
    vars->snapshot_kfx_sim_state = malloc(sizeof(kfx_sim_state));
    vars->snapshot_kfx_net_state = malloc(sizeof(kfx_net_state));
    vars->snapshot_kfx_game_state = malloc(sizeof(kfx_game_state));
    vars->snapshot_kfx_frontend_state = malloc(sizeof(kfx_frontend_state));
    if (vars->snapshot_game == NULL || vars->snapshot_kfx_sim_state == NULL || vars->snapshot_kfx_net_state == NULL
        || vars->snapshot_kfx_game_state == NULL || vars->snapshot_kfx_frontend_state == NULL)
    {
        FTEST_FAIL_TEST("Failed to allocate state snapshot buffers");
        return FTRs_Go_To_Next_Action;
    }
    memcpy(vars->snapshot_game, &game, sizeof(game));
    memcpy(vars->snapshot_kfx_sim_state, &kfx_sim_state, sizeof(kfx_sim_state));
    memcpy(vars->snapshot_kfx_net_state, &kfx_net_state, sizeof(kfx_net_state));
    memcpy(vars->snapshot_kfx_game_state, &kfx_game_state, sizeof(kfx_game_state));
    memcpy(vars->snapshot_kfx_frontend_state, &kfx_frontend_state, sizeof(kfx_frontend_state));

    // Force real divergence the same way the debug console's "desync"
    // command does, so the next action's receive side has something
    // real to fix. get_player(0)->instance_remain_turns += 1 is
    // intentional_desync()'s one unconditional effect (net_resync.cpp) --
    // check it directly rather than assuming which snapshot it landed in.
    vars->instance_remain_before = get_player(0)->instance_remain_turns;
    intentional_desync();
    if (get_player(0)->instance_remain_turns != vars->instance_remain_before + 1)
    {
        FTEST_FAIL_TEST("intentional_desync() didn't corrupt state as expected -- can't prove the receive side actually fixes anything");
        return FTRs_Go_To_Next_Action;
    }

    // Same game turn, same process, now playing the client: receive the
    // blob the host half above just sent over the fake wire and prove it
    // restores the pristine snapshot byte-for-byte.
    ftest_net_fake_set_role_client(1);

    TbBool receive_ok = receive_resync_game();
    TbBool game_ok = receive_ok && (memcmp(vars->snapshot_game, &game, sizeof(game)) == 0);
    TbBool sim_ok = receive_ok && (memcmp(vars->snapshot_kfx_sim_state, &kfx_sim_state, sizeof(kfx_sim_state)) == 0);
    TbBool net_ok = receive_ok && (memcmp(vars->snapshot_kfx_net_state, &kfx_net_state, sizeof(kfx_net_state)) == 0);
    TbBool gamest_ok = receive_ok && (memcmp(vars->snapshot_kfx_game_state, &kfx_game_state, sizeof(kfx_game_state)) == 0);
    TbBool front_ok = receive_ok && (memcmp(vars->snapshot_kfx_frontend_state, &kfx_frontend_state, sizeof(kfx_frontend_state)) == 0);
    uint64_t instance_remain_restored = get_player(0)->instance_remain_turns;

    free(vars->snapshot_game);
    free(vars->snapshot_kfx_sim_state);
    free(vars->snapshot_kfx_net_state);
    free(vars->snapshot_kfx_game_state);
    free(vars->snapshot_kfx_frontend_state);
    netstate.sp = vars->saved_netstate_sp;
    memset(netstate.users, 0, sizeof(netstate.users));
    ftest_net_fake_reset();

    if (!receive_ok)
    {
        FTEST_FAIL_TEST("receive_resync_game() failed while playing the client role");
        return FTRs_Go_To_Next_Action;
    }
    if (!game_ok || !sim_ok || !net_ok || !gamest_ok || !front_ok)
    {
        FTEST_FAIL_TEST("Resync round-trip did not restore state byte-for-byte (game=%" PRId64 " sim=%" PRId64 " net=%" PRId64 " game_state=%" PRId64 " frontend=%" PRId64 ")",
            (int64_t)game_ok, (int64_t)sim_ok, (int64_t)net_ok, (int64_t)gamest_ok, (int64_t)front_ok);
        return FTRs_Go_To_Next_Action;
    }
    if (instance_remain_restored != vars->instance_remain_before)
    {
        FTEST_FAIL_TEST("get_player(0)->instance_remain_turns wasn't restored by resync (expected %" PRIu64 ", got %" PRIu64 ")",
            (uint64_t)(vars->instance_remain_before), (uint64_t)(instance_remain_restored));
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("Fake host->client resync round-trip restored state byte-for-byte");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
