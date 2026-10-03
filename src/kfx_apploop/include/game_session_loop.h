/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_session_loop.h
 *     Header file for game_session_loop.cpp.
 * @par Purpose:
 *     Top-level game/frontend session loop: game_loop(), wait_at_frontend(),
 *     keeper_gameplay_loop(), the per-turn update() dispatcher, and the
 *     frame-pacing helpers around them. Created in stage 12.5
 *     (docs/refactor/stage-12-slim-app-target.md) when this cluster was
 *     physically extracted out of src/main.cpp into its own top-ranked
 *     kfx_apploop library, since it ties every other layer together by
 *     design and doesn't fit the "layer X calls into layer Y via a
 *     callback struct" shape the rest of this refactor uses.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_GAME_SESSION_LOOP_H
#define DK_GAME_SESSION_LOOP_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// quit_game/exit_keeper/FatalError moved to kfx_platform's bflib_basics.h
// (stage 13.3, docs/refactor/stage-13-enforce-and-document.md) --
// genuinely cross-cutting session-exit signals with no single owner
// above kfx_platform; already visible here transitively via this
// header's own bflib_basics.h include.
// redetect_screen_refresh_rate_for_draw() moved to kfx_platform's
// bflib_video.h, same stage -- it only ever touched lbWindow
// (kfx_platform) and fps_limit_current/main/secondary (moved there
// with it), a misclassified function with no real game-loop coupling.

void update(void);
void find_frame_rate(void);
void packet_load_find_frame_rate(uint64_t incr);
int64_t display_should_be_updated_this_turn(void);
TbBool keeper_screen_swap(void);
TbBool keeper_wait_for_next_turn(void);
void keeper_gameplay_loop(void);
void game_loop(void);

/* Called via SessionLoopPort (ports/session_loop_port.def) by
 * kfx_net while blocked on network I/O -- kfx_net is ranked below
 * kfx_apploop, so it cannot call these directly. */
void network_yield_poll_gameplay(void);
void network_yield_waiting_gameplay_packets(void);
void network_yield_draw_frontend(void);

// SessionLoopPort's set_host_packet_received -- kfx_net's net_exchange_common.c updates
// this timestamp, read by the multiplayer clock-adjust logic above. See
// docs/refactor/todo/check-layering-symbol-level-blind-spot.md.
extern long double host_packet_received;

// SessionLoopPort's get_interpolate_time -- kfx_render's engine_render.c reads this
// per-frame interpolation fraction. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
extern double interpolate_time;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
