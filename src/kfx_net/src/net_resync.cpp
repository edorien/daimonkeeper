/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_resync.cpp
 *     Network resynchronization for Dungeon Keeper multiplayer.
 * @par Purpose:
 *     Network resynchronization routines for multiplayer games.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     31 Oct 2025
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "ariadne_saved_state.h"
#include "net_resync.h"
#include "bflib_datetm.h"
#include "net_exchange_gameplay.h"
#include "net_main.h"
#include <zlib.h>
#include "globals.h"
#include "player_data.h"
#include "net_game.h"
#include "lens_api.h"
#include "net_input_lag.h"
#include "net_checksums.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "light_registry.h"
#include "kfx_config_state.h"
#include "sound_manager.h"
#include "state_versions.h"
#include "ports/script_port.h"
#include "ports/ui_port.h"
#include "ports/game_port.h"
#include "local_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Boing {
  unsigned char active_panel_menu_index;
  unsigned char comp_player_aggressive;
  unsigned char comp_player_defensive;
  unsigned char comp_player_construct;
  unsigned char comp_player_creatrsonly;
  unsigned char creatures_tend_imprison;
  unsigned char creatures_tend_flee;
  int64_t hand_over_subtile_x;
  int64_t hand_over_subtile_y;
  uint64_t chosen_room_kind;
  uint64_t chosen_room_spridx;
  uint64_t chosen_room_tooltip;
  uint64_t chosen_spell_type;
  uint64_t chosen_spell_spridx;
  uint64_t chosen_spell_tooltip;
  uint64_t manufactr_element;
  uint64_t manufactr_spridx;
  uint64_t manufactr_tooltip;
};

static struct Boing boing;

TbBool detailed_multiplayer_logging = false;

#define RESYNC_RECEIVE_TIMEOUT_MS 30000

struct ResyncHeader {
    unsigned char message_type;
    uint64_t compressed_length;
    uint64_t original_length;
    uint64_t data_checksum;
};


// function to intentionally desync the game state for testing purposes
void intentional_desync() {
    if (!is_my_player_number(0)) {
        return;
    }
    struct Room* start_rooms = &kfx_sim_state.rooms[1];
    struct Room* end_rooms = &kfx_sim_state.rooms[ROOMS_COUNT];

    for (struct Room* room = start_rooms; room < end_rooms; room += 1) {
        if (room_exists(room)) {
            room->slabs_count += 1;
            break;
        }
    }
    int64_t i = kfx_sim_state.thing_lists[TngList_Creatures].index;
    if (i != 0) {
        struct Thing* thing = thing_get(i);
        if (!thing_is_invalid(thing)) {
            thing->health += 1;
        }
    }
    get_player(0)->instance_remain_turns += 1;
}

void animate_resync_progress_bar(int64_t current_phase, int64_t total_phases) {
    if (get_gameturn() == 0) {
        return;
    }
    if ((kfx_sim_state.operation_flags & GOF_Paused) != 0) {
        return;
    }
    const int64_t max_progress = (int64_t)(32 * units_per_pixel / 16);
    const int64_t progress_pixels = (int64_t)((double)max_progress * current_phase / total_phases);
    ui_draw_out_of_sync_box(progress_pixels, max_progress, ui_get_status_panel_width());
}

void store_localised_game_structure(void) {
    boing.active_panel_menu_index = kfx_sim_state.active_panel_mnu_idx;
    boing.comp_player_aggressive = kfx_net_state.comp_player_aggressive;
    boing.comp_player_defensive = kfx_net_state.comp_player_defensive;
    boing.comp_player_construct = kfx_net_state.comp_player_construct;
    boing.comp_player_creatrsonly = kfx_net_state.comp_player_creatrsonly;
    boing.creatures_tend_imprison = kfx_sim_state.creatures_tend_imprison;
    boing.creatures_tend_flee = kfx_sim_state.creatures_tend_flee;
    boing.hand_over_subtile_x = kfx_sim_state.hand_over_subtile_x;
    boing.hand_over_subtile_y = kfx_sim_state.hand_over_subtile_y;
    boing.chosen_room_kind = kfx_sim_state.chosen_room_kind;
    boing.chosen_room_spridx = kfx_sim_state.chosen_room_spridx;
    boing.chosen_room_tooltip = kfx_sim_state.chosen_room_tooltip;
    boing.chosen_spell_type = kfx_sim_state.chosen_spell_type;
    boing.chosen_spell_spridx = kfx_sim_state.chosen_spell_spridx;
    boing.chosen_spell_tooltip = kfx_sim_state.chosen_spell_tooltip;
    boing.manufactr_element = kfx_sim_state.manufactr_element;
    boing.manufactr_spridx = kfx_sim_state.manufactr_spridx;
    boing.manufactr_tooltip = kfx_sim_state.manufactr_tooltip;
}

void recall_localised_game_structure(void) {
    kfx_sim_state.active_panel_mnu_idx = boing.active_panel_menu_index;
    kfx_net_state.comp_player_aggressive = boing.comp_player_aggressive;
    kfx_net_state.comp_player_defensive = boing.comp_player_defensive;
    kfx_net_state.comp_player_construct = boing.comp_player_construct;
    kfx_net_state.comp_player_creatrsonly = boing.comp_player_creatrsonly;
    kfx_sim_state.creatures_tend_imprison = boing.creatures_tend_imprison;
    kfx_sim_state.creatures_tend_flee = boing.creatures_tend_flee;
    kfx_sim_state.hand_over_subtile_x = boing.hand_over_subtile_x;
    kfx_sim_state.hand_over_subtile_y = boing.hand_over_subtile_y;
    kfx_sim_state.chosen_room_kind = boing.chosen_room_kind;
    kfx_sim_state.chosen_room_spridx = boing.chosen_room_spridx;
    kfx_sim_state.chosen_room_tooltip = boing.chosen_room_tooltip;
    kfx_sim_state.chosen_spell_type = boing.chosen_spell_type;
    kfx_sim_state.chosen_spell_spridx = boing.chosen_spell_spridx;
    kfx_sim_state.chosen_spell_tooltip = boing.chosen_spell_tooltip;
    kfx_sim_state.manufactr_element = boing.manufactr_element;
    kfx_sim_state.manufactr_spridx = boing.manufactr_spridx;
    kfx_sim_state.manufactr_tooltip = boing.manufactr_tooltip;
}

static TbBool send_resync_data(const void * buffer, size_t total_length)
{
    if (total_length > UINT32_MAX) {
        ERRORLOG("Resync data too large");
        return false;
    }

    uLongf compressed_size = compressBound(total_length);
    if (compressed_size > UINT32_MAX - sizeof(ResyncHeader)) {
        ERRORLOG("Compressed resync data too large");
        return false;
    }

    size_t message_size = sizeof(ResyncHeader) + compressed_size;
    char * message_buffer = (char *) malloc(message_size);
    if (message_buffer == NULL) {
        ERRORLOG("Failed to allocate message buffer");
        return false;
    }

    int64_t compress_result = compress((Bytef *)(message_buffer + sizeof(ResyncHeader)), &compressed_size, (const Bytef *)buffer, total_length);
    if (compress_result != Z_OK) {
        ERRORLOG("Compression failed: zlib error %" PRId64, (int64_t)(compress_result));
        free(message_buffer);
        return false;
    }

    uLong data_crc = crc32(0, Z_NULL, 0);
    data_crc = crc32(data_crc, (const Bytef *)buffer, total_length);
    NETLOG("Compression successful: %" PRIu64 " -> %" PRIu64 " bytes", (uint64_t)total_length, (uint64_t)compressed_size);

    ResyncHeader header;
    memset(&header, 0, sizeof(header));
    header.message_type = NETMSG_RESYNC_DATA;
    header.compressed_length = (uint64_t)compressed_size;
    header.original_length = (uint64_t)total_length;
    header.data_checksum = (uint64_t)data_crc;

    message_size = sizeof(ResyncHeader) + compressed_size;
    memcpy(message_buffer, &header, sizeof(ResyncHeader));

    NETLOG("Host: Sending resync data to all clients");
    for (NetUserId user_index = 0; user_index < MAX_NET_USERS; ++user_index) {
        if (netstate.users[user_index].progress != USER_LOGGEDIN) {
            continue;
        }
        netstate.sp->sendmsg_single(netstate.users[user_index].id, message_buffer, message_size);
    }

    free(message_buffer);
    return true;
}

static TbBool receive_resync_data(char ** data_buffer, size_t * data_length)
{
    NETLOG("Starting to receive resync data");

    TbClockMSec start_time = LbTimerClock();
    while (LbTimerClock() - start_time < RESYNC_RECEIVE_TIMEOUT_MS) {
        netstate.sp->update(OnNewUser);
        size_t received_size = netstate.sp->msgready(SERVER_ID, 0);
        if (received_size == 0) {
            continue;
        }

        char * message_buffer = (char *) malloc(received_size);
        if (message_buffer == NULL) {
            ERRORLOG("Failed to allocate message buffer");
            return false;
        }

        received_size = netstate.sp->readmsg(SERVER_ID, message_buffer, received_size);
        if (received_size < sizeof(ResyncHeader)) {
            if (received_size > 0 && message_buffer[0] == NETMSG_RESYNC_DATA) {
                ERRORLOG("Received incomplete resync data: %" PRIu64 " bytes", (uint64_t)received_size);
            } else {
                ERRORLOG("Ignoring message too small for resync data: %" PRIu64 " bytes", (uint64_t)received_size);
            }
            free(message_buffer);
            continue;
        }

        ResyncHeader header;
        memcpy(&header, message_buffer, sizeof(ResyncHeader));

        if (header.message_type != NETMSG_RESYNC_DATA) {
            MULTIPLAYER_LOG("Received wrong message type: %" PRId64, (int64_t)(header.message_type));
            free(message_buffer);
            continue;
        }

        if (header.compressed_length != received_size - sizeof(ResyncHeader)) {
            ERRORLOG("Received message size mismatch: %" PRIu64 " != %" PRIu64,
                (uint64_t)(received_size - sizeof(ResyncHeader)), (uint64_t)(header.compressed_length));
            free(message_buffer);
            continue;
        }
        if (*data_length != 0 && header.original_length != *data_length) {
            ERRORLOG("Received data with wrong size: %" PRIu64 " != %" PRIu64, (uint64_t)(header.original_length), (uint64_t)*data_length);
            free(message_buffer);
            continue;
        }

        size_t output_size = header.original_length;
        if (output_size == 0) {
            output_size = 1;
        }
        char * output_buffer = (char *) malloc(output_size);
        if (output_buffer == NULL) {
            ERRORLOG("Failed to allocate resync destination buffer");
            free(message_buffer);
            return false;
        }

        uLongf dest_len = output_size;

        MULTIPLAYER_LOG("Client: Received resync message, decompressing %" PRIu64 " bytes", (uint64_t)(header.compressed_length));

        int64_t uncompress_result = uncompress((Bytef *)output_buffer, &dest_len,
            (const Bytef *)(message_buffer + sizeof(ResyncHeader)), header.compressed_length);
        if (uncompress_result != Z_OK || dest_len != header.original_length) {
            ERRORLOG("Decompression failed: zlib error %" PRId64 ", expected %" PRIu64 " bytes, got %" PRIu64 " bytes",
                (int64_t)(uncompress_result), (uint64_t)(header.original_length), (uint64_t)dest_len);
            free(output_buffer);
            free(message_buffer);
            return false;
        }

        uLong verify_crc = crc32(0, Z_NULL, 0);
        verify_crc = crc32(verify_crc, (const Bytef *)output_buffer, header.original_length);
        if ((uint64_t)verify_crc != header.data_checksum) {
            ERRORLOG("Resync data checksum mismatch");
            free(output_buffer);
            free(message_buffer);
            return false;
        }

        *data_buffer = output_buffer;
        *data_length = header.original_length;
        free(message_buffer);
        NETLOG("Client: Resync data received successfully");
        return true;
    }

    ERRORLOG("Client: Timeout waiting for resync data after %" PRId64 "ms", (int64_t)(RESYNC_RECEIVE_TIMEOUT_MS));
    return false;
}

TbBool LbNetwork_Resync(void * data_buffer, size_t buffer_length)
{
    MULTIPLAYER_LOG("Starting resync, my_id=%" PRId64 ", buffer_length=%" PRIu64, (int64_t)(netstate.my_id), (uint64_t)buffer_length);

    TbBool result;
    if (network_is_host()) {
        MULTIPLAYER_LOG("Resync: I am the server");
        result = send_resync_data(data_buffer, buffer_length);
    } else {
        MULTIPLAYER_LOG("Resync: I am a client, receiving");
        char * received_data = NULL;
        size_t received_length = buffer_length;
        result = receive_resync_data(&received_data, &received_length);
        if (result) {
            memcpy(data_buffer, received_data, buffer_length);
        }
        free(received_data);
    }

    if (!result) {
        ERRORLOG("Resync FAILED");
        return false;
    }

    MULTIPLAYER_LOG("Resync data transfer completed successfully");
    return true;
}

/*
 * Layout versions of every raw blob in a full resync (state_versions.h), sent
 * first and checked before anything else is read (refactor pass 2, S09).
 * Multiplayer already requires matching builds; this makes a mismatch fail
 * cleanly instead of copying a struct of another layout.
 */
struct ResyncVersions {
    uint64_t game_orig;
    uint64_t sim_state;
    uint64_t net_state;
    uint64_t game_state;
    uint64_t frontend_state;
    uint64_t ariadne_state;
    uint64_t config_state;
};

static struct ResyncVersions current_resync_versions(void)
{
    struct ResyncVersions v;
    v.game_orig = KFX_GAME_ORIG_VER;
    v.sim_state = KFX_SIM_STATE_VER;
    v.net_state = KFX_NET_STATE_VER;
    v.game_state = KFX_GAME_STATE_VER;
    v.frontend_state = KFX_FRONTEND_STATE_VER;
    v.ariadne_state = KFX_ARIADNE_STATE_VER;
    v.config_state = KFX_CONFIG_STATE_VER;
    return v;
}

TbBool send_resync_game(void)
{
    pack_desync_history_for_resync();
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    animate_resync_progress_bar(0, 6);
    NETLOG("Initiating re-synchronization of network game");

    size_t lua_data_len = 0;
    const char * lua_data = script_lua_resync_export(&lua_data_len);
    if (lua_data == NULL) {
        script_cleanup_serialized_data();
        return false;
    }

    // game/kfx_game_state (kfx_game) and kfx_frontend_state (kfx_frontend)
    // are both above kfx_net -- exported as opaque (pointer, length) blobs
    // via GamePort/UiPort (same reasoning/shape as the Lua pair above) rather
    // than #include'd and memcpy'd directly. See
    // docs/refactor/todo/remove-remaining-layering-violations.md.
    size_t game_state_len = 0;
    const char * game_state_data = game_resync_export_game_state(&game_state_len);
    size_t frontend_state_len = 0;
    const char * frontend_state_data = ui_resync_export_frontend_state(&frontend_state_len);
    if (game_state_len > UINT32_MAX || frontend_state_len > UINT32_MAX) {
        ERRORLOG("Full resync data too large");
        script_cleanup_serialized_data();
        return false;
    }

    // The lights travel inside kfx_sim_state (light_registry); kfx_render's
    // shading (lish) isn't sent -- the receiver rebuilds it (refactor pass
    // 2, S11). game_state/frontend_state are each prefixed with their own u32
    // length now that they're opaque blobs of a size this file can't
    // sizeof() directly -- same framing lua_data already used below.
    // Ariadne's navigation mesh follows kfx_net_state: it isn't rebuilt from the map on the receiving end, which
    // would give a different mesh (refactor pass 4, P4-F7; ariadne_saved_state.h). Then kfx_config_state's saved
    // part: the payday progress, the slabs' texture packs, the configuration as the level's script changed it
    // (P4-F16; what upstream's struct Game held).
    const size_t ariadne_state_len = ariadne_saved_state_size();
    size_t fixed_state_size = sizeof(struct ResyncVersions) + sizeof(uint64_t) + game_state_len + sizeof(kfx_sim_state) + sizeof(kfx_net_state)
        + ariadne_state_len + KFX_CONFIG_STATE_SAVED_LEN + sizeof(uint64_t) + frontend_state_len;
    size_t lua_data_offset = fixed_state_size + sizeof(uint64_t);
    if (lua_data_len > UINT32_MAX - lua_data_offset) {
        ERRORLOG("Full resync data too large");
        script_cleanup_serialized_data();
        return false;
    }

    size_t full_resync_len = lua_data_offset + lua_data_len;
    char * full_resync_data = (char *) malloc(full_resync_len);
    if (full_resync_data == NULL) {
        ERRORLOG("Failed to allocate full resync buffer");
        script_cleanup_serialized_data();
        return false;
    }

    uint64_t game_state_len32 = (uint64_t)game_state_len;
    uint64_t frontend_state_len32 = (uint64_t)frontend_state_len;
    uint64_t lua_data_len32 = (uint64_t)lua_data_len;
    char * write_ptr = full_resync_data;
    const struct ResyncVersions versions = current_resync_versions();
    memcpy(write_ptr, &versions, sizeof(versions)); write_ptr += sizeof(versions);
    memcpy(write_ptr, &game_state_len32, sizeof(game_state_len32)); write_ptr += sizeof(game_state_len32);
    memcpy(write_ptr, game_state_data, game_state_len); write_ptr += game_state_len;
    memcpy(write_ptr, &kfx_sim_state, sizeof(kfx_sim_state)); write_ptr += sizeof(kfx_sim_state);
    memcpy(write_ptr, &kfx_net_state, sizeof(kfx_net_state)); write_ptr += sizeof(kfx_net_state);
    ariadne_saved_state_write(write_ptr); write_ptr += ariadne_state_len;
    memcpy(write_ptr, KFX_CONFIG_STATE_SAVED_PTR, KFX_CONFIG_STATE_SAVED_LEN); write_ptr += KFX_CONFIG_STATE_SAVED_LEN;
    memcpy(write_ptr, &frontend_state_len32, sizeof(frontend_state_len32)); write_ptr += sizeof(frontend_state_len32);
    memcpy(write_ptr, frontend_state_data, frontend_state_len); write_ptr += frontend_state_len;
    memcpy(write_ptr, &lua_data_len32, sizeof(lua_data_len32)); write_ptr += sizeof(lua_data_len32);
    memcpy(full_resync_data + lua_data_offset, lua_data, lua_data_len);
    TbBool result = send_resync_data(full_resync_data, full_resync_len);
    free(full_resync_data);
    script_cleanup_serialized_data();
    if (!result) {
        return false;
    }
    animate_resync_progress_bar(2, 6);
    animate_resync_progress_bar(6, 6);
    NETLOG("Host: Resync complete");
    return true;
}

TbBool receive_resync_game(void)
{
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    animate_resync_progress_bar(0, 6);
    NETLOG("Initiating re-synchronization of network game");
    char * full_resync_data = NULL;
    size_t full_resync_len = 0;

    if (!receive_resync_data(&full_resync_data, &full_resync_len)) {
        return false;
    }

    // Two-phase, same discipline the original fixed-offset version used
    // (ports/script_port.def's comment on the Lua pair explains why): parse
    // and validate every length/pointer *without* mutating any local
    // state first, so a failure at any point below leaves game/
    // kfx_sim_state/kfx_net_state/kfx_game_state/kfx_frontend_state
    // all untouched -- only once every import has actually succeeded do
    // the raw memcpy()s for the fields that don't go through a callback
    // (kfx_sim_state/kfx_net_state) happen, right at the end.
    const char * const data_end = full_resync_data + full_resync_len;
    const char * read_ptr = full_resync_data;
    struct ResyncVersions versions;
    const struct ResyncVersions expected = current_resync_versions();
    if (full_resync_len < sizeof(versions)) {
        ERRORLOG("Full resync data too small for its version header: %" PRIu64 " bytes", (uint64_t)full_resync_len);
        free(full_resync_data);
        return false;
    }
    memcpy(&versions, read_ptr, sizeof(versions)); read_ptr += sizeof(versions);
    if (memcmp(&versions, &expected, sizeof(versions)) != 0) {
        ERRORLOG("Resync data is from a build with other state layouts (sim %" PRIu64 "/%" PRIu64 ", net %" PRIu64 "/%" PRIu64
            ", game %" PRIu64 "/%" PRIu64 ", frontend %" PRIu64 "/%" PRIu64 ", orig %" PRIu64 "/%" PRIu64 ", ariadne %" PRIu64 "/%" PRIu64
            ", config %" PRIu64 "/%" PRIu64 "; theirs/ours)",
            versions.sim_state, expected.sim_state, versions.net_state, expected.net_state,
            versions.game_state, expected.game_state, versions.frontend_state, expected.frontend_state,
            versions.game_orig, expected.game_orig, versions.ariadne_state, expected.ariadne_state,
            versions.config_state, expected.config_state);
        free(full_resync_data);
        return false;
    }
    full_resync_len -= sizeof(versions);
    const size_t ariadne_state_len = ariadne_saved_state_size();
    size_t min_header_size = sizeof(uint64_t) + sizeof(kfx_sim_state) + sizeof(kfx_net_state) + ariadne_state_len + KFX_CONFIG_STATE_SAVED_LEN + sizeof(uint64_t) + sizeof(uint64_t);
    if (full_resync_len < min_header_size) {
        ERRORLOG("Full resync data too small: %" PRIu64 " bytes", (uint64_t)full_resync_len);
        free(full_resync_data);
        return false;
    }

    uint64_t game_state_len = 0;
    memcpy(&game_state_len, read_ptr, sizeof(game_state_len)); read_ptr += sizeof(game_state_len);
    if ((size_t)(data_end - read_ptr) < (size_t)game_state_len + sizeof(kfx_sim_state) + sizeof(kfx_net_state) + ariadne_state_len + KFX_CONFIG_STATE_SAVED_LEN + sizeof(uint64_t) + sizeof(uint64_t)) {
        ERRORLOG("Full resync data truncated (game state)");
        free(full_resync_data);
        return false;
    }
    const char * game_state_data = read_ptr; read_ptr += game_state_len;

    const char * kfx_sim_state_data = read_ptr; read_ptr += sizeof(kfx_sim_state);
    const char * kfx_net_state_data = read_ptr; read_ptr += sizeof(kfx_net_state);
    const char * ariadne_state_data = read_ptr; read_ptr += ariadne_state_len;
    const char * config_state_data = read_ptr; read_ptr += KFX_CONFIG_STATE_SAVED_LEN;

    uint64_t frontend_state_len = 0;
    memcpy(&frontend_state_len, read_ptr, sizeof(frontend_state_len)); read_ptr += sizeof(frontend_state_len);
    if ((size_t)(data_end - read_ptr) < (size_t)frontend_state_len + sizeof(uint64_t)) {
        ERRORLOG("Full resync data truncated (frontend state)");
        free(full_resync_data);
        return false;
    }
    const char * frontend_state_data = read_ptr; read_ptr += frontend_state_len;

    uint64_t lua_data_len = 0;
    memcpy(&lua_data_len, read_ptr, sizeof(lua_data_len)); read_ptr += sizeof(lua_data_len);
    if ((size_t)(data_end - read_ptr) != lua_data_len) {
        ERRORLOG("Received lua data with wrong size: %" PRIu64 " != %" PRIu64, (uint64_t)(lua_data_len), (uint64_t)(data_end - read_ptr));
        free(full_resync_data);
        return false;
    }
    const char * lua_data = read_ptr;

    if (!game_resync_import_game_state(game_state_data, game_state_len)) {
        free(full_resync_data);
        return false;
    }
    if (!ui_resync_import_frontend_state(frontend_state_data, frontend_state_len)) {
        free(full_resync_data);
        return false;
    }
    if (!script_lua_resync_import(lua_data, lua_data_len)) {
        free(full_resync_data);
        return false;
    }

    memcpy(&kfx_sim_state, kfx_sim_state_data, sizeof(kfx_sim_state));
    memcpy(&kfx_net_state, kfx_net_state_data, sizeof(kfx_net_state));
    ariadne_saved_state_read(ariadne_state_data);
    memcpy(KFX_CONFIG_STATE_SAVED_PTR, config_state_data, KFX_CONFIG_STATE_SAVED_LEN);
    // the host's creature sound indices are its session's: this machine's custom sound banks may be laid out
    // differently (as after a load)
    sound_manager_reapply_creature_sounds();
    light_registry_invalidate_shading();
    free(full_resync_data);

    animate_resync_progress_bar(2, 6);
    animate_resync_progress_bar(6, 6);
    NETLOG("Client: Resync complete");

    compare_desync_history_from_host();

    return true;
}


void resync_game(void)
{
    SYNCDBG(2,"Starting");
    ui_draw_out_of_sync_box(0, 32*units_per_pixel/16, local_state.engine_window_x);
    // The eye lens (the possession view's effect) is this machine's: keep which lens it was, and set the lens system
    // up again after the resync, as loading a game does (refactor pass 4, P4-F13: it was shut down here and never
    // set up again, so lenses stopped working for the rest of the game on every machine).
    const int64_t local_lens = kfx_sim_state.applied_lens_type;
    reset_eye_lenses();
    store_localised_game_structure();
    TbBool result;
    if (network_is_host()) {
        result = send_resync_game();
    } else {
        result = receive_resync_game();
    }
    if (!result) {
        recall_localised_game_structure();
        reinitialise_eye_lens(local_lens);
        return;
    }
    if (script_lua_script_active()) {
        script_lua_set_random_seed(kfx_sim_state.action_random_seed);
    }
    recall_localised_game_structure();
    game_reinit_level_after_load();
    reinitialise_eye_lens(local_lens);

    kfx_net_state.skip_initial_input_turns = calculate_skip_input();
    initialize_packet_history();
    NETLOG("Input lag after resync: %" PRId64 " turns", (int64_t)(kfx_net_state.input_lag_turns));

    clear_flag(kfx_sim_state.system_flags, GSF_NetGameNoSync);
    clear_flag(kfx_sim_state.system_flags, GSF_NetSeedNoSync);
}

#ifdef __cplusplus
}
#endif
