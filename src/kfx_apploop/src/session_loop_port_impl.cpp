/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file session_loop_port_impl.cpp
 *     kfx_apploop's SessionLoopPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "session_loop_port_impl.h"
#include "game_session_loop.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// net_exchange_common.c can't reach kfx_apploop's host_packet_received
// directly.
static void set_host_packet_received(long double value) { host_packet_received = value; }
// engine_render.c can't reach kfx_apploop's interpolate_time directly.
static double get_interpolate_time(void) { return interpolate_time; }

const struct SessionLoopPort kfx_apploop_session_loop_port = {
    .network_yield_poll_gameplay = &network_yield_poll_gameplay,
    .network_yield_waiting_gameplay_packets = &network_yield_waiting_gameplay_packets,
    .network_yield_draw_frontend = &network_yield_draw_frontend,
    .set_host_packet_received = &set_host_packet_received,
    .get_interpolate_time = &get_interpolate_time,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
