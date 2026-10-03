/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file local_view.h
 *     Header file for local_view.c.
 * @par Purpose:
 *     The local player's view transitions (possession, the parchment map,
 *     level start): what the local machine shows, not what the simulation
 *     does. kfx_sim reports them through UiPort's local_view_transition.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_LOCAL_VIEW_H
#define DK_LOCAL_VIEW_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct PlayerInfo;

/** UiPort's local_view_transition; kind is an enum LocalViewTransition. */
void local_view_transition(struct PlayerInfo *player, int64_t kind);

/** Hides the status menu and turns tooltips off for the map and its fades, or
 *  puts them back. Each argument is the state wanted from this call on. */
void set_map_ui_hidden(TbBool status_menu, TbBool tooltips);
/** set_map_ui_hidden() with the status-menu toggle passed in, for tests. */
void set_map_ui_hidden_with(TbBool status_menu, TbBool tooltips, uint64_t (*toggle_status)(int64_t visible));
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
