/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_port_impl.c
 *     kfx_net's NetPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "net_port_impl.h"
#include "net_exchange_gameplay.h"
#include "net_matchmaking.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// NetPort's matchmaking entries:
// config_keeperfx.c's MATCHMAKING_SERVER config var can't reach
// net_matchmaking.h's matchmaking_enabled/matchmaking_ws_url plain
// extern variables directly (kfx_net is above kfx_config).
static void matchmaking_config_set_enabled(TbBool enabled)
{
    matchmaking_enabled = enabled;
}

static const char *matchmaking_config_get_ws_url(void)
{
    return matchmaking_ws_url;
}


const struct NetPort kfx_net_port = {
    .get_history_packet = &get_history_packet,
    .matchmaking_set_enabled = &matchmaking_config_set_enabled,
    .matchmaking_set_server = &matchmaking_set_server,
    .matchmaking_get_ws_url = &matchmaking_config_get_ws_url,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
