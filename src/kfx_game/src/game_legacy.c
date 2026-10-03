/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_legacy.c
 *     Module which contains the legacy Game structure.
 * @par Purpose:
 *     Allows easy saving and loading of game data.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     21 Oct 2009 - 23 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "game_legacy.h"

#include "globals.h"
#include "bflib_basics.h"
#include "state_versions.h"
#include <string.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Game game;
// Saved/resynced as raw bytes: see state_versions.h before changing the layout.
_Static_assert(sizeof(struct Game) == KFX_GAME_ORIG_SIZE, "struct Game changed size: bump KFX_GAME_ORIG_VER and update KFX_GAME_ORIG_SIZE in state_versions.h");

// Static buffer, not malloc'd -- mirrors lua_resync_export()'s (pointer,
// length) shape (kfx_script/src/lua_base.c) but neither of these structs
// need real serialization, just a wholesale memcpy, so there's nothing to
// allocate/free per call the way Lua's variable-length export needs.
static char resync_game_state_buffer[sizeof(struct Game) + sizeof(struct KfxGameState)];

const char *resync_export_game_state(size_t *len)
{
    char *write_ptr = resync_game_state_buffer;
    memcpy(write_ptr, &game, sizeof(game));
    write_ptr += sizeof(game);
    memcpy(write_ptr, &kfx_game_state, sizeof(kfx_game_state));
    *len = sizeof(resync_game_state_buffer);
    return resync_game_state_buffer;
}

TbBool resync_import_game_state(const char *data, size_t len)
{
    if (len != sizeof(resync_game_state_buffer)) {
        ERRORLOG("Received game state with wrong size: %" PRIu64 " != %" PRIu64, (uint64_t)len, (uint64_t)sizeof(resync_game_state_buffer));
        return false;
    }
    memcpy(&game, data, sizeof(game));
    memcpy(&kfx_game_state, data + sizeof(game), sizeof(kfx_game_state));
    return true;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
/******************************************************************************/
