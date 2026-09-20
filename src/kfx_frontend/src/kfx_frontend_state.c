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

// kfx_frontend_state is saved/loaded/resynced as one raw blob, but a few members are pointers into THIS
// process (the cheat-menu boxes point into the static gui_boxes[] array; level_names_data into the heap).
// A blob written by another process -- an earlier run, a multiplayer host -- carries that process's
// addresses (different under ASLR/PIE and between 32- and 64-bit builds), and the next
// gui_box_is_not_valid(kfx_frontend_state.gui_cheat_box_1) would dereference them. So an import keeps the
// live process's own pointers.
struct FrontendProcessPointers {
    struct GuiBox *gui_cheat_box_1;
    struct GuiBox *gui_cheat_box_3;
    struct GuiBox *gui_cheat_box_4;
    char *level_names_data;
    char *end_level_names_data;
};

static void frontend_pointers_capture(struct FrontendProcessPointers *ptrs)
{
    ptrs->gui_cheat_box_1 = kfx_frontend_state.gui_cheat_box_1;
    ptrs->gui_cheat_box_3 = kfx_frontend_state.gui_cheat_box_3;
    ptrs->gui_cheat_box_4 = kfx_frontend_state.gui_cheat_box_4;
    ptrs->level_names_data = kfx_frontend_state.level_names_data;
    ptrs->end_level_names_data = kfx_frontend_state.end_level_names_data;
}

static void frontend_pointers_restore(const struct FrontendProcessPointers *ptrs)
{
    kfx_frontend_state.gui_cheat_box_1 = ptrs->gui_cheat_box_1;
    kfx_frontend_state.gui_cheat_box_3 = ptrs->gui_cheat_box_3;
    kfx_frontend_state.gui_cheat_box_4 = ptrs->gui_cheat_box_4;
    kfx_frontend_state.level_names_data = ptrs->level_names_data;
    kfx_frontend_state.end_level_names_data = ptrs->end_level_names_data;
}

TbBool load_frontend_state(TbFileHandle fhandle)
{
    struct FrontendProcessPointers live;
    frontend_pointers_capture(&live);
    TbBool ok = LbFileRead(fhandle, &kfx_frontend_state, sizeof(struct KfxFrontendState)) == sizeof(struct KfxFrontendState);
    frontend_pointers_restore(&live);
    return ok;
}

void reset_frontend_state(void)
{
    memset(&kfx_frontend_state, 0, sizeof(struct KfxFrontendState));
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
        ERRORLOG("Received frontend state with wrong size: %u != %u", (unsigned)len, (unsigned)sizeof(kfx_frontend_state));
        return false;
    }
    struct FrontendProcessPointers live;
    frontend_pointers_capture(&live);
    memcpy(&kfx_frontend_state, data, len);
    frontend_pointers_restore(&live);
    return true;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
