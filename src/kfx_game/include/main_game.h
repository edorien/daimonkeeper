/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file main_game.h
 *     Header file for main_game.c.
 * @par Purpose:
 *     Level/session lifecycle: starting, clearing, and resetting a game
 *     session. Created in stage 12 (docs/refactor/stage-12-slim-app-target.md)
 *     as functions get migrated out of src/main.cpp -- grows as later
 *     stage-12 sub-stages add more.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_MAIN_GAME_H
#define DK_MAIN_GAME_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_coroutine.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct PlayerInfo;

extern int64_t default_loc_player;

void clear_game_for_summary(void);
void clear_game(void);
void reinit_level_after_load(void);
void startup_network_game(CoroutineLoop *context, TbBool local);
void faststartup_network_game(CoroutineLoop *context);
void faststartup_saved_packet_game(void);
CoroutineLoopState set_not_has_quit(CoroutineLoop *context);
void set_general_information(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y);
void set_general_information_with_icon(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx);
void set_quick_information(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y);
void set_quick_information_with_icon(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx);
void process_objective(const char *msg_text, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y);
void process_objective_with_icon(const char *msg_text, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx);
void set_general_objective(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y);
void set_general_objective_with_icon(int64_t msg_id, PlayerNumber plyr_idx, TbMapLocation target, MapSubtlCoord x, MapSubtlCoord y, int64_t icon_idx);
int64_t winning_player_quitting(struct PlayerInfo *player, int64_t *plyr_count);
int64_t lose_level(struct PlayerInfo *player);
int64_t resign_level(struct PlayerInfo *player);
int64_t complete_level(struct PlayerInfo *player);

void reset_script_timers_and_flags(void);
void clear_complete_game(void);
void init_seeds();
TbBool startup_saved_packet_game(void);

// docs/refactor/editor/01-entry-and-editor-session.md §4/§5 -- the one
// sanctioned new kfx_game function for the in-game level editor: one
// player, zombie keepers, optionally-trimmed post-init, optionally-paused.
// Coroutine-staged like startup_network_game() above (kfx_apploop's
// `case FeSt_START_EDITOR:` adds its steps to the same loop). When
// new_map is set, editor_request_blank_map() below is called first so
// init_level() builds a fresh blank map instead of loading lvnum from
// disk.
void startup_local_game_for_editor(CoroutineLoop *context, LevelNumber lvnum,
    TbBool suspend, TbBool trim_post_init);

// Arms a one-shot "build a blank map" request that the next
// startup_local_game_for_editor()/init_level() call consumes instead of
// loading the requested level number from disk. See create_blank_map()
// (kfx_sim, lvl_filesdk1.h).
void editor_request_blank_map(MapSlabCoord tiles_x, MapSlabCoord tiles_y, int64_t texture_set);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
