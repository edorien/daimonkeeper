// Phase 2 of docs/refactor/todo/ftest-fake-multiplayer.md: real coverage
// for src/kfx_platform/src/bflib_enet.cpp (the check_layering.py accepted
// residual's other file, architecture.md SS8.2), which Phase 1's fake NetSP
// deliberately bypasses -- it *is* the thing being faked there. bflib_enet.cpp
// keeps its ENet state in anonymous-namespace globals (a single `ENetHost
// *host` and `ENetPeer *client_peer`), so two NetSP instances can't coexist
// in one process the way Phase 1's two roles did: bf_enet_join()'s
// create_join_host() unconditionally tears down and replaces the same
// `host` global bf_enet_host() just set up. The only way to exercise a
// real host+join+send+receive round trip is genuinely two processes -- this
// is the host half; ftest_net_enet_loopback_join.c is the other.
//
// Deliberately drives LbNetwork_Init()/netstate.sp directly rather than
// going through setup_network_service() (frontend-only; gated to
// FrontendNetSvc_Online/LAN, drags in lobby-screen machinery this test
// doesn't need) or the full net_lobby.c NETMSG_LOGIN handshake (a separate
// file from the one this plan is targeting). netstate.my_id/users[SERVER_ID]
// are set directly here the same way net_lobby.c's real host path does
// (net_lobby.c:256's `netstate.my_id = SERVER_ID;`), just without the wire
// handshake that normally produces it -- enough to keep OnNewUser() (real
// production code, net_main.c) from assigning the incoming client to slot 0.
//
// Beyond the original ping/pong round trip, this pair also exercises
// sendmsg_all()/sendmsg_single_unsequenced(), the connection-quality query
// functions (GetPing() et al.), and drop_user() -- see action004/005's own
// comments below for why each needed the real two-process session rather
// than being addable to Phase 1's single-process fake.
#include "ftest_net_enet_loopback_host.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"
#include "ftest_net_enet_loopback_shared.h"

#include "config_keeperfx.h"
#include "net_main.h"
#include "bflib_enet.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ftest_net_enet_loopback_host__variables
{
    // LbNetwork_Init() stores this array's address (net_main.c's static
    // local_player_info) and later net.c code (e.g. UpdateLocalPlayerInfo(),
    // called from OnDroppedUser()) dereferences it -- needs to outlive the
    // action that calls LbNetwork_Init(), not just that function's own stack.
    struct TbNetworkUserInfo net_player_info[MAX_NET_USERS];

    TbBool sent_ping;
    char reply_buffer[64];
};
struct ftest_net_enet_loopback_host__variables ftest_net_enet_loopback_host__vars = {
    .sent_ping = false,
};

FTestActionResult ftest_net_enet_loopback_host_action001__init_and_host(struct FTestActionArgs* const args);
FTestActionResult ftest_net_enet_loopback_host_action002__wait_for_client(struct FTestActionArgs* const args);
FTestActionResult ftest_net_enet_loopback_host_action003__ping_pong(struct FTestActionArgs* const args);
FTestActionResult ftest_net_enet_loopback_host_action004__broadcast(struct FTestActionArgs* const args);
FTestActionResult ftest_net_enet_loopback_host_action005__wait_for_ack_stats_and_exit(struct FTestActionArgs* const args);

TbBool ftest_net_enet_loopback_host_init()
{
    ftest_append_action(ftest_net_enet_loopback_host_action001__init_and_host, 0, &ftest_net_enet_loopback_host__vars);
    ftest_append_action(ftest_net_enet_loopback_host_action002__wait_for_client, 0, &ftest_net_enet_loopback_host__vars);
    ftest_append_action(ftest_net_enet_loopback_host_action003__ping_pong, 0, &ftest_net_enet_loopback_host__vars);
    ftest_append_action(ftest_net_enet_loopback_host_action004__broadcast, 0, &ftest_net_enet_loopback_host__vars);
    ftest_append_action(ftest_net_enet_loopback_host_action005__wait_for_ack_stats_and_exit, 0, &ftest_net_enet_loopback_host__vars);

    return true;
}

FTestActionResult ftest_net_enet_loopback_host_action001__init_and_host(struct FTestActionArgs* const args)
{
    struct ftest_net_enet_loopback_host__variables* const vars = args->data;
    memset(vars->net_player_info, 0, sizeof(vars->net_player_info));

    if (LbNetwork_Init(NS_ENET_UDP, MAX_NET_USERS, &vars->net_player_info[0], NULL) != Lb_OK)
    {
        FTEST_FAIL_TEST("LbNetwork_Init() failed");
        return FTRs_Go_To_Next_Action;
    }

    // Reserve slot SERVER_ID for ourselves so OnNewUser() (net_main.c,
    // real production code) assigns the incoming client to a different
    // slot instead of colliding with the host's own identity.
    netstate.my_id = SERVER_ID;
    netstate.users[SERVER_ID].progress = USER_LOGGEDIN;

    char port_string[16];
    snprintf(port_string, sizeof(port_string), "%" PRId64, (int64_t)(FTEST_NET_ENET_LOOPBACK_PORT));
    if (netstate.sp->host(port_string, NULL) != Lb_OK)
    {
        FTEST_FAIL_TEST("netstate.sp->host() failed to bind port %" PRId64, (int64_t)(FTEST_NET_ENET_LOOPBACK_PORT));
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("Hosting on port %" PRId64 ", waiting for a client to connect", (int64_t)(FTEST_NET_ENET_LOOPBACK_PORT));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_net_enet_loopback_host_action002__wait_for_client(struct FTestActionArgs* const args)
{
    netstate.sp->update(OnNewUser);

    if (netstate.users[FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID].progress != USER_UNUSED)
    {
        FTESTLOG("Client connected as user %" PRId64, (int64_t)(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID));
        return FTRs_Go_To_Next_Action;
    }

    if (get_gameturn() >= args->intended_start_at_game_turn + FTEST_NET_ENET_LOOPBACK_TURN_BUDGET)
    {
        FTEST_FAIL_TEST("No client connected within the turn budget -- run this test together with net_enet_loopback_join via scripts/run_ftest_net_enet_loopback.sh, not standalone");
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Repeat_Current_Action;
}

FTestActionResult ftest_net_enet_loopback_host_action003__ping_pong(struct FTestActionArgs* const args)
{
    struct ftest_net_enet_loopback_host__variables* const vars = args->data;

    if (!vars->sent_ping)
    {
        netstate.sp->sendmsg_single(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID, FTEST_NET_ENET_LOOPBACK_PING_MSG, sizeof(FTEST_NET_ENET_LOOPBACK_PING_MSG));
        vars->sent_ping = true;
        FTESTLOG("Sent ping to client, waiting for reply");
    }

    size_t ready_size = netstate.sp->msgready(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID, 0);
    if (ready_size == 0)
    {
        if (get_gameturn() >= args->intended_start_at_game_turn + FTEST_NET_ENET_LOOPBACK_TURN_BUDGET)
        {
            FTEST_FAIL_TEST("No reply from client within the turn budget");
            netstate.sp->exit();
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }

    size_t received = netstate.sp->readmsg(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID, vars->reply_buffer, sizeof(vars->reply_buffer) - 1);
    vars->reply_buffer[received < sizeof(vars->reply_buffer) ? received : sizeof(vars->reply_buffer) - 1] = '\0';

    if (strcmp(vars->reply_buffer, FTEST_NET_ENET_LOOPBACK_PONG_MSG) != 0)
    {
        FTEST_FAIL_TEST("Unexpected reply from client: '%s' (expected '%s')", vars->reply_buffer, FTEST_NET_ENET_LOOPBACK_PONG_MSG);
        netstate.sp->exit();
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("Real ENet loopback ping/pong round trip (host side) succeeded");
    return FTRs_Go_To_Next_Action;
}

// Covers two bflib_enet.cpp NetSP entry points the ping/pong exchange
// above never reaches: sendmsg_all() (bf_enet_sendmsg_all) and
// sendmsg_single_unsequenced() (bf_enet_sendmsg_single_unsequenced --
// ENET_CHANNEL_UNSEQUENCED, a different channel from the reliable one
// ping/pong used, but still delivered through the same per-source mailbox
// readmsg()/msgready() already use). Doesn't wait for delivery here --
// see the next action for why (it waits for an explicit ack instead).
FTestActionResult ftest_net_enet_loopback_host_action004__broadcast(struct FTestActionArgs* const args)
{
    netstate.sp->sendmsg_all(FTEST_NET_ENET_LOOPBACK_BROADCAST_MSG, sizeof(FTEST_NET_ENET_LOOPBACK_BROADCAST_MSG));
    netstate.sp->sendmsg_single_unsequenced(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID, FTEST_NET_ENET_LOOPBACK_UNSEQUENCED_MSG, sizeof(FTEST_NET_ENET_LOOPBACK_UNSEQUENCED_MSG));
    FTESTLOG("Sent broadcast + unsequenced messages to client, waiting for ack");
    return FTRs_Go_To_Next_Action;
}

// Waits for the join side's explicit ack (see
// ftest_net_enet_loopback_shared.h's FTEST_NET_ENET_LOOPBACK_EXTRAS_ACK_MSG
// comment for why a blind settle-and-hope isn't enough here) before
// covering the connection-quality query functions
// (GetPing/GetPacketLoss/GetClientDataInTransit/GetClientPacketsLost/
// GetUploadRateBytesPerSecond/GetDownloadRateBytesPerSecond -- normally
// only called from frontend UI to show connection stats, safe to call
// directly here once a real connection exists) and drop_user()
// (bf_enet_drop_user, the *manual* disconnect path -- everywhere else in
// this pair, disconnection only ever happens as a side effect of exit()).
FTestActionResult ftest_net_enet_loopback_host_action005__wait_for_ack_stats_and_exit(struct FTestActionArgs* const args)
{
    struct ftest_net_enet_loopback_host__variables* const vars = args->data;

    size_t ready_size = netstate.sp->msgready(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID, 0);
    if (ready_size == 0)
    {
        if (get_gameturn() >= args->intended_start_at_game_turn + FTEST_NET_ENET_LOOPBACK_TURN_BUDGET)
        {
            FTEST_FAIL_TEST("No ack from client within the turn budget");
            netstate.sp->exit();
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }

    size_t received = netstate.sp->readmsg(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID, vars->reply_buffer, sizeof(vars->reply_buffer) - 1);
    vars->reply_buffer[received < sizeof(vars->reply_buffer) ? received : sizeof(vars->reply_buffer) - 1] = '\0';
    TbBool ack_ok = (strcmp(vars->reply_buffer, FTEST_NET_ENET_LOOPBACK_EXTRAS_ACK_MSG) == 0);

    // Values are real but non-deterministic (actual measured RTT/loss/
    // throughput over a real, if loopback, UDP socket) -- nothing here to
    // assert against, this is purely to exercise the functions themselves.
    uint64_t ping = GetPing(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID, SERVER_ID);
    uint64_t packet_loss = GetPacketLoss(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID, SERVER_ID);
    uint64_t data_in_transit = GetClientDataInTransit();
    uint64_t packets_lost = GetClientPacketsLost();
    uint64_t upload_rate = GetUploadRateBytesPerSecond();
    uint64_t download_rate = GetDownloadRateBytesPerSecond();
    FTESTLOG("Connection stats: ping=%" PRIu64 " packet_loss=%" PRIu64 " data_in_transit=%" PRIu64 " packets_lost=%" PRIu64 " upload_rate=%" PRIu64 " download_rate=%" PRIu64,
        (uint64_t)(ping), (uint64_t)(packet_loss), (uint64_t)(data_in_transit), (uint64_t)(packets_lost), (uint64_t)(upload_rate), (uint64_t)(download_rate));

    netstate.sp->drop_user(FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID);
    netstate.sp->exit();
    memset(&netstate, 0, sizeof(netstate));

    if (!ack_ok)
    {
        FTEST_FAIL_TEST("Unexpected ack from client: '%s' (expected '%s')", vars->reply_buffer, FTEST_NET_ENET_LOOPBACK_EXTRAS_ACK_MSG);
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("Real ENet loopback round trip (host side) succeeded");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
