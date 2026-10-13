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
#include "game_lifecycle.h"
#include "bflib_datetm.h"
#include "bflib_enet.h"
#include "net_exchange_gameplay.h"
#include "net_exchange_common.h"
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
#include "ariadne_update.h"
#include "kfx_config_state.h"
#include "sound_manager.h"
#include "state_versions.h"
#include "state_validate.h"
#include "config_state_validate.h"
#include "ports/script_port.h"
#include "ports/ui_port.h"
#include "ports/game_port.h"
#include "local_state.h"
#include "kfx_local_view.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif


TbBool detailed_multiplayer_logging = false;

#define RESYNC_RECEIVE_TIMEOUT_MS 30000

struct ResyncHeader {
    unsigned char message_type;
    uint64_t compressed_length;
    uint64_t original_length;
    uint64_t data_checksum;
    uint32_t gameplay_generation;
};

// function to intentionally desync the game state for testing purposes
void intentional_desync()
{
    if (!network_is_active() || network_is_host()) {
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

// A resync message: a ResyncHeader, then the zlib-compressed data. Built once, so the host can both
// send it and record it in a replay (upstream #5376).
static char *encode_resync_message(const void * buffer, size_t total_length, size_t *message_size_out)
{
    TbClockMSec start_time = LbTimerClock();
    if (total_length > UINT32_MAX) {
        ERRORLOG("Resync data too large");
        return NULL;
    }

    uLongf compressed_size = compressBound(total_length);
    if (compressed_size > UINT32_MAX - sizeof(ResyncHeader)) {
        ERRORLOG("Compressed resync data too large");
        return NULL;
    }

    size_t message_size = sizeof(ResyncHeader) + compressed_size;
    char * message_buffer = (char *) malloc(message_size);
    if (message_buffer == NULL) {
        ERRORLOG("Failed to allocate message buffer");
        return NULL;
    }

    int64_t compress_result = compress((Bytef *)(message_buffer + sizeof(ResyncHeader)), &compressed_size, (const Bytef *)buffer, total_length);
    if (compress_result != Z_OK) {
        ERRORLOG("Compression failed: zlib error %" PRId64, (int64_t)(compress_result));
        free(message_buffer);
        return NULL;
    }

    uLong data_crc = crc32(0, Z_NULL, 0);
    data_crc = crc32(data_crc, (const Bytef *)buffer, total_length);
    NETLOG("Compression successful: %" PRIu64 " -> %" PRIu64 " bytes in %" PRIu64 " ms", (uint64_t)total_length, (uint64_t)compressed_size, (uint64_t)(LbTimerClock() - start_time));

    ResyncHeader header;
    memset(&header, 0, sizeof(header));
    header.message_type = NETMSG_RESYNC_DATA;
    header.compressed_length = (uint64_t)compressed_size;
    header.original_length = (uint64_t)total_length;
    header.gameplay_generation = netstate.gameplay_generation;
    header.data_checksum = (uint64_t)data_crc;

    message_size = sizeof(ResyncHeader) + compressed_size;
    memcpy(message_buffer, &header, sizeof(ResyncHeader));

    *message_size_out = message_size;
    return message_buffer;
}

static void send_resync_message(const char * message_buffer, size_t message_size)
{
    NETLOG("Host: Sending resync data to all clients");
    for (NetUserId user_index = 0; user_index < MAX_NET_USERS; ++user_index) {
        if (netstate.users[user_index].progress != USER_LOGGEDIN) {
            continue;
        }
        netstate.sp->sendmsg_single(netstate.users[user_index].id, message_buffer, message_size);
    }
}

static TbBool receive_resync_message(char ** message_out, size_t * message_size_out)
{
    netstate.resync_pending = false;
    NETLOG("Starting to receive resync data");

    TbClockMSec start_time = LbTimerClock();
    TbClockMSec last_progress_time = start_time;
    int32_t last_progress = 0;
    while (LbTimerClock() - start_time < RESYNC_RECEIVE_TIMEOUT_MS) {
        netstate.sp->update(OnNewUser);
        int32_t progress = GetResyncProgress() * 80 / 100;
        if (progress > last_progress && LbTimerClock() - last_progress_time >= 50) {
            animate_resync_progress_bar(progress, 100);
            last_progress = progress;
            last_progress_time = LbTimerClock();
        }
        size_t received_size = netstate.sp->msgready(SERVER_ID, 10);
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

        if (message_buffer[0] != NETMSG_RESYNC_DATA) {
            MULTIPLAYER_LOG("Received wrong message type: %" PRId64, (int64_t)(message_buffer[0]));
            free(message_buffer);
            continue;
        }
        NETLOG("Resync transfer received: %u bytes in %u ms", (uint32_t)received_size, (uint32_t)(LbTimerClock() - start_time));
        animate_resync_progress_bar(80, 100);
        MULTIPLAYER_LOG("Client: Received resync message, %" PRIu64 " bytes", (uint64_t)received_size);
        *message_out = message_buffer;
        *message_size_out = received_size;
        return true;
    }

    ERRORLOG("Client: Timeout waiting for resync data after %" PRId64 "ms", (int64_t)(RESYNC_RECEIVE_TIMEOUT_MS));
    return false;
}

#define RESYNC_MAX_SCRIPT_DATA (64u * 1024u * 1024u)
#define RESYNC_SLACK (16u * 1024u * 1024u)

size_t resync_max_original_length(void)
{
    return (size_t)KFX_SIM_STATE_SIZE + KFX_NET_STATE_SIZE + KFX_GAME_STATE_SIZE + KFX_FRONTEND_STATE_SIZE
        + KFX_CONFIG_STATE_SIZE + ariadne_saved_state_size() + RESYNC_MAX_SCRIPT_DATA + RESYNC_SLACK;
}

static TbBool decode_resync_message(const char * message_buffer, size_t message_size, size_t expected_length, char ** data_buffer, size_t * data_length)
{
    if (message_size < sizeof(ResyncHeader)) {
        ERRORLOG("Resync message too small: %" PRIu64 " bytes", (uint64_t)message_size);
        return false;
    }
    ResyncHeader header;
    memcpy(&header, message_buffer, sizeof(ResyncHeader));
    if (header.message_type != NETMSG_RESYNC_DATA) {
        ERRORLOG("Resync message has wrong type: %" PRId64, (int64_t)(header.message_type));
        return false;
    }
    if (header.compressed_length != message_size - sizeof(ResyncHeader)) {
        ERRORLOG("Resync message size mismatch: %" PRIu64 " != %" PRIu64,
            (uint64_t)(message_size - sizeof(ResyncHeader)), (uint64_t)(header.compressed_length));
        return false;
    }
    if (expected_length != 0 && header.original_length != expected_length) {
        ERRORLOG("Resync data with wrong size: %" PRIu64 " != %" PRIu64, (uint64_t)(header.original_length), (uint64_t)expected_length);
        return false;
    }
    if (header.original_length > resync_max_original_length()) {
        ERRORLOG("Resync data claims %" PRIu64 " bytes, more than a resync can hold (%" PRIu64 "): refused",
            (uint64_t)header.original_length, (uint64_t)resync_max_original_length());
        return false;
    }
    size_t output_size = header.original_length;
    if (output_size == 0) {
        output_size = 1;
    }
    char * output_buffer = (char *) malloc(output_size);
    if (output_buffer == NULL) {
        ERRORLOG("Failed to allocate resync destination buffer");
        return false;
    }
    uLongf dest_len = output_size;
    int64_t uncompress_result = uncompress((Bytef *)output_buffer, &dest_len,
        (const Bytef *)(message_buffer + sizeof(ResyncHeader)), header.compressed_length);
    if (uncompress_result != Z_OK || dest_len != header.original_length) {
        ERRORLOG("Decompression failed: zlib error %" PRId64 ", expected %" PRIu64 " bytes, got %" PRIu64 " bytes",
            (int64_t)(uncompress_result), (uint64_t)(header.original_length), (uint64_t)dest_len);
        free(output_buffer);
        return false;
    }
    uLong verify_crc = crc32(0, Z_NULL, 0);
    verify_crc = crc32(verify_crc, (const Bytef *)output_buffer, header.original_length);
    if ((uint64_t)verify_crc != header.data_checksum) {
        ERRORLOG("Resync data checksum mismatch");
        free(output_buffer);
        return false;
    }
    *data_buffer = output_buffer;
    *data_length = header.original_length;
    return true;
}

static TbBool send_resync_data(const void * buffer, size_t total_length)
{
    size_t message_size = 0;
    char * message_buffer = encode_resync_message(buffer, total_length, &message_size);
    if (message_buffer == NULL) {
        return false;
    }
    send_resync_message(message_buffer, message_size);
    free(message_buffer);
    return true;
}

static TbBool receive_resync_data(char ** data_buffer, size_t * data_length)
{
    char * message_buffer = NULL;
    size_t message_size = 0;
    if (!receive_resync_message(&message_buffer, &message_size)) {
        return false;
    }
    TbBool result = decode_resync_message(message_buffer, message_size, *data_length, data_buffer, data_length);
    free(message_buffer);
    if (result) {
        NETLOG("Client: Resync data received successfully");
    }
    return result;
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

// The game state a resync carries (the framing below); NULL on failure.
static char *build_resync_game_data(size_t *full_resync_len_out)
{
    size_t lua_data_len = 0;
    const char * lua_data = script_lua_resync_export(&lua_data_len);
    if (lua_data == NULL) {
        script_cleanup_serialized_data();
        return NULL;
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
        return NULL;
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
        return NULL;
    }

    size_t full_resync_len = lua_data_offset + lua_data_len;
    char * full_resync_data = (char *) malloc(full_resync_len);
    if (full_resync_data == NULL) {
        ERRORLOG("Failed to allocate full resync buffer");
        script_cleanup_serialized_data();
        return NULL;
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
    script_cleanup_serialized_data();
    *full_resync_len_out = full_resync_len;
    return full_resync_data;
}

TbBool send_resync_game(void)
{
    pack_desync_history_for_resync();
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    animate_resync_progress_bar(0, 100);
    NETLOG("Initiating re-synchronization of network game");
    take_game_timestamp();
    size_t full_resync_len = 0;
    char * full_resync_data = build_resync_game_data(&full_resync_len);
    if (full_resync_data == NULL) {
        return false;
    }
    size_t message_size = 0;
    netstate.gameplay_generation += 1;
    char * message_buffer = encode_resync_message(full_resync_data, full_resync_len, &message_size);
    free(full_resync_data);
    if (message_buffer == NULL) {
        netstate.gameplay_generation -= 1;
        return false;
    }
    send_resync_message(message_buffer, message_size);
    // a replay of this game resyncs where the live game did (upstream #5376)
    game_replay_record_resync(message_buffer, message_size);
    free(message_buffer);
    animate_resync_progress_bar(90, 100);
    NETLOG("Host: Resync data queued");
    return true;
}

// Applies a resync's game state (build_resync_game_data()'s framing); frees nothing.
/** Whether the simulation state received in a resync (at `data`, possibly not aligned for the struct) passes
 *  sim_state_validate(). */
TbBool resync_sim_state_acceptable(const char *data)
{
    char why[256];
    void *aligned_copy = NULL;
    const struct KfxSimState *candidate = (const struct KfxSimState *)data;
    if (((uintptr_t)data % alignof(struct KfxSimState)) != 0) {
        aligned_copy = malloc(sizeof(kfx_sim_state));
        if (aligned_copy == NULL) {
            ERRORLOG("Resync refused: no memory to check the received state");
            return false;
        }
        memcpy(aligned_copy, data, sizeof(kfx_sim_state));
        candidate = (const struct KfxSimState *)aligned_copy;
    }
    const TbBool ok = sim_state_validate(candidate, why, sizeof(why));
    free(aligned_copy);
    if (!ok) {
        ERRORLOG("Resync refused, the received state is invalid: %s", why);
    }
    return ok;
}

/** Whether the configuration received in a resync (at `data`, KFX_CONFIG_STATE_SAVED_LEN bytes, possibly not aligned for
 *  the struct) passes config_state_validate(). */
TbBool resync_config_state_acceptable(const char *data)
{
    char why[256];
    struct KfxConfigState *candidate = (struct KfxConfigState *)calloc(1, sizeof(struct KfxConfigState));
    if (candidate == NULL) {
        ERRORLOG("Resync refused: no memory to check the received configuration");
        return false;
    }
    memcpy(candidate, data, KFX_CONFIG_STATE_SAVED_LEN);
    const TbBool ok = config_state_validate(candidate, why, sizeof(why));
    free(candidate);
    if (!ok) {
        ERRORLOG("Resync refused, the received configuration is invalid: %s", why);
    }
    return ok;
}

static TbBool apply_resync_game_data(const char * full_resync_data, size_t full_resync_len)
{

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
        return false;
    }
    full_resync_len -= sizeof(versions);
    const size_t ariadne_state_len = ariadne_saved_state_size();
    size_t min_header_size = sizeof(uint64_t) + sizeof(kfx_sim_state) + sizeof(kfx_net_state) + ariadne_state_len + KFX_CONFIG_STATE_SAVED_LEN + sizeof(uint64_t) + sizeof(uint64_t);
    if (full_resync_len < min_header_size) {
        ERRORLOG("Full resync data too small: %" PRIu64 " bytes", (uint64_t)full_resync_len);
        return false;
    }

    uint64_t game_state_len = 0;
    memcpy(&game_state_len, read_ptr, sizeof(game_state_len)); read_ptr += sizeof(game_state_len);
    if ((size_t)(data_end - read_ptr) < (size_t)game_state_len + sizeof(kfx_sim_state) + sizeof(kfx_net_state) + ariadne_state_len + KFX_CONFIG_STATE_SAVED_LEN + sizeof(uint64_t) + sizeof(uint64_t)) {
        ERRORLOG("Full resync data truncated (game state)");
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
        return false;
    }
    const char * frontend_state_data = read_ptr; read_ptr += frontend_state_len;

    uint64_t lua_data_len = 0;
    memcpy(&lua_data_len, read_ptr, sizeof(lua_data_len)); read_ptr += sizeof(lua_data_len);
    if ((size_t)(data_end - read_ptr) != lua_data_len) {
        ERRORLOG("Received lua data with wrong size: %" PRIu64 " != %" PRIu64, (uint64_t)(lua_data_len), (uint64_t)(data_end - read_ptr));
        return false;
    }
    const char * lua_data = read_ptr;

    // The state about to replace ours comes from the host's machine: nothing in it is trusted until its indexes and
    // links are known to stay inside the state (security pass S06). Before anything local changes.
    if (!resync_sim_state_acceptable(kfx_sim_state_data)) {
        return false;
    }
    if (!resync_config_state_acceptable(config_state_data)) {
        return false;
    }
    {
        char why[256];
        if (!ariadne_saved_state_check(ariadne_state_data, why, sizeof(why))) {
            ERRORLOG("Resync refused, the received navigation mesh is invalid: %s", why);
            return false;
        }
    }

    if (!game_resync_import_game_state(game_state_data, game_state_len)) {
        return false;
    }
    if (!ui_resync_import_frontend_state(frontend_state_data, frontend_state_len)) {
        return false;
    }
    if (!script_lua_resync_import(lua_data, lua_data_len)) {
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
    return true;
}

static TbBool apply_resync_game_message(const char * message_buffer, size_t message_size)
{
    TbClockMSec start_time = LbTimerClock();
    char * full_resync_data = NULL;
    size_t full_resync_len = 0;
    if (!decode_resync_message(message_buffer, message_size, 0, &full_resync_data, &full_resync_len)) {
        return false;
    }
    TbBool result = apply_resync_game_data(full_resync_data, full_resync_len);
    free(full_resync_data);
    if (result) {
        ResyncHeader header;
        memcpy(&header, message_buffer, sizeof(header));
        netstate.gameplay_generation = header.gameplay_generation;
    }
    NETLOG("Resync decompression and state application took %u ms", (uint32_t)(LbTimerClock() - start_time));
    return result;
}

TbBool receive_resync_game(void)
{
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    animate_resync_progress_bar(0, 100);
    NETLOG("Initiating re-synchronization of network game");
    char * message_buffer = NULL;
    size_t message_size = 0;
    if (!receive_resync_message(&message_buffer, &message_size)) {
        return false;
    }
    TbBool result = apply_resync_game_message(message_buffer, message_size);
    if (result) {
        game_replay_record_resync(message_buffer, message_size);
    }
    free(message_buffer);
    if (!result) {
        return false;
    }
    animate_resync_progress_bar(90, 100);

    compare_desync_history_from_host();

    return true;
}


static void finish_resync(int64_t local_lens)
{
    if (script_lua_script_active()) {
        script_lua_set_random_seed(kfx_sim_state.action_random_seed);
    }
    // No rebuild of the navigation mesh here (upstream #5405 has one): the resync carried the host's mesh itself
    // (ariadne_saved_state.h), restored by apply_resync_game_data(); a rebuilt one would be a different mesh.
    game_reinit_level_after_load();
    reinitialise_eye_lens(local_lens);

    kfx_net_state.skip_initial_input_turns = calculate_skip_input();
    initialize_packet_history();
    NETLOG("Input lag after resync: %" PRId64 " turns", (int64_t)(kfx_net_state.input_lag_turns));

    clear_flag(local_system_flags, GSF_NetGameNoSync);
    clear_flag(local_system_flags, GSF_NetSeedNoSync);
}

void resync_game(void)
{
    TbClockMSec start_time = LbTimerClock();
    SYNCDBG(2,"Starting");
    ui_draw_out_of_sync_box(0, 32*units_per_pixel/16, local_state.engine_window_x);
    // The eye lens (the possession view's effect) is this machine's: keep which lens it was, and set the lens system
    // up again after the resync, as loading a game does (refactor pass 4, P4-F13: it was shut down here and never
    // set up again, so lenses stopped working for the rest of the game on every machine).
    const int64_t local_lens = kfx_local_view.applied_lens_type;
    reset_eye_lenses();
    TbBool result;
    if (network_is_host()) {
        result = send_resync_game();
    } else {
        result = receive_resync_game();
    }
    if (!result) {
        reinitialise_eye_lens(local_lens);
        return;
    }
    wait_for_all_players();
    animate_resync_progress_bar(100, 100);
    NETLOG("Resync finished locally in %u ms", (uint32_t)(LbTimerClock() - start_time));
    finish_resync(local_lens);
}

// A resync recorded in a replay, applied at the end of its turn as the live game did (upstream #5376).
TbBool apply_recorded_resync(const char * message_buffer, size_t message_size)
{
    const int64_t local_lens = kfx_local_view.applied_lens_type;
    reset_eye_lenses();
    if (!apply_resync_game_message(message_buffer, message_size)) {
        reinitialise_eye_lens(local_lens);
        return false;
    }
    finish_resync(local_lens);
    return true;
}

#ifdef __cplusplus
}
#endif
