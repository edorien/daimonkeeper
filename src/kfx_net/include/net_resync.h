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
void store_localised_game_structure(void);
void recall_localised_game_structure(void);
void resync_game(void);
TbBool apply_recorded_resync(const char *message_buffer, size_t message_size);

// Declared here (previously only usable from within net_resync.cpp
// itself) so a caller can drive the host/client halves of a resync
// directly without going through resync_game()'s my_player_number-based
// dispatch -- see src/ftests/tests/ftest_net_resync_fake_multiplayer.c
// and docs/refactor/todo/ftest-fake-multiplayer.md.
TbBool send_resync_game(void);
TbBool receive_resync_game(void);

void intentional_desync();

#ifdef __cplusplus
}
#endif

#endif
