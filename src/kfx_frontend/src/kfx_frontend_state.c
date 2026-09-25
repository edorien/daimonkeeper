/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_frontend_state.c
 *     Global instance for kfx_frontend_state.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx_frontend_state.h"
#include "bflib_fileio.h"
#include <string.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct KfxFrontendState kfx_frontend_state;

// Registered on GameCallbacks (src/kfx_config/include/game_callbacks.h)
// so kfx_game's game_saves.c/main_game.c don't need to reach up into
// kfx_frontend_state.h directly to save/load/reset this struct as a
// raw blob.
TbBool save_frontend_state(TbFileHandle fhandle)
{
    return LbFileWrite(fhandle, &kfx_frontend_state, sizeof(struct KfxFrontendState)) == sizeof(struct KfxFrontendState);
}

struct KfxFrontendLocal kfx_frontend_local;

TbBool load_frontend_state(TbFileHandle fhandle)
{
    return LbFileRead(fhandle, &kfx_frontend_state, sizeof(struct KfxFrontendState)) == sizeof(struct KfxFrontendState);
}

void reset_frontend_state(void)
{
    memset(&kfx_frontend_state, 0, sizeof(struct KfxFrontendState));
    memset(&kfx_frontend_local, 0, sizeof(kfx_frontend_local));
}

size_t get_frontend_state_size(void)
{
    return sizeof(struct KfxFrontendState);
}

// Registered on NetCallbacks (kfx_config/include/net_callbacks.h) --
// same reasoning as save_frontend_state()/load_frontend_state() above,
// just returning a (pointer, length) blob for the network resync payload
// (net_resync.cpp) instead of writing to a file handle.
const char *resync_export_frontend_state(size_t *len)
{
    *len = sizeof(kfx_frontend_state);
    return (const char *)&kfx_frontend_state;
}

TbBool resync_import_frontend_state(const char *data, size_t len)
{
    if (len != sizeof(kfx_frontend_state)) {
        ERRORLOG("Received frontend state with wrong size: %" PRIu64 " != %" PRIu64, (uint64_t)len, (uint64_t)sizeof(kfx_frontend_state));
        return false;
    }
    memcpy(&kfx_frontend_state, data, len);
    return true;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
