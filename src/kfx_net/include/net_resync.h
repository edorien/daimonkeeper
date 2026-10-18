/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_resync.h
 *     Header file for net_resync.cpp.
 * @par Purpose:
 *     Network resynchronization for Dungeon Keeper multiplayer.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     31 Oct 2025
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_NET_RESYNC_H
#define DK_NET_RESYNC_H

#include "bflib_basics.h"
#include "bflib_coroutine.h"

#ifdef __cplusplus
extern "C" {
#endif

TbBool LbNetwork_Resync(void *data_buffer, size_t buffer_length);
void animate_resync_progress_bar(int64_t current_phase, int64_t total_phases);

/**
 * The most a full resync can honestly hold once decompressed: the state structs, the frontend and game blobs, the
 * Ariadne mesh, the configuration, and a ceiling for the scripts' data. The header of a resync message states the
 * decompressed size itself, and a client allocates that much; a host (or anyone in its place) could otherwise ask
 * for gigabytes with a few kilobytes of zlib (security pass S05).
 */
size_t resync_max_original_length(void);

/**
 * Whether the simulation state received in a resync, at `data` (sizeof(struct KfxSimState) bytes, not necessarily
 * aligned) passes sim_state_validate(); logs why not. A resync that fails this is refused before anything local
 * changes (security pass S06).
 */
TbBool resync_sim_state_acceptable(const char *data);
/** Whether the configuration received in a resync (KFX_CONFIG_STATE_SAVED_LEN bytes at `data`) passes config_state_validate(). */
TbBool resync_config_state_acceptable(const char *data);
void resync_game(void);

/** The parts of the game state a full resync carries, in its order. */
enum ResyncStatePart {
    RSPart_Game = 0,  /**< struct Game, then kfx_game_state (kfx_game, through GamePort) */
    RSPart_Sim,       /**< kfx_sim_state */
    RSPart_Net,       /**< kfx_net_state */
    RSPart_Ariadne,   /**< the navigation mesh, piece by piece (ariadne_saved_state.h) */
    RSPart_Config,    /**< kfx_config_state's saved part */
    RSPart_Frontend,  /**< kfx_frontend_state (kfx_frontend, through UiPort) */
    RSPart_Count,
};
typedef void (*ResyncStateVisitor)(void *ctx, int part, void *data, size_t size);
/**
 * Visits each raw piece of the game state a full resync carries, at its live address, in the resync's order (the
 * Lua data, which is serialised rather than copied, is not among them). build_resync_game_data() writes them; a
 * replay rewind (kfx_game's replay_rewind.c) snapshots and restores them.
 */
void resync_visit_state(ResyncStateVisitor visit, void *ctx);
/**
 * What goes with the pieces once they have been written back: Ariadne's post-read set-up and the lights' shading
 * rebuilt. apply_resync_game_data() runs it after its copies; so does a replay rewind.
 */
void resync_state_restored(void);
TbBool apply_recorded_resync(const char *message_buffer, size_t message_size);

// Declared here (previously only usable from within net_resync.cpp
// itself) so a caller can drive the host/client halves of a resync
// directly without going through resync_game()'s my_player_number-based
// dispatch -- see src/ftests/tests/ftest_net_resync_fake_multiplayer.c
// and docs/refactor/todo/ftest-fake-multiplayer.md.
TbBool send_resync_game(void);
TbBool receive_resync_game(void);

void intentional_desync();

/**
 * This machine's own pacing and screen state kept in the shared state blobs: the turn clock's phase and the
 * fast-forward (kfx_net_state) and whether the GUI and its panel are shown (kfx_sim_state.operation_flags). A resync
 * or a loaded save replaces those blobs wholesale; keep these across it (upstream #5441 moved them out of the saved
 * game state into its LocalState).
 */
struct LocalPacingState {
    double process_turn_time;
    int64_t frame_skip;
    unsigned char gui_flags;
};
struct LocalPacingState local_pacing_keep(void);
void local_pacing_restore(const struct LocalPacingState *kept);

#ifdef __cplusplus
}
#endif

#endif
