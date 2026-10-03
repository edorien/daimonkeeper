/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_saves.h
 *     Header file for game_saves.c.
 * @par Purpose:
 *     Saved games maintain functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     27 Jan 2009 - 25 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_GAMESAVE_H
#define DK_GAMESAVE_H

#include "bflib_basics.h"
#include "globals.h"
#include "save_catalogue.h"

struct IntralevelData; // game_merge.h -- only used here by pointer

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define CAMPAIGN_SAVE_SLOTS_COUNT 8
/* Savegames are individual files fx1gNNNN.sav. The in-memory catalogue
 * (save_game_catalogue) grows dynamically to hold exactly the saves that exist plus
 * one free slot to save into, so the only real limit is disk space, there is no fixed
 * slot count. SAVE_SLOTS_LIMIT is only a sanity ceiling matching the fx1g%04d filename
 * range (0-9999).
 */
#define SAVE_SLOTS_LIMIT         10000
#define SAVE_SLOTS_MIN           8

enum SaveGameChunks {
     SGC_InfoBlock      = 0x4F464E49, //"INFO"
     SGC_GameOrig       = 0x53444C4F, //"OLDS"
     SGC_PacketHeader   = 0x52444850, //"PHDR"
     SGC_PacketData     = 0x544B4350, //"PCKT"
     SGC_IntralevelData = 0x4C564C49, //"ILVL"
     SGC_LuaData        = 0x2041554C, //"LUA "
     SGC_KfxSimState    = 0x4D49534B, //"KSIM"
     SGC_KfxNetState    = 0x54454E4B, //"KNET"
     SGC_KfxGameState   = 0x4D41474B, //"KGAM"
     SGC_KfxFrontendState = 0x4F52464B, //"KFRO"
     SGC_AgentMemory    = 0x544E4741, //"AGNT" optional: External seat agents' memory (agent_memory.h)
     SGC_AriadneState   = 0x41495241, //"ARIA" the navigation mesh (ariadne_saved_state.h)
     SGC_KfxConfigState = 0x4746434B, //"KCFG" kfx_config_state's saved part (KFX_CONFIG_STATE_SAVED_*)
     SGC_Product        = 0x444F5250  //"PROD" which game wrote the file (struct ProductChunk)
};

enum SaveGameChunkFlags {
     SGF_InfoBlock      = 0x0001,
     SGF_GameOrig       = 0x0002,
     SGF_Product        = 0x0004,
     SGF_PacketHeader   = 0x0100,
     SGF_PacketData     = 0x0200,
     SGF_IntralevelData = 0x0400,
     SGF_LuaData        = 0x0800,
     SGF_KfxSimState    = 0x1000,
     SGF_KfxNetState    = 0x2000,
     SGF_KfxGameState   = 0x4000,
     SGF_KfxFrontendState = 0x8000,
     SGF_AriadneState   = 0x10000,
     SGF_KfxConfigState = 0x20000,
};
#define SGF_SavedGame      (SGF_InfoBlock|SGF_Product|SGF_GameOrig|SGF_KfxSimState|SGF_KfxNetState|SGF_KfxGameState|SGF_KfxFrontendState|SGF_AriadneState|SGF_KfxConfigState|SGF_IntralevelData|SGF_LuaData)
#define SGF_PacketStart    (SGF_PacketHeader|SGF_PacketData|SGF_InfoBlock|SGF_Product)
#define SGF_PacketContinue (SGF_PacketHeader|SGF_PacketData|SGF_InfoBlock|SGF_Product|SGF_GameOrig|SGF_KfxSimState|SGF_KfxNetState|SGF_KfxGameState|SGF_KfxFrontendState|SGF_AriadneState|SGF_KfxConfigState)

/* enum GameLoadStatus, enum CatalogueEntryFlags, struct CatalogueEntry
   moved to save_catalogue.h (kfx_net, stage 13.3) -- kfx_net's
   packets_misc.c needs struct CatalogueEntry by value, not just a
   pointer, so a forward declaration wasn't enough. */
/******************************************************************************/
#pragma pack(1)

struct Game;

struct FileChunkHeader {
    uint64_t len;
    uint64_t id;
    uint64_t ver;
};

/* SGC_Product payload, written right after the INFO chunk of every save and
   replay. KeeperFX files (and this game's before 1.0.0) have no such chunk,
   so validate_save_chunks() refuses them by name instead of by layout. */
#define PRODUCT_CHUNK_VER 1
struct ProductChunk {
    uint64_t magic;     // PRODUCT_MAGIC (version.h)
    char slug[32];      // PRODUCT_SLUG, for anyone reading the file by hand
};

/******************************************************************************/
extern int64_t number_of_saved_games;
extern const char* continue_game_filename;

#pragma pack()
/******************************************************************************/
extern const int64_t VersionMajor;
extern const int64_t VersionMinor;
extern int64_t const VersionRelease;
extern int64_t const VersionBuild;
extern struct CatalogueEntry *save_game_catalogue;
extern int64_t save_game_catalogue_count;
/******************************************************************************/
int64_t load_game_chunks(TbFileHandle fhandle,struct CatalogueEntry *centry);
/* Refactor pass 2, S09: checks every chunk's version and size against
   state_versions.h without loading anything; load_game_chunks() runs it
   first and refuses the whole file if it fails. */
TbBool validate_save_chunks(TbFileHandle fhandle);
/* Whether the last load_game()/load_game_chunks() refused the file for
   being from another layout (the game was left untouched), and why. */
TbBool last_save_was_refused(void);
const char *last_save_refusal_reason(void);
TbBool fill_game_catalogue_entry(struct CatalogueEntry *centry,const char *textname);
TbBool save_game_chunks(TbFileHandle fhandle,struct CatalogueEntry *centry);
TbBool save_packet_chunks(TbFileHandle fhandle,struct CatalogueEntry *centry);
/******************************************************************************/
TbBool load_game(int64_t slot_idx);
TbBool save_game(int64_t slot_idx);
TbBool initialise_load_game_slots(void);
int64_t count_valid_saved_games(void);
TbBool is_save_game_loadable(int64_t slot_num);
/******************************************************************************/
TbBool save_catalogue_slot_disable(uint64_t slot_idx);
TbBool load_game_save_catalogue(void);
TbBool fill_game_catalogue_slot(int64_t slot_num,const char *textname);
/******************************************************************************/
TbBool add_transfered_creature(PlayerNumber plyr_idx, ThingModel model, CrtrExpLevel exp_level, const char *name);
void clear_transfered_creatures(void);
TbBool get_transferred_creature(PlayerNumber plyr_idx, int64_t idx, ThingModel *model, CrtrExpLevel *exp_level, char *name_buf, size_t name_buf_size);
/******************************************************************************/
LevelNumber move_campaign_to_next_level(void);
LevelNumber move_campaign_to_prev_level(void);
/******************************************************************************/
TbBool continue_game_available(void);
// Exposed for game_campaign_progress.c's reconcile_fx1contn_into_progress()
// -- see that function's own comment.
int64_t read_continue_game_progress(char *cmpgn_fname, LevelNumber *lvnum, struct IntralevelData *intralevel);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
