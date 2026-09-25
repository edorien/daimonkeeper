// Join half of the Phase 2 real ENet loopback session -- see
// ftest_net_enet_loopback_host.c's file comment for the full rationale
// (why this needs two real processes rather than Phase 1's single-process
// sequential-role trick) and docs/refactor/todo/ftest-fake-multiplayer.md.
//
// netstate.sp->join() (bf_enet_join(), bflib_enet.cpp) is itself
// synchronous -- it internally polls enet_host_service() against a real
// timeout (TIMEOUT_CONNECT_DIRECT_IPV4, net_main.h) and only returns once
// connected or timed out, so by the time action001 finishes here the
// underlying real ENet connection genuinely exists.
#include "ftest_net_enet_loopback_join.h"

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

struct ftest_net_enet_loopback_join__variables
{
    // See ftest_net_enet_loopback_host.c's matching field comment --
    // LbNetwork_Init() stores this array's address for the lifetime of
    // the network session.
    struct TbNetworkUserInfo net_player_info[MAX_NET_USERS];

    char ping_buffer[64];
    unsigned char extra_messages_received;
    TbBool saw_broadcast_msg;
    TbBool saw_unsequenced_msg;
};
struct ftest_net_enet_loopback_join__variables ftest_net_enet_loopback_join__vars;

FTestActionResult ftest_net_enet_loopback_join_action001__init_and_join(struct FTestActionArgs* const args);
FTestActionResult ftest_net_enet_loopback_join_action002__reply_to_ping(struct FTestActionArgs* const args);
FTestActionResult ftest_net_enet_loopback_join_action003__extra_messages_stats_and_exit(struct FTestActionArgs* const args);

TbBool ftest_net_enet_loopback_join_init()
{
    ftest_append_action(ftest_net_enet_loopback_join_action001__init_and_join, 0, &ftest_net_enet_loopback_join__vars);
    ftest_append_action(ftest_net_enet_loopback_join_action002__reply_to_ping, 0, &ftest_net_enet_loopback_join__vars);
    ftest_append_action(ftest_net_enet_loopback_join_action003__extra_messages_stats_and_exit, 0, &ftest_net_enet_loopback_join__vars);

    return true;
}

FTestActionResult ftest_net_enet_loopback_join_action001__init_and_join(struct FTestActionArgs* const args)
{
    struct ftest_net_enet_loopback_join__variables* const vars = args->data;
    memset(vars->net_player_info, 0, sizeof(vars->net_player_info));

    if (LbNetwork_Init(NS_ENET_UDP, MAX_NET_USERS, &vars->net_player_info[0], NULL) != Lb_OK)
    {
        FTEST_FAIL_TEST("LbNetwork_Init() failed");
        return FTRs_Go_To_Next_Action;
    }

    if (netstate.sp->join(FTEST_NET_ENET_LOOPBACK_JOIN_ADDRESS, NULL) != Lb_OK)
    {
        FTEST_FAIL_TEST("netstate.sp->join(%s) failed -- is net_enet_loopback_host running? Run both together via scripts/run_ftest_net_enet_loopback.sh", FTEST_NET_ENET_LOOPBACK_JOIN_ADDRESS);
        return FTRs_Go_To_Next_Action;
    }

    // Not discovered via a real login handshake (net_lobby.c, out of scope
    // here) -- known in advance because the host half reserves SERVER_ID
    // for itself before accepting any connection (see its action001).
    netstate.my_id = FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID;

    FTESTLOG("Connected to host at %s", FTEST_NET_ENET_LOOPBACK_JOIN_ADDRESS);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_net_enet_loopback_join_action002__reply_to_ping(struct FTestActionArgs* const args)
{
    struct ftest_net_enet_loopback_join__variables* const vars = args->data;

    size_t ready_size = netstate.sp->msgready(SERVER_ID, 0);
    if (ready_size == 0)
    {
        if (get_gameturn() >= args->intended_start_at_game_turn + FTEST_NET_ENET_LOOPBACK_TURN_BUDGET)
        {
            FTEST_FAIL_TEST("No ping from host within the turn budget");
            netstate.sp->exit();
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }

    size_t received = netstate.sp->readmsg(SERVER_ID, vars->ping_buffer, sizeof(vars->ping_buffer) - 1);
    vars->ping_buffer[received < sizeof(vars->ping_buffer) ? received : sizeof(vars->ping_buffer) - 1] = '\0';

    if (strcmp(vars->ping_buffer, FTEST_NET_ENET_LOOPBACK_PING_MSG) != 0)
    {
        FTEST_FAIL_TEST("Unexpected message from host: '%s' (expected '%s')", vars->ping_buffer, FTEST_NET_ENET_LOOPBACK_PING_MSG);
        netstate.sp->exit();
        return FTRs_Go_To_Next_Action;
    }

    netstate.sp->sendmsg_single(SERVER_ID, FTEST_NET_ENET_LOOPBACK_PONG_MSG, sizeof(FTEST_NET_ENET_LOOPBACK_PONG_MSG));
    FTESTLOG("Real ENet loopback ping/pong round trip (join side) succeeded, waiting for host's follow-up messages");
    return FTRs_Go_To_Next_Action;
}

// Covers the two more NetSP entry points the ping/pong exchange never
// reaches (sendmsg_all()/sendmsg_single_unsequenced(), see
// ftest_net_enet_loopback_host.c's matching action for the fuller
// rationale) from the *receiving* side, plus the connection-quality query
// functions' client-side branch (GetPing() et al. have an
// `if (IsPeerConnected(client_peer))` fast path specific to the join
// side, distinct from the host-side peer-list-scan branch the host's own
// action already covers -- calling them from both processes exercises
// both branches, not just one).
FTestActionResult ftest_net_enet_loopback_join_action003__extra_messages_stats_and_exit(struct FTestActionArgs* const args)
{
    struct ftest_net_enet_loopback_join__variables* const vars = args->data;

    while (vars->extra_messages_received < 2)
    {
        size_t ready_size = netstate.sp->msgready(SERVER_ID, 0);
        if (ready_size == 0)
        {
            if (get_gameturn() >= args->intended_start_at_game_turn + FTEST_NET_ENET_LOOPBACK_TURN_BUDGET)
            {
                FTEST_FAIL_TEST("Only received %" PRIu64 "/2 follow-up messages from host within the turn budget", (uint64_t)vars->extra_messages_received);
                netstate.sp->exit();
                return FTRs_Go_To_Next_Action;
            }
            return FTRs_Repeat_Current_Action;
        }

        char buffer[64];
        size_t received = netstate.sp->readmsg(SERVER_ID, buffer, sizeof(buffer) - 1);
        buffer[received < sizeof(buffer) ? received : sizeof(buffer) - 1] = '\0';
        vars->extra_messages_received++;

        // ENet doesn't guarantee delivery order *across* channels (the
        // broadcast used the reliable channel, the unsequenced message a
        // different one) -- check membership, not a fixed order.
        if (strcmp(buffer, FTEST_NET_ENET_LOOPBACK_BROADCAST_MSG) == 0)
        {
            vars->saw_broadcast_msg = true;
        }
        else if (strcmp(buffer, FTEST_NET_ENET_LOOPBACK_UNSEQUENCED_MSG) == 0)
        {
            vars->saw_unsequenced_msg = true;
        }
        else
        {
            FTEST_FAIL_TEST("Unexpected follow-up message from host: '%s'", buffer);
            netstate.sp->exit();
            return FTRs_Go_To_Next_Action;
        }
    }

    // Ack so the host knows it's safe to drop_user()/exit() -- see
    // ftest_net_enet_loopback_shared.h's FTEST_NET_ENET_LOOPBACK_EXTRAS_ACK_MSG
    // comment. Settle briefly before this side's own exit() too, same
    // reasoning as the ping/pong exchange's fix: the ack itself needs real
    // time to actually reach the host before this socket goes away.
    netstate.sp->sendmsg_single(SERVER_ID, FTEST_NET_ENET_LOOPBACK_EXTRAS_ACK_MSG, sizeof(FTEST_NET_ENET_LOOPBACK_EXTRAS_ACK_MSG));
    netstate.sp->msgready(SERVER_ID, 250);

    uint64_t ping = GetPing(SERVER_ID, FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID);
    uint64_t packet_loss = GetPacketLoss(SERVER_ID, FTEST_NET_ENET_LOOPBACK_JOIN_USER_ID);
    uint64_t data_in_transit = GetClientDataInTransit();
    uint64_t packets_lost = GetClientPacketsLost();
    uint64_t upload_rate = GetUploadRateBytesPerSecond();
    uint64_t download_rate = GetDownloadRateBytesPerSecond();
    FTESTLOG("Connection stats: ping=%" PRIu64 " packet_loss=%" PRIu64 " data_in_transit=%" PRIu64 " packets_lost=%" PRIu64 " upload_rate=%" PRIu64 " download_rate=%" PRIu64,
        (uint64_t)(ping), (uint64_t)(packet_loss), (uint64_t)(data_in_transit), (uint64_t)(packets_lost), (uint64_t)(upload_rate), (uint64_t)(download_rate));

    netstate.sp->exit();
    memset(&netstate, 0, sizeof(netstate));

    if (!vars->saw_broadcast_msg || !vars->saw_unsequenced_msg)
    {
        FTEST_FAIL_TEST("Didn't see both follow-up messages (broadcast=%" PRId64 " unsequenced=%" PRId64 ")", (int64_t)vars->saw_broadcast_msg, (int64_t)vars->saw_unsequenced_msg);
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("Real ENet loopback round trip (join side) succeeded");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
