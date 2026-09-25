/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file save_catalogue.h
 *     Save/packet-file catalogue entry type, shared by kfx_game's
 *     game_saves.c (which authors and defines the save-catalogue
 *     mechanism) and kfx_net's packets_misc.c (which declares
 *     struct CatalogueEntry by value while opening/writing packet
 *     replay files).
 * @par Comment:
 *     Physically split out of kfx_game's game_saves.h into this new
 *     low-rank header, same pattern as camera_data.h/packet_data.h/
 *     speech_ref.h/GameTime: packets_misc.c needs the complete type
 *     for a local by-value declaration, not just a pointer, so a
 *     forward declaration isn't enough. See
 *     docs/refactor/stage-13-enforce-and-document.md.
 */
/******************************************************************************/
#ifndef DK_SAVE_CATALOGUE_H
#define DK_SAVE_CATALOGUE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define SAVE_TEXTNAME_LEN        30
#define PLAYER_NAME_LENGTH       64

enum GameLoadStatus {
    GLoad_Failed = 0,
    GLoad_SavedGame,
    GLoad_ContinueGame,
    GLoad_PacketStart,
    GLoad_PacketContinue,
};

#pragma pack(1)

enum CatalogueEntryFlags {
    CEF_InUse       = 0x0001,
};

// file header for game saves. Also used in packet recordings.
// (Remember to bump the version number if the layout of this struct changes!)
#define CATALOGUE_ENTRY_VER 0
struct CatalogueEntry {
    int64_t flags;
    char textname[SAVE_TEXTNAME_LEN];
    LevelNumber level_num;
    char campaign_name[LINEMSG_SIZE];
    char campaign_fname[DISKPATH_SIZE];
    char player_name[PLAYER_NAME_LENGTH];
    int64_t game_ver_major;
    int64_t game_ver_minor;
    int64_t game_ver_release;
    int64_t game_ver_build;
};

#pragma pack()
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
