/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_saves.c
 *     Saved games maintain functions.
 * @par Purpose:
 *     For opening, writing, listing saved games.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     27 Jan 2009 - 25 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "game_saves.h"
#include "vidmode.h"
#include "vidfade.h"
#include "main_game.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"

#include "config.h"
#include "config_campaigns.h"
#include "game_campaign_progress.h" // Phase D, same doc
#include "config_settings.h"
#include "dungeon_stats.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
#include "config_compp.h"
#include "sound_manager.h"
#include "custom_sprites.h"
#include "lens_api.h"
#include "local_camera.h"
#include "sim_feedback.h"
#include "game_callbacks.h"
#include "script_hooks.h"
#include "game_legacy.h"
#include "game_merge.h"
#include "net_exchange_gameplay.h"
#include "packets.h"
#include "lvl_filesdk1.h"
#include "moonphase.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/******************************************************************************/
TbBool load_catalogue_entry(TbFileHandle fh,struct FileChunkHeader *hdr,struct CatalogueEntry *centry);
/******************************************************************************/
const int64_t VersionMajor    = VER_MAJOR;
const int64_t VersionMinor    = VER_MINOR;
int64_t const VersionRelease  = VER_RELEASE;
int64_t const VersionBuild    = VER_BUILD;

const char *continue_game_filename="fx1contn.sav";
const char *saved_game_filename="fx1g%04" PRId64 ".sav";
const char *packet_filename="fx1rp%04" PRId64 ".pck";

/* Dynamically-grown savegame catalogue (see game_saves.h): holds one CatalogueEntry per
 * reachable save slot. It is sized on load to (highest existing slot + 2), min
 * SAVE_SLOTS_MIN, and grown on demand when writing to a higher slot, so the slot count
 * is limited only by disk space. */
struct CatalogueEntry *save_game_catalogue = NULL;
int64_t save_game_catalogue_count = 0;      // logical length (slots the menus range over)
static int64_t save_game_catalogue_capacity = 0;  // number of entries actually allocated

int64_t number_of_saved_games;

/** Grow the catalogue so that index slot is valid; any newly added entries are zeroed
 *  (not in use). Returns false only on allocation failure. */
static TbBool ensure_catalogue_slot(int64_t slot)
{
    if ((slot < 0) || (slot >= SAVE_SLOTS_LIMIT))
        return false;
    if (slot >= save_game_catalogue_capacity)
    {
        int64_t newcap = slot + 1;
        struct CatalogueEntry* p = (struct CatalogueEntry*)realloc(save_game_catalogue,
            newcap * sizeof(struct CatalogueEntry));
        if (p == NULL)
        {
            ERRORLOG("Cannot grow save catalogue to %" PRId64 " entries", (int64_t)(newcap));
            return false;
        }
        memset(&p[save_game_catalogue_capacity], 0,
            (newcap - save_game_catalogue_capacity) * sizeof(struct CatalogueEntry));
        save_game_catalogue = p;
        save_game_catalogue_capacity = newcap;
    }
    if (slot >= save_game_catalogue_count)
        save_game_catalogue_count = slot + 1;
    return true;
}

/** Parse the slot index from a savegame filename "fx1gNNNN.sav" (case-insensitive).
 *  Returns the index, or -1 if the name does not match the expected pattern. */
static int64_t save_slot_index_from_filename(const char *fname)
{
    if (strncasecmp(fname, "fx1g", 4) != 0)
        return -1;
    const char* p = fname + 4;
    if ((*p < '0') || (*p > '9'))
        return -1;
    int64_t idx = LbAtoI32(p);
    while ((*p >= '0') && (*p <= '9'))
        p++;
    if (strcasecmp(p, ".sav") != 0)
        return -1;
    if ((idx < 0) || (idx >= SAVE_SLOTS_LIMIT))
        return -1;
    return (int64_t)idx;
}

#define CONTINUE_GAME_FILE_SIZE (CAMPAIGN_FNAME_LEN + sizeof(LevelNumber) + sizeof(struct IntralevelData))
/******************************************************************************/
TbBool is_primitive_save_version(int64_t filesize)
{
    if (filesize < (char *)&kfx_sim_state.loaded_level_number - (char *)&game)
        return false;
    if (filesize <= 1382437) // sizeof(struct Game) - but it's better to use constant here
        return true;
    return false;
}

TbBool save_game_chunks(TbFileHandle fhandle, struct CatalogueEntry *centry)
{
    struct FileChunkHeader hdr;
    int64_t chunks_done = 0;
    // Currently there is some game data outside of structs - make sure it is updated
    light_export_system_state(&kfx_game_state.lightst);
    { // Info chunk
        hdr.id = SGC_InfoBlock;
        hdr.ver = CATALOGUE_ENTRY_VER;
        hdr.len = sizeof(struct CatalogueEntry);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, centry, sizeof(struct CatalogueEntry)) == sizeof(struct CatalogueEntry))
            chunks_done |= SGF_InfoBlock;
    }
    { // Game data chunk
        hdr.id = SGC_GameOrig;
        hdr.ver = 0;
        hdr.len = sizeof(struct Game);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, &game, sizeof(struct Game)) == sizeof(struct Game))
            chunks_done |= SGF_GameOrig;
    }
    { // KfxSimState data chunk
        hdr.id = SGC_KfxSimState;
        hdr.ver = 0;
        hdr.len = sizeof(struct KfxSimState);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, &kfx_sim_state, sizeof(struct KfxSimState)) == sizeof(struct KfxSimState))
            chunks_done |= SGF_KfxSimState;
    }
    { // KfxNetState data chunk
        hdr.id = SGC_KfxNetState;
        hdr.ver = 0;
        hdr.len = sizeof(struct KfxNetState);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, &kfx_net_state, sizeof(struct KfxNetState)) == sizeof(struct KfxNetState))
            chunks_done |= SGF_KfxNetState;
    }
    { // KfxGameState data chunk
        hdr.id = SGC_KfxGameState;
        hdr.ver = 0;
        hdr.len = sizeof(struct KfxGameState);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, &kfx_game_state, sizeof(struct KfxGameState)) == sizeof(struct KfxGameState))
            chunks_done |= SGF_KfxGameState;
    }
    { // KfxFrontendState data chunk
        hdr.id = SGC_KfxFrontendState;
        hdr.ver = 0;
        hdr.len = game_callbacks->get_frontend_state_size();
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (game_callbacks->save_frontend_state(fhandle))
            chunks_done |= SGF_KfxFrontendState;
    }
    { // IntralevelData data chunk
        hdr.id = SGC_IntralevelData;
        hdr.ver = 0;
        hdr.len = sizeof(struct IntralevelData);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, &intralvl, sizeof(struct IntralevelData)) == sizeof(struct IntralevelData))
            chunks_done |= SGF_IntralevelData;
    }

    // Adding Lua serialized data chunk
    {
        size_t lua_data_len;
        const char* lua_data = script_hooks->lua_get_serialised_data(&lua_data_len);

        hdr.id = SGC_LuaData;
        hdr.ver = 0;
        hdr.len = lua_data_len;
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, lua_data, lua_data_len) == lua_data_len)
            chunks_done |= SGF_LuaData;
        script_hooks->cleanup_serialized_data();
    }

    if (chunks_done != SGF_SavedGame)
        return false;
    return true;
}

TbBool save_packet_chunks(TbFileHandle fhandle,struct CatalogueEntry *centry)
{
    struct FileChunkHeader hdr;
    int64_t chunks_done = 0;
    { // Packet file header
        hdr.id = SGC_PacketHeader;
        hdr.ver = PACKET_SAVE_HEAD_VER;
        hdr.len = sizeof(struct PacketSaveHead);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, &kfx_net_state.packet_save_head, sizeof(struct PacketSaveHead)) == sizeof(struct PacketSaveHead))
            chunks_done |= SGF_PacketHeader;
    }
    { // Info chunk
        hdr.id = SGC_InfoBlock;
        hdr.ver = CATALOGUE_ENTRY_VER;
        hdr.len = sizeof(struct CatalogueEntry);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, centry, sizeof(struct CatalogueEntry)) == sizeof(struct CatalogueEntry))
            chunks_done |= SGF_InfoBlock;
    }
    // If it's not start of a level, save progress data too
    if (get_gameturn() != 0)
    {
        { // Game data chunk
            hdr.id = SGC_GameOrig;
            hdr.ver = 0;
            hdr.len = sizeof(struct Game);
            if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
            if (LbFileWrite(fhandle, &game, sizeof(struct Game)) == sizeof(struct Game))
                chunks_done |= SGF_GameOrig;
        }
        { // KfxSimState data chunk
            hdr.id = SGC_KfxSimState;
            hdr.ver = 0;
            hdr.len = sizeof(struct KfxSimState);
            if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
            if (LbFileWrite(fhandle, &kfx_sim_state, sizeof(struct KfxSimState)) == sizeof(struct KfxSimState))
                chunks_done |= SGF_KfxSimState;
        }
        { // KfxNetState data chunk
            hdr.id = SGC_KfxNetState;
            hdr.ver = 0;
            hdr.len = sizeof(struct KfxNetState);
            if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
            if (LbFileWrite(fhandle, &kfx_net_state, sizeof(struct KfxNetState)) == sizeof(struct KfxNetState))
                chunks_done |= SGF_KfxNetState;
        }
        { // KfxGameState data chunk
            hdr.id = SGC_KfxGameState;
            hdr.ver = 0;
            hdr.len = sizeof(struct KfxGameState);
            if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
            if (LbFileWrite(fhandle, &kfx_game_state, sizeof(struct KfxGameState)) == sizeof(struct KfxGameState))
                chunks_done |= SGF_KfxGameState;
        }
        { // KfxFrontendState data chunk
            hdr.id = SGC_KfxFrontendState;
            hdr.ver = 0;
            hdr.len = game_callbacks->get_frontend_state_size();
            if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
            if (game_callbacks->save_frontend_state(fhandle))
                chunks_done |= SGF_KfxFrontendState;
        }
    }
    { // Packet file data start indicator
        hdr.id = SGC_PacketData;
        hdr.ver = PACKET_VER;
        hdr.len = 0; // unbounded
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
            chunks_done |= SGF_PacketData;
    }
    if ((chunks_done != SGF_PacketStart) && (chunks_done != SGF_PacketContinue))
        return false;
    return true;
}

static TbBool chunk_version_ok(TbFileHandle fhandle, const struct FileChunkHeader *hdr, uint64_t expected)
{
    if (hdr->ver == expected)
        return true;
    WARNLOG("Chunk %04" PRIx64 " is version %" PRIu64 ", expected %" PRIu64 "; skipping it",
        (uint64_t)hdr->id, (uint64_t)hdr->ver, (uint64_t)expected);
    if (LbFileSeek(fhandle, hdr->len, Lb_FILE_SEEK_CURRENT) < 0)
        LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
    return false;
}

int64_t load_game_chunks(TbFileHandle fhandle, struct CatalogueEntry *centry)
{
    int64_t chunks_done = 0;
    while (!LbFileEof(fhandle))
    {
        struct FileChunkHeader hdr;
        if (LbFileRead(fhandle, &hdr, sizeof(struct FileChunkHeader)) != sizeof(struct FileChunkHeader))
            break;
        switch (hdr.id)
        {
        case SGC_InfoBlock:
            if (!chunk_version_ok(fhandle, &hdr, CATALOGUE_ENTRY_VER))
                break;
            if (load_catalogue_entry(fhandle, &hdr, centry))
            {
                chunks_done |= SGF_InfoBlock;
                if (!change_campaign(CampgnT_Default, centry->campaign_fname)) {
                    ERRORLOG("Unable to load campaign");
                    return GLoad_Failed;
                }
                free_level_strings_data();
                struct GameCampaign *campgn = &campaign;
                load_map_string_data(campgn, centry->level_num, get_level_fgroup(centry->level_num));
                // Load configs which may have per-campaign part, and even be modified within a level
                recheck_all_mod_exist();
                init_custom_sprites(centry->level_num);
                load_stats_files();
                game_callbacks->set_high_score_entry(centry->player_name);
            }
            break;
        case SGC_GameOrig:
            if (hdr.len != sizeof(struct Game))
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible GameOrig chunk");
                break;
            }
            if (LbFileRead(fhandle, &game, sizeof(struct Game)) == sizeof(struct Game)) {
                chunks_done |= SGF_GameOrig;
            } else {
                WARNLOG("Could not read GameOrig chunk");
            }
            break;
        case SGC_KfxSimState:
            if (hdr.len != sizeof(struct KfxSimState))
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible KfxSimState chunk");
                break;
            }
            if (LbFileRead(fhandle, &kfx_sim_state, sizeof(struct KfxSimState)) == sizeof(struct KfxSimState)) {
                chunks_done |= SGF_KfxSimState;
            } else {
                WARNLOG("Could not read KfxSimState chunk");
            }
            break;
        case SGC_KfxNetState:
            if (hdr.len != sizeof(struct KfxNetState))
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible KfxNetState chunk");
                break;
            }
            if (LbFileRead(fhandle, &kfx_net_state, sizeof(struct KfxNetState)) == sizeof(struct KfxNetState)) {
                chunks_done |= SGF_KfxNetState;
            } else {
                WARNLOG("Could not read KfxNetState chunk");
            }
            break;
        case SGC_KfxGameState:
            if (hdr.len != sizeof(struct KfxGameState))
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible KfxGameState chunk");
                break;
            }
            if (LbFileRead(fhandle, &kfx_game_state, sizeof(struct KfxGameState)) == sizeof(struct KfxGameState)) {
                chunks_done |= SGF_KfxGameState;
            } else {
                WARNLOG("Could not read KfxGameState chunk");
            }
            break;
        case SGC_KfxFrontendState:
            if (hdr.len != game_callbacks->get_frontend_state_size())
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible KfxFrontendState chunk");
                break;
            }
            if (game_callbacks->load_frontend_state(fhandle)) {
                chunks_done |= SGF_KfxFrontendState;
            } else {
                WARNLOG("Could not read KfxFrontendState chunk");
            }
            break;
        case SGC_PacketHeader:
            if (!chunk_version_ok(fhandle, &hdr, PACKET_SAVE_HEAD_VER))
                break;
            if (hdr.len != sizeof(struct PacketSaveHead))
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible PacketHeader chunk");
                break;
            }
            if (LbFileRead(fhandle, &kfx_net_state.packet_save_head, sizeof(struct PacketSaveHead))
                == sizeof(struct PacketSaveHead)) {
                chunks_done |= SGF_PacketHeader;
            } else {
                WARNLOG("Could not read GameOrig chunk");
            }
            break;
        case SGC_PacketData:
            if (!chunk_version_ok(fhandle, &hdr, PACKET_VER))
                break;
            if (hdr.len != 0)
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible PacketData chunk");
                break;
            }
            chunks_done |= SGF_PacketData;
            if ((chunks_done & SGF_PacketContinue) == SGF_PacketContinue)
                return GLoad_PacketContinue;
            if ((chunks_done & SGF_PacketStart) == SGF_PacketStart)
                return GLoad_PacketStart;
            return GLoad_Failed;
        case SGC_IntralevelData:
            if (hdr.len != sizeof(struct IntralevelData))
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible IntralevelData chunk");
                break;
            }
            if (LbFileRead(fhandle, &intralvl, sizeof(struct IntralevelData)) == sizeof(struct IntralevelData)) {
                chunks_done |= SGF_IntralevelData;
            } else {
                WARNLOG("Could not read IntralevelData chunk");
            }
            break;
        case SGC_LuaData:
            {
                char* lua_data = (char*)malloc(hdr.len);
                if (lua_data == NULL) {
                    WARNLOG("Could not allocate memory for LuaData chunk");
                    break;
                }
                if (LbFileRead(fhandle, lua_data, hdr.len) == hdr.len) {
                    //has to be loaded here as level num only filled while gamestruct loaded, and need it for setting serialised_data
                    script_hooks->open_lua_script(get_loaded_level_number());

                    script_hooks->lua_set_serialised_data(lua_data, hdr.len);
                    chunks_done |= SGF_LuaData;
                } else {
                    WARNLOG("Could not read LuaData chunk");
                    free(lua_data);
                }
            }
            break;
        default:
            WARNLOG("Unrecognized chunk, ID = %08" PRIx64, (uint64_t)(hdr.id));
            if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
            break;
        }
    }
    if ((chunks_done & SGF_SavedGame) == SGF_SavedGame)
    {
        // Update interface items
        config_reload_callbacks->update_trap_tab_to_config();
        config_reload_callbacks->update_room_tab_to_config();
        return GLoad_SavedGame;
    }
    return GLoad_Failed;
}

/**
 * Saves the game state file (savegame).
 * @note fill_game_catalogue_entry() should be called before to fill level information.
 *
 * @param slot_num
 * @return
 */
TbBool save_game(int64_t slot_num)
{
    if (!ensure_catalogue_slot(slot_num))
    {
        ERRORLOG("Outranged slot index %" PRId64,(int64_t)slot_num);
        return false;
    }
    char* fname = prepare_file_fmtpath(FGrp_Save, saved_game_filename, slot_num);
    TbFileHandle handle = LbFileOpen(fname, Lb_FILE_MODE_NEW);
    if (!handle)
    {
        WARNMSG("Cannot open file to save, \"%s\".",fname);
        return false;
    }
    if (!save_game_chunks(handle,&save_game_catalogue[slot_num]))
    {
        LbFileClose(handle);
        WARNMSG("Cannot write to save file, \"%s\".",fname);
        return false;
    }
    LbFileClose(handle);
    script_hooks->api_event("GAME_SAVED");
    return true;
}

TbBool is_save_game_loadable(int64_t slot_num)
{
    // Prepare filename and open the file
    char* fname = prepare_file_fmtpath(FGrp_Save, saved_game_filename, slot_num);
    TbFileHandle fh = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
    if (fh)
    {
        // Let's try to read the file, just to be sure
        struct FileChunkHeader hdr;
        if (LbFileRead(fh, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        {
            LbFileClose(fh);
            return true;
        }
        LbFileClose(fh);
    }
    return false;
}

TbBool load_game(int64_t slot_num)
{
    if (!ensure_catalogue_slot(slot_num))
    {
        ERRORLOG("Outranged slot index %" PRId64,(int64_t)slot_num);
        return false;
    }
    TbFileHandle fh;
//  unsigned char buf[14];
//  char cmpgn_fname[CAMPAIGN_FNAME_LEN];
    SYNCDBG(6,"Starting");
    reset_eye_lenses();
    {
        // Use fname only here - it is overwritten by next use of prepare_file_fmtpath()
        char* fname = prepare_file_fmtpath(FGrp_Save, saved_game_filename, slot_num);
        fh = LbFileOpen(fname,Lb_FILE_MODE_READ_ONLY);
        if (!fh)
        {
          WARNMSG("Cannot open saved game file \"%s\".",fname);
          save_catalogue_slot_disable(slot_num);
          return false;
        }
    }
    int64_t file_len = LbFileLengthHandle(fh);
    if (is_primitive_save_version(file_len))
    {
        {
          LbFileClose(fh);
          save_catalogue_slot_disable(slot_num);
          return false;
        }
    }
    struct CatalogueEntry* centry = &save_game_catalogue[slot_num];

        // Check if the game version is compatible
    if ((centry->game_ver_major != VER_MAJOR) || (centry->game_ver_minor != VER_MINOR) ||
        (centry->game_ver_release != VER_RELEASE) || (centry->game_ver_build != VER_BUILD))
    {
        WARNLOG("loading savegame made in different version %" PRId64 ".%" PRId64 ".%" PRId64 ".%" PRId64 " current %" PRId64 ".%" PRId64 ".%" PRId64 ".%" PRId64,
            (int64_t)centry->game_ver_major, (int64_t)centry->game_ver_minor,
            (int64_t)centry->game_ver_release, (int64_t)centry->game_ver_build,
            (int64_t)(VER_MAJOR), (int64_t)(VER_MINOR), (int64_t)(VER_RELEASE), (int64_t)(VER_BUILD));
    }

    LbFileSeek(fh, 0, Lb_FILE_SEEK_BEGINNING);
    // Here is the actual loading
    if (load_game_chunks(fh,centry) != GLoad_SavedGame)
    {
        LbFileClose(fh);
        if (kfx_sim_state.loaded_level_number == 0)
        {
            kfx_sim_state.loaded_level_number = centry->level_num;
        }
        WARNMSG("Couldn't correctly load saved game in slot %" PRId64 ".",(int64_t)slot_num);
        return false;
    }
    my_player_number = kfx_net_state.local_plyr_idx;
    LbFileClose(fh);
    // Re-apply creature sound overrides: SGC_GameOrig restored kfx_config_state.conf with
    // session-specific negative bank indices from the save; fix them to match
    // the current session's custom bank layout.
    sound_manager_reapply_creature_sounds();
    snprintf(kfx_game_state.campaign_fname, sizeof(kfx_game_state.campaign_fname), "%s", campaign.fname);
    reinit_level_after_load();
    initialize_packet_history();
    clear_packets();
    process_pause_packet(0, 0);
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    clear_flag(kfx_sim_state.operation_flags, GOF_WorldInfluence);
    game_callbacks->close_main_cheat_menu();
    game_callbacks->close_creature_cheat_menu();
    game_callbacks->close_instance_cheat_menu();
    game_callbacks->close_secondary_cheat_menu();
    sim_feedback->play_sound_message(SMsg_GameLoaded, 0);
    config_reload_callbacks->panel_map_update(0, 0, kfx_sim_state.map_subtiles_x+1, kfx_sim_state.map_subtiles_y+1);
    calculate_moon_phase(false,false);
    update_extra_levels_visibility();
    struct PlayerInfo* player = get_my_player();
    struct UserState* ustate = get_user_state(get_local_user());
    clear_flag(ustate->additional_flags, UsrAF_LightningPaletteIsActive);
    clear_flag(ustate->additional_flags, UsrAF_FreezePaletteIsActive);
    local_state.view_type = PVT_None;
    local_state.palette_fade_step_pain = 0;
    local_state.palette_fade_step_possession = 0;
    local_state.lens_palette = 0;
    local_state.minimap_pos_x = 11;
    local_state.minimap_pos_y = 11;
    local_state.minimap_zoom = settings.minimap_zoom;
    local_state.roomspace_size = DEFAULT_USER_ROOMSPACE_WIDTH;
    // Reinitialize lens first (restores lens_palette pointer from config)
    reinitialise_eye_lens(kfx_sim_state.applied_lens_type);
    // Apply the appropriate palette (lens palette if active, otherwise engine default)
    PaletteSetUserPalette(player->user_id, local_state.lens_palette ? local_state.lens_palette : engine_palette);
    init_local_cameras(player);
    // Update the lights system state
    light_import_system_state(&kfx_game_state.lightst);
    // Victory state
    if (player->victory_state != VicS_Undecided)
    {
      game_callbacks->frontstats_initialise();
      struct Dungeon* dungeon = get_players_dungeon(player);
      dungeon->lvstats.player_score = 0;
      dungeon->lvstats.allow_save_score = 1;
    }
    kfx_sim_state.loaded_swipe_idx = -1;
    JUSTMSG("Loaded level %" PRId64 " from %s", (int64_t)(kfx_sim_state.continue_level_number), campaign.name);

    script_hooks->api_event("GAME_LOADED");

    return true;
}

int64_t count_valid_saved_games(void)
{
  number_of_saved_games = 0;
  for (int64_t i = 0; i < save_game_catalogue_count; i++)
  {
      struct CatalogueEntry* centry = &save_game_catalogue[i];
      if ((centry->flags & CEF_InUse) != 0)
          number_of_saved_games++;
  }
  return number_of_saved_games;
}

TbBool fill_game_catalogue_entry(struct CatalogueEntry *centry,const char *textname)
{
    centry->level_num = get_loaded_level_number();
    snprintf(centry->textname, SAVE_TEXTNAME_LEN, "%s", textname);
    snprintf(centry->campaign_name, LINEMSG_SIZE, "%s", campaign.name);
    const char *cmpgn_pfx = "";
    for (int64_t i = 0; i < CampgnT_COUNT; i++) {
        if ((cmpgn_fgroup[i] == campaign.fgroup) && (cmpgn_prefix[i] != NULL)) {
            cmpgn_pfx = cmpgn_prefix[i];
            break;
        }
    }
    snprintf(centry->campaign_fname, DISKPATH_SIZE, "%s%s", cmpgn_pfx, campaign.fname);
    game_callbacks->get_high_score_entry(centry->player_name, PLAYER_NAME_LENGTH);
    set_flag(centry->flags, CEF_InUse);
    centry->game_ver_major = VER_MAJOR;
    centry->game_ver_minor = VER_MINOR;
    centry->game_ver_release = VER_RELEASE;
    centry->game_ver_build = VER_BUILD;
    return true;
}

TbBool fill_game_catalogue_slot(int64_t slot_num,const char *textname)
{
    if (!ensure_catalogue_slot(slot_num))
    {
        ERRORLOG("Outranged slot index %" PRId64,(int64_t)slot_num);
        return false;
    }
    struct CatalogueEntry* centry = &save_game_catalogue[slot_num];
    return fill_game_catalogue_entry(centry,textname);
}

TbBool game_catalogue_slot_disable(struct CatalogueEntry *game_catalg,uint64_t slot_idx)
{
  if (slot_idx >= (uint64_t)save_game_catalogue_count)
    return false;
  clear_flag(game_catalg[slot_idx].flags, CEF_InUse);
  return true;
}

TbBool save_catalogue_slot_disable(uint64_t slot_idx)
{
  return game_catalogue_slot_disable(save_game_catalogue,slot_idx);
}

TbBool load_catalogue_entry(TbFileHandle fh,struct FileChunkHeader *hdr,struct CatalogueEntry *centry)
{
    clear_flag(centry->flags, CEF_InUse);
    if ((hdr->id == SGC_InfoBlock) && (hdr->len == sizeof(struct CatalogueEntry)))
    {
        if (LbFileRead(fh, centry, sizeof(struct CatalogueEntry))
          == sizeof(struct CatalogueEntry))
        {
            set_flag(centry->flags, CEF_InUse);
        }
    }
    centry->textname[SAVE_TEXTNAME_LEN-1] = '\0';
    centry->campaign_name[LINEMSG_SIZE-1] = '\0';
    centry->campaign_fname[DISKPATH_SIZE-1] = '\0';
    centry->player_name[PLAYER_NAME_LENGTH-1] = '\0';
    return ((centry->flags & CEF_InUse) != 0);
}


TbBool load_game_save_catalogue(void)
{
    // Scan the save directory to find the highest existing slot index, so the catalogue
    // can be sized to exactly the saves that exist plus one free slot to save into.
    int64_t highest = -1;
    struct TbFileEntry fe;
    char* spec = prepare_file_path(FGrp_Save, "fx1g*.sav");
    struct TbFileFind* ff = LbFileFindFirst(spec, &fe);
    if (ff != NULL)
    {
        do {
            int64_t idx = save_slot_index_from_filename(fe.Filename);
            if (idx > highest)
                highest = idx;
        } while (LbFileFindNext(ff, &fe) >= 0);
        LbFileFindEnd(ff);
    }
    int64_t needed = highest + 2;               // used slots + one free slot to save into
    if (needed < SAVE_SLOTS_MIN)
        needed = SAVE_SLOTS_MIN;
    if (needed > SAVE_SLOTS_LIMIT)
        needed = SAVE_SLOTS_LIMIT;
    if (!ensure_catalogue_slot(needed - 1))
        return false;
    save_game_catalogue_count = needed;      // logical length the menus range over

    // (Re)load metadata for every slot in range; missing files leave a zeroed (free) entry.
    int64_t saves_found = 0;
    for (int64_t slot_num = 0; slot_num < save_game_catalogue_count; slot_num++)
    {
        struct CatalogueEntry* centry = &save_game_catalogue[slot_num];
        memset(centry, 0, sizeof(struct CatalogueEntry));
        char* fname = prepare_file_fmtpath(FGrp_Save, saved_game_filename, slot_num);
        TbFileHandle fh = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
        if (!fh)
            continue;
        struct FileChunkHeader hdr;
        if (LbFileRead(fh, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        {
            if (load_catalogue_entry(fh,&hdr,centry))
                saves_found++;
        }
        LbFileClose(fh);
    }
    return (saves_found > 0);
}

TbBool initialise_load_game_slots(void)
{
    load_game_save_catalogue();
    return (count_valid_saved_games() > 0);
}

// No longer static: game_campaign_progress.c's reconcile_fx1contn_into_progress()
// (docs/refactor/gui/05-campaign-progress-and-landview.md §3.4) reads the
// same file this way to absorb old progress into save/progress.cfg.
int64_t read_continue_game_progress(char *cmpgn_fname, LevelNumber *lvnum, struct IntralevelData *intralevel)
{
    char* fname = prepare_file_path(FGrp_Save, continue_game_filename);
    int64_t fsize = LbFileLength(fname);
    if (fsize != (int64_t)CONTINUE_GAME_FILE_SIZE)
    {
        SYNCDBG(7, "No correct .SAV file; there's no continue");
        return false;
    }
    TbFileHandle fh = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
    if (!fh)
    {
        SYNCDBG(7,"Can't open .SAV file; there's no continue");
        return false;
    }
    int64_t result = false;
    if (LbFileRead(fh, cmpgn_fname, CAMPAIGN_FNAME_LEN) == CAMPAIGN_FNAME_LEN)
    if (LbFileRead(fh, lvnum, sizeof(*lvnum)) == sizeof(*lvnum))
    if (LbFileRead(fh, intralevel, sizeof(struct IntralevelData)) == sizeof(struct IntralevelData))
        result = true;
    LbFileClose(fh);
    if (!result)
    {
        SYNCDBG(7, "No correct .SAV file; there's no continue");
        return false;
    }
    cmpgn_fname[CAMPAIGN_FNAME_LEN-1] = '\0';
    return true;
}

/**
 * Indicates whether continue game option is available.
 * @return
 */
TbBool continue_game_available(void)
{
    // docs/refactor/gui/05-campaign-progress-and-landview.md §3.4: "Continue"
    // means "go to Campaign Select" (frontend_load_continue_game_resolve()),
    // which needs no specific campaign/level pre-loaded -- it's available
    // the moment *any* campaign has *any* unlocked level, full stop.
    // fx1contn.sav is never written any more; read_continue_game_progress()
    // stays only for reconcile_fx1contn_into_progress()'s one-time migration
    // of an old save into progress.cfg, below.
    if (!load_campaign_progress_file())
        return false;
    reconcile_fx1contn_into_progress();
    return any_campaign_progress_exists();
}

TbBool add_transfered_creature(PlayerNumber plyr_idx, ThingModel model, CrtrExpLevel exp_level, char *name)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon))
    {
        ERRORDBG(11, "Can't transfer creature; player %" PRId64 " has no dungeon.", (int64_t)plyr_idx);
        return false;
    }

    int64_t i = dungeon->creatures_transferred; //makes sure it fits 255 units

    intralvl.transferred_creatures[plyr_idx][i].model = model;
    intralvl.transferred_creatures[plyr_idx][i].exp_level = exp_level;
    strcpy(intralvl.transferred_creatures[plyr_idx][i].creature_name, name);
    return true;
}

void clear_transfered_creatures(void)
{
    for (int64_t p = 0; p < PLAYERS_COUNT; p++)
    {
        for (int64_t i = 0; i < TRANSFER_CREATURE_STORAGE_COUNT; i++)
        {
            intralvl.transferred_creatures[p][i].model = 0;
            intralvl.transferred_creatures[p][i].exp_level = 0;
        }
    }
}

TbBool get_transferred_creature(PlayerNumber plyr_idx, int64_t idx, ThingModel *model, CrtrExpLevel *exp_level, char *name_buf, size_t name_buf_size)
{
    struct CreatureStorage* stored = &intralvl.transferred_creatures[plyr_idx][idx];
    if (stored->model <= 0)
    {
        return false;
    }
    *model = stored->model;
    *exp_level = stored->exp_level;
    snprintf(name_buf, name_buf_size, "%s", stored->creature_name);
    return true;
}

LevelNumber move_campaign_to_next_level(void)
{
    LevelNumber curr_lvnum = get_continue_level_number();
    LevelNumber lvnum = next_singleplayer_level(curr_lvnum, false);
    SYNCDBG(15,"Campaign move %" PRId64 " to %" PRId64,(int64_t)(curr_lvnum),(int64_t)(lvnum));
    {
        struct PlayerInfo* player = get_my_player();
        player->display_flags &= ~PlaF6_PlyrHasQuit;
    }
    if (lvnum != LEVELNUMBER_ERROR)
    {
        curr_lvnum = set_continue_level_number(lvnum);
        SYNCDBG(8,"Continue level moved to %" PRId64 ".",(int64_t)(curr_lvnum));
        return curr_lvnum;
    } else
    {
        curr_lvnum = set_continue_level_number(SINGLEPLAYER_NOTSTARTED);
        SYNCDBG(8,"Continue level moved to NOTSTARTED.");
        return curr_lvnum;
    }
}

LevelNumber move_campaign_to_prev_level(void)
{
    LevelNumber curr_lvnum = get_continue_level_number();
    LevelNumber lvnum = prev_singleplayer_level(curr_lvnum);
    SYNCDBG(15,"Campaign move %" PRId64 " to %" PRId64,(int64_t)(curr_lvnum),(int64_t)(lvnum));
    if (lvnum != LEVELNUMBER_ERROR)
    {
        curr_lvnum = set_continue_level_number(lvnum);
        SYNCDBG(8,"Continue level moved to %" PRId64 ".",(int64_t)(curr_lvnum));
        return curr_lvnum;
    } else
    {
        curr_lvnum = set_continue_level_number(SINGLEPLAYER_FINISHED);
        SYNCDBG(8,"Continue level moved to FINISHED.");
        return curr_lvnum;
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
