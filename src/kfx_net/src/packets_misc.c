/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file packets_misc.c
 *     Processing packets with cheat commands.
 * @par Purpose:
 *     Functions for creating and executing packets.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     20 Sep 2020 - 20 Sep 2020
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "net_game.h"
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "packets.h"

#include "bflib_fileio.h"
#include "net_exchange_gameplay.h"
#include "bflib_datetm.h"
#include "net_callbacks.h"
#include "save_catalogue.h"
#include "config_settings.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "config_keeperfx.h"
#include "player_utils.h"
#include "slab_data.h"
#include "dungeon_data.h"
#include "tasks_list.h"
#include "spdigger_stack.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define PACKET_TURN_MAX_SIZE (MAX_NET_USERS*sizeof(struct Packet) + sizeof(TbBigChecksum))
#define MULTIPLAYER_PAUSE_COOLDOWN_MS 500
uint64_t initial_replay_seed;
uint64_t last_pause_toggle_time = 0;
extern TbBool IMPRISON_BUTTON_DEFAULT;
extern TbBool FLEE_BUTTON_DEFAULT;
extern TbBool get_skip_heart_zoom_feature(void);
extern TbBool keeper_screen_redraw(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif

// These three "for player" writers are how the local human's own input (front_input.c, and every frontend/editor
// menu action) reaches its packet -- always meaning "my own local input", never a specific remote/seat player,
// the same assumption get_local_user()/get_local_packet() (packet_data.c) already encode for front_input.c's
// other writes. Reading player->user_id directly broke that once net_add_external_seat() could reassign
// my_player_number's own user_id (M10): converting the local player's own seat to an External one, everything
// here would silently redirect into the seat's packet slot instead, racing extseat_tick()'s writes to that same
// slot -- e.g. front_input.c's per-frame set_players_packet_control(get_my_player(), PCtr_Gui) would land on the
// seat's packet turn after turn, and process_dungeon_control_packet_clicks()'s own early
// `if (flag_is_set(pckt->control_flags,PCtr_Gui)) return false;` would then silently no-op the seat's
// agent-submitted clicks, with no error anywhere.
static struct Packet *get_players_own_packet(struct PlayerInfo *player)
{
    if (player->id_number == my_player_number) {
        return get_local_packet();
    }
    return get_packet(player->user_id);
}

unsigned char get_players_packet_action(struct PlayerInfo *player)
{
    return get_players_own_packet(player)->action;
}

void set_packet_control(struct Packet *pckt, uint64_t flag)
{
  pckt->control_flags |= flag;
}

void set_players_packet_control(struct PlayerInfo *player, uint64_t flag)
{
    get_players_own_packet(player)->control_flags |= flag;
}

void unset_packet_control(struct Packet *pckt, uint64_t flag)
{
    pckt->control_flags &= ~flag;
}

void unset_players_packet_control(struct PlayerInfo *player, uint64_t flag)
{
    get_players_own_packet(player)->control_flags &= ~flag;
}

void set_players_packet_position(struct Packet *pckt, int64_t x, int64_t y, unsigned char context)
{
    pckt->pos_x = x;
    pckt->pos_y = y;
    pckt->control_flags |= PCtr_MapCoordsValid;
    pckt->additional_packet_values &= ~PCAdV_ContextMask;
    pckt->additional_packet_values |= (context << 1);
}

void clear_packets(void)
{
    for (int64_t i = 0; i < PACKETS_COUNT; i++)
    {
        memset(&sim_packets[i], 0, sizeof(struct Packet));
    }
}

static int64_t packet_saved_users(NetUserId *users)
{
    int64_t n = 0;
    for (NetUserId user = 0; user < MAX_NET_USERS; user++)
    {
        if (kfx_net_state.packet_save_head.user_players[user] >= 0)
            users[n++] = user;
    }
    return n;
}

static int64_t packet_turn_size(void)
{
    NetUserId users[MAX_NET_USERS];
    return packet_saved_users(users) * sizeof(struct Packet) + sizeof(TbBigChecksum);
}

// If the turn-end checksum is LONG_TURN_MARKER, additional data follows, for what can't
// be expressed just in regular packets. (The real checksum comes right after the long turn marker.)
#define LONG_TURN_MARKER ((TbBigChecksum)0x80000000)
enum LongTurnReplayRecordKind {
    LTK_End = 0,
    LTK_ChatMessage = 1, // payload: uint16_t user, char message[PLAYER_MP_MESSAGE_LEN]
    // TODO: resyncs / state transfers?
};

static TbBool chat_messages_recorded(void)
{
    // Chat messages are sent out-of-band, so they break the requirements
    // for determinism if they don't arrive the same turn they are sent.
    // (Also, it's nice to preserve privacy too, in case any couples send a bug report...)
    return !network_is_active() && (kfx_net_state.input_lag_turns == 0);
}

static TbBool write_long_turn_record(unsigned char kind, uint32_t len, const void *payload)
{
    return (LbFileWrite(kfx_net_local.packet_save_fp, &kind, sizeof(kind)) == sizeof(kind))
        && (LbFileWrite(kfx_net_local.packet_save_fp, &len, sizeof(len)) == sizeof(len))
        && (LbFileWrite(kfx_net_local.packet_save_fp, payload, len) == (long)len);
}

static TbBool read_long_turn_data(TbBigChecksum *chksum, TbBool apply, int32_t *consumed)
{
    TbFileHandle fh = kfx_net_local.packet_save_fp;
    if (LbFileRead(fh, chksum, sizeof(*chksum)) != sizeof(*chksum))
        return false;
    *consumed += sizeof(*chksum);
    while (true)
    {
        unsigned char kind;
        if (LbFileRead(fh, &kind, sizeof(kind)) != sizeof(kind))
            return false;
        *consumed += sizeof(kind);
        if (kind == LTK_End)
            return true;
        uint32_t len;
        if (LbFileRead(fh, &len, sizeof(len)) != sizeof(len))
            return false;
        *consumed += sizeof(len);
        const int32_t remaining = LbFileLengthHandle(fh) - LbFilePosition(fh);
        if ((remaining < 0) || (len > (uint32_t)remaining))
        {
            ERRORLOG("Long turn record kind %u length %u exceeds Packet File (%d bytes left)", (unsigned)kind, (unsigned)len, (int)remaining);
            return false;
        }
        uint32_t used = 0;
        if (apply && (kind == LTK_ChatMessage) && (len >= sizeof(uint16_t)))
        {
            uint16_t user;
            if (LbFileRead(fh, &user, sizeof(user)) != sizeof(user))
                return false;
            used += sizeof(user);
            const uint32_t msg_len = min(len - used, (uint32_t)PLAYER_MP_MESSAGE_LEN);
            char message[PLAYER_MP_MESSAGE_LEN] = {0};
            if (LbFileRead(fh, message, msg_len) != (int)msg_len)
                return false;
            used += msg_len;
            message[PLAYER_MP_MESSAGE_LEN - 1] = '\0';
            PlayerNumber plyr_idx = (user < MAX_NET_USERS) ? get_net_user_player_number(user) : -1;
            if (plyr_idx >= 0)
                memcpy(get_player(plyr_idx)->mp_pending_message, message, PLAYER_MP_MESSAGE_LEN);
            else
                WARNLOG("Chat message for invalid user %u in Packet File", (unsigned)user);
        }
        if ((len > used) && (LbFileSeek(fh, len - used, Lb_FILE_SEEK_CURRENT) < 0))
            return false;
        *consumed += len;
    }
}

static GameTurn count_stored_turns(void)
{
    NetUserId users[MAX_NET_USERS];
    const int nusers = packet_saved_users(users);
    const int turn_data_size = packet_turn_size();
    const int32_t file_len = LbFileLengthHandle(kfx_net_local.packet_save_fp);
    unsigned char pckt_buf[PACKET_TURN_MAX_SIZE+4];
    GameTurn turns = 0;
    int32_t pos = kfx_net_state.packet_file_pos;
    LbFileSeek(kfx_net_local.packet_save_fp, pos, Lb_FILE_SEEK_BEGINNING);
    while (pos + turn_data_size <= file_len)
    {
        if (LbFileRead(kfx_net_local.packet_save_fp, &pckt_buf, turn_data_size) != turn_data_size)
            break;
        int32_t consumed = turn_data_size;
        TbBigChecksum chksum = llong(&pckt_buf[nusers * sizeof(struct Packet)]);
        if ((chksum == LONG_TURN_MARKER) && !read_long_turn_data(&chksum, false, &consumed))
            break;
        if (pos + consumed > file_len)
            break;
        pos += consumed;
        turns++;
    }
    if (pos != file_len)
        ERRORLOG("Packet File unreadable at offset %d of %d; replay ends after %u turns", (int)pos, (int)file_len, (unsigned)turns);
    LbFileSeek(kfx_net_local.packet_save_fp, kfx_net_state.packet_file_pos, Lb_FILE_SEEK_BEGINNING);
    return turns;
}

TbBool open_packet_file_for_load(char *fname, struct CatalogueEntry *centry)
{
    memset(centry, 0, sizeof(struct CatalogueEntry));
    strcpy(kfx_net_state.packet_fname, fname);
    kfx_net_local.packet_save_fp = LbFileOpen(kfx_net_state.packet_fname, Lb_FILE_MODE_READ_ONLY);
    if (!kfx_net_local.packet_save_fp)
    {
        ERRORLOG("Cannot open keeper packet file for load");
        kfx_net_state.packet_fopened = 0;
        return false;
    }
    int64_t i = net_callbacks->load_game_chunks(kfx_net_local.packet_save_fp, centry);
    if ((i != GLoad_PacketStart) && (i != GLoad_PacketContinue))
    {
        LbFileClose(kfx_net_local.packet_save_fp);
        kfx_net_local.packet_save_fp = NULL;
        kfx_net_state.packet_fopened = 0;
        WARNMSG("Couldn't correctly read packet file \"%s\" header.",fname);
        return false;
    }
    kfx_net_state.packet_file_pos = LbFilePosition(kfx_net_local.packet_save_fp);
    kfx_net_state.turns_stored = count_stored_turns();
    if ((kfx_net_state.packet_checksum_verify) && (!kfx_net_state.packet_save_head.chksum_available))
    {
        WARNMSG("PacketSave checksum not available, checking disabled.");
        kfx_net_state.packet_checksum_verify = false;
    }
    if (kfx_net_state.log_things_start_turn == -1)
    {
        kfx_net_state.log_things_start_turn = 0;
        kfx_net_state.log_things_end_turn = kfx_net_state.turns_stored + 1;
    }
    kfx_net_state.packet_fopened = 1;
    return true;
}

void restore_users_from_packet_save(void)
{
    TbBool local_mapped = false;
    for (NetUserId user = 0; user < MAX_NET_USERS; user++)
    {
        set_net_user_player_number(user, -1);
    }
    for (NetUserId user = 0; user < MAX_NET_USERS; user++)
    {
        PlayerNumber plyr_idx = kfx_net_state.packet_save_head.user_players[user];
        if (plyr_idx < 0)
            continue;
        if ((plyr_idx >= PLAYERS_COUNT)
         || !flag_is_set(kfx_net_state.packet_save_head.players_exist, to_flag(plyr_idx)))
        {
            WARNLOG("Packet file maps user %" PRId64 " to player %" PRId64 ", which the file says does not exist",
                (int64_t)user, (int64_t)plyr_idx);
            continue;
        }
        set_net_user_player_number(user, plyr_idx);
        struct PlayerInfo *player = get_player(plyr_idx);
        player->user_id = user;
        snprintf(player->player_name, sizeof(player->player_name), "%s",
            kfx_net_state.packet_save_head.user_names[user]);
        init_user_state(user);
        local_mapped |= (plyr_idx == my_player_number);
        SYNCLOG("Replay user %" PRId64 " -> player %" PRId64, (int64_t)user, (int64_t)plyr_idx);
    }
    if (!local_mapped)
    {
        set_net_user_player_number(SOLO_HUMAN_ID, my_player_number);
        get_player(my_player_number)->user_id = SOLO_HUMAN_ID;
        init_user_state(SOLO_HUMAN_ID);
        SYNCLOG("Replay local user %" PRId64 " -> player %" PRId64 " (not in the recorded map)",
            (int64_t)SOLO_HUMAN_ID, (int64_t)my_player_number);
    }
}

void post_init_packets(void)
{
    SYNCDBG(6,"Starting");
    initialize_packet_history();
    clear_packets();
}

/**
 * Computes verification checksum for -packetsave/-packetload replay files.
 * NOT used for multiplayer - only for single-player replay integrity checking.
 * Sums position/movement data of all things except ambient sounds and effect elements.
 *
 * @return Checksum value for detecting replay file corruption
 */
TbBigChecksum compute_replay_integrity(void)
{
    TbBigChecksum sum = 0;
    for (int64_t tng_idx = 0; tng_idx < THINGS_COUNT; tng_idx++)
    {
        struct Thing* tng = thing_get(tng_idx);
        if ((tng->alloc_flags & TAlF_Exists) != 0)
        {
            // It would be nice to completely ignore effects, but since
            // thing indices are used in packets, lack of effect may cause desync too.
            if (!is_non_synchronized_thing_class(tng->class_id))
            {
                sum += (uint64_t)tng->mappos.x.val + (uint64_t)tng->mappos.y.val + (uint64_t)tng->mappos.z.val
                     + (uint64_t)tng->move_angle_xy + (uint64_t)tng->owner;
            }
        }
    }
    for (MapSlabCoord slb_y = 0; slb_y < kfx_sim_state.map_tiles_y; slb_y++)
    {
        for (MapSlabCoord slb_x = 0; slb_x < kfx_sim_state.map_tiles_x; slb_x++)
        {
            const struct SlabMap* slb = get_slabmap_block(slb_x, slb_y);
            sum += (uint64_t)slb->kind + (uint64_t)slb->owner + (uint64_t)slb->health;
        }
    }
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        const struct PlayerInfo* player = get_player(plyr_idx);
        if (!player_exists(player))
            continue;
        const struct Dungeon* dungeon = get_players_dungeon(player);
        if (dungeon_invalid(dungeon))
            continue;
        for (size_t i = 0; i < MAPTASKS_COUNT; i++)
        {
            const struct MapTask* task = &dungeon->task_list[i];
            if (task->kind != SDDigTask_None)
                sum += (uint64_t)task->kind + (uint64_t)task->coords;
        }
    }
    return sum;
}

int64_t save_packets(void)
{
    NetUserId users[MAX_NET_USERS];
    const int64_t nusers = packet_saved_users(users);
    const int64_t turn_data_size = nusers * sizeof(struct Packet) + sizeof(TbBigChecksum);
    unsigned char pckt_buf[PACKET_TURN_MAX_SIZE+4];
    TbBigChecksum chksum;
    SYNCDBG(6,"Starting");
    if (kfx_net_state.packet_checksum_verify)
        chksum = compute_replay_integrity();
    else
        chksum = 0;
    LbFileSeek(kfx_net_local.packet_save_fp, 0, Lb_FILE_SEEK_END);
    // Prepare data in the buffer
    for (int64_t i = 0; i < nusers; i++)
        memcpy(&pckt_buf[i*sizeof(struct Packet)], &sim_packets[users[i]], sizeof(struct Packet));
    TbBool has_chat = false;
    if (chat_messages_recorded()) {
        for (int64_t i = 0; i < nusers; i++)
            has_chat |= (sim_packets[users[i]].action == PckA_PlyrMsgEnd);
    }
    const TbBool long_turn = has_chat || (chksum == LONG_TURN_MARKER);
    const TbBigChecksum chksum_slot = long_turn ? LONG_TURN_MARKER : chksum;
    memcpy(&pckt_buf[nusers*sizeof(struct Packet)], &chksum_slot, sizeof(TbBigChecksum));
    // Write buffer into file
    if (LbFileWrite(kfx_net_local.packet_save_fp, &pckt_buf, turn_data_size) != turn_data_size)
    {
        ERRORLOG("Packet file write error");
    }
    if (long_turn)
    {
        TbBool ok = (LbFileWrite(kfx_net_local.packet_save_fp, &chksum, sizeof(chksum)) == sizeof(chksum));
        // write a null-terminated sequence of additional records
        for (int64_t i = 0; has_chat && (i < nusers); i++) {
            if (sim_packets[users[i]].action != PckA_PlyrMsgEnd)
                continue;
            unsigned char payload[sizeof(uint16_t) + PLAYER_MP_MESSAGE_LEN];
            const uint16_t user = users[i];
            memcpy(payload, &user, sizeof(user));
            memcpy(payload + sizeof(user), get_player(get_net_user_player_number(users[i]))->mp_pending_message, PLAYER_MP_MESSAGE_LEN);
            ok &= write_long_turn_record(LTK_ChatMessage, sizeof(payload), payload);
        }
        const unsigned char end = LTK_End;
        ok &= (LbFileWrite(kfx_net_local.packet_save_fp, &end, sizeof(end)) == sizeof(end));
        if (!ok)
            ERRORLOG("Long turn data file write error");
    }
    if ( !LbFileFlush(kfx_net_local.packet_save_fp) )
    {
        ERRORLOG("Unable to flush PacketSave File");
        return false;
    }
    if (packetsave_max_kb > 0)
    {
        int64_t pos = LbFilePosition(kfx_net_local.packet_save_fp);
        if ((pos >= 0) && ((uint64_t)pos >= packetsave_max_kb * 1024))
        {
            WARNLOG("PacketSave reached the %" PRIu64 " KB limit at turn %" PRIu64 "; recording stopped",
                (uint64_t)(packetsave_max_kb), (uint64_t)(get_gameturn()));
            close_packet_file();
            kfx_net_state.packet_save_enable = false;
        }
    }
    return true;
}

void close_packet_file(void)
{
    if ( kfx_net_state.packet_fopened )
    {
        LbFileClose(kfx_net_local.packet_save_fp);
        kfx_net_state.packet_fopened = 0;
        kfx_net_local.packet_save_fp = NULL;
    }
}

void dump_memory_to_file(const char * fname, const char * buf, size_t len)
{
    FILE* file = fopen(fname, "w");
    fwrite(buf, 1, len, file);
    fflush(file);
    fclose(file);
}

void write_debug_packets(void)
{
    //note, changed this to be more general and to handle multiplayer where there can
    //be several players writing to same directory if testing on local machine
    char filename[32];
    snprintf(filename, sizeof(filename), "%s%" PRIu64 ".%s", "keeperd", (uint64_t)(my_player_number), "pck");
    dump_memory_to_file(filename, (char*) sim_packets, sizeof(sim_packets));
}

void write_debug_screenpackets(void)
{
    char filename[32];
    snprintf(filename, sizeof(filename), "%s%" PRIu64 ".%s", "keeperd", (uint64_t)(my_player_number), "spck");
    dump_memory_to_file(filename, (char*) net_screen_packet, sizeof(net_screen_packet));
}

TbBool reinit_packets_after_load(void)
{
    kfx_net_state.packet_save_enable = false;
    kfx_net_state.packet_load_enable = false;
    kfx_net_local.packet_save_fp = NULL;
    kfx_net_state.packet_fopened = 0;
    return true;
}

TbBool open_new_packet_file_for_save(void)
{
    // Filling the header
    SYNCMSG("Starting packet saving, turn %" PRIu64,(uint64_t)get_gameturn());
    kfx_net_state.packet_save_head.game_ver_major = VER_MAJOR;
    kfx_net_state.packet_save_head.game_ver_minor = VER_MINOR;
    kfx_net_state.packet_save_head.game_ver_release = VER_RELEASE;
    kfx_net_state.packet_save_head.game_ver_build = VER_BUILD;
    kfx_net_state.packet_save_head.level_num = get_loaded_level_number();
    kfx_net_state.packet_save_head.players_exist = 0;
    kfx_net_state.packet_save_head.players_comp = 0;
    kfx_net_state.packet_save_head.chksum_available = kfx_net_state.packet_checksum_verify;
    kfx_net_state.packet_save_head.isometric_view_zoom_level = settings.isometric_view_zoom_level;
    kfx_net_state.packet_save_head.frontview_zoom_level = settings.frontview_zoom_level;
    kfx_net_state.packet_save_head.isometric_tilt = settings.isometric_tilt;
    kfx_net_state.packet_save_head.video_rotate_mode = settings.video_rotate_mode;
    kfx_net_state.packet_save_head.action_seed = initial_replay_seed;
    kfx_net_state.packet_save_head.skip_heart_zoom = get_skip_heart_zoom_feature();
    kfx_net_state.packet_save_head.default_imprison_tendency = IMPRISON_BUTTON_DEFAULT;
    kfx_net_state.packet_save_head.default_flee_tendency = FLEE_BUTTON_DEFAULT;
    kfx_net_state.packet_save_head.highlight_mode = settings.highlight_mode;
    for (NetUserId user = 0; user < MAX_NET_USERS; user++)
        kfx_net_state.packet_save_head.user_players[user] = get_net_user_player_number(user);
    kfx_net_state.packet_save_head.recording_user = get_local_user();
    kfx_net_state.packet_save_head.frontend_alliances = net_callbacks->get_frontend_alliances();
    for (NetUserId user = 0; user < MAX_NET_USERS; user++)
    {
        const char *name = network_user_name(user);
        snprintf(kfx_net_state.packet_save_head.user_names[user],
            sizeof(kfx_net_state.packet_save_head.user_names[user]), "%s", (name != NULL) ? name : "");
    }
    for (int64_t i = 0; i < PLAYERS_COUNT; i++)
    {
        struct PlayerInfo* player = get_player(i);
        if (player_exists(player))
        {
            set_flag(kfx_net_state.packet_save_head.players_exist, to_flag(i));
            if ((player->allocflags & PlaF_CompCtrl) != 0)
              set_flag(kfx_net_state.packet_save_head.players_comp, to_flag(i));
        }
    }
    LbFileDelete(kfx_net_state.packet_fname);
    kfx_net_local.packet_save_fp = LbFileOpen(kfx_net_state.packet_fname, Lb_FILE_MODE_NEW);
    if (!kfx_net_local.packet_save_fp)
    {
        ERRORLOG("Cannot open keeper packet file for save, \"%s\".",kfx_net_state.packet_fname);
        kfx_net_state.packet_fopened = 0;
        return false;
    }
    struct CatalogueEntry centry;
    net_callbacks->fill_game_catalogue_entry(&centry, "Packet file");
    if (!net_callbacks->save_packet_chunks(kfx_net_local.packet_save_fp,&centry))
    {
        WARNMSG("Cannot write to packet file, \"%s\".",kfx_net_state.packet_fname);
        LbFileClose(kfx_net_local.packet_save_fp);
        kfx_net_state.packet_fopened = 0;
        kfx_net_local.packet_save_fp = NULL;
        return false;
    }
    kfx_net_state.packet_fopened = 1;
    return true;
}

static TbBool turn_has_quit_packet(void)
{
    for (NetUserId i = 0; i < MAX_NET_USERS; i++)
    {
        switch (sim_packets[i].action)
        {
        case PckA_QuitToMainMenu:
        case PckA_ForceApplicationClose:
            return true;
        default:
            break;
        }
    }
    return false;
}

void load_packets_for_turn(GameTurn nturn)
{
    SYNCDBG(19,"Starting");
    NetUserId users[MAX_NET_USERS];
    const int64_t nusers = packet_saved_users(users);
    const int64_t turn_data_size = nusers * sizeof(struct Packet) + sizeof(TbBigChecksum);
    unsigned char pckt_buf[PACKET_TURN_MAX_SIZE+4];
    if (nturn >= kfx_net_state.turns_stored)
    {
        ERRORDBG(18,"Out of turns to load from Packet File");
        net_callbacks->report_error_stat(ESE_CantReadPackets);
        return;
    }

    if (LbFileRead(kfx_net_local.packet_save_fp, &pckt_buf, turn_data_size) != turn_data_size)
    {
        ERRORDBG(18,"Cannot read turn data from Packet File");
        net_callbacks->report_error_stat(ESE_CantReadPackets);
        return;
    }
    kfx_net_state.packet_file_pos += turn_data_size;
    for (int64_t i = 0; i < nusers; i++)
        memcpy(&sim_packets[users[i]], &pckt_buf[i * sizeof(struct Packet)], sizeof(struct Packet));
    for (int64_t i = 0; i < nusers; i++) {
        if (sim_packets[users[i]].action == PckA_PlyrMsgEnd)
            memset(get_player(get_net_user_player_number(users[i]))->mp_pending_message, 0, PLAYER_MP_MESSAGE_LEN);
    }
    TbBigChecksum tot_chksum = llong(&pckt_buf[nusers * sizeof(struct Packet)]);
    if (tot_chksum == LONG_TURN_MARKER)
    {
        int32_t consumed = 0;
        if (!read_long_turn_data(&tot_chksum, true, &consumed)) {
            ERRORLOG("Cannot read long turn data from Packet File; replay aborted at turn %" PRIu64, (uint64_t)(get_gameturn()));
            net_callbacks->report_error_stat(ESE_CantReadPackets);
            disable_packet_mode();
            return;
        }
        kfx_net_state.packet_file_pos += consumed;
    }
    if (kfx_net_state.turns_fastforward > 0)
        kfx_net_state.turns_fastforward--;
    if (kfx_net_state.packet_checksum_verify && !turn_has_quit_packet())
    {
        if (compute_replay_integrity() != tot_chksum)
        {
            ERRORLOG("PacketSave checksum - Out of sync (GameTurn %" PRIu64 ")", (uint64_t)(get_gameturn()));
            if (!net_callbacks->is_onscreen_msg_visible())
                net_callbacks->show_onscreen_msg(kfx_sim_state.turns_per_second, "Out of sync");
        }
    }
}

void set_packet_pause_toggle()
{
    struct PlayerInfo* player = get_my_player();
    if (player_invalid(player))
        return;
    if (player->user_id >= PACKETS_COUNT)
        return;
    if (kfx_sim_state.game_kind != GKind_LocalGame) {
        uint64_t current_time = LbTimerClock();
        if (current_time - last_pause_toggle_time < MULTIPLAYER_PAUSE_COOLDOWN_MS) {
            MULTIPLAYER_LOG("set_packet_pause_toggle: cooldown active, ignoring");
            return;
        }
        last_pause_toggle_time = current_time;
    }
    if ((kfx_sim_state.operation_flags & GOF_Paused) == 0) {
        set_players_packet_action(player, PckA_TogglePause, 1, 0, 0, 0);
        return;
    }
    if (kfx_sim_state.game_kind != GKind_LocalGame) {
        MULTIPLAYER_LOG("set_packet_pause_toggle: broadcasting unpause");
        unpausing_in_progress = 1;
        keeper_screen_redraw();
        RendererPresentStepFrame();
        LbNetwork_BroadcastUnpause();
        if (network_is_host()) {
            process_pause_packet(0, 0);
        }
        unpausing_in_progress = 0;
        return;
    }
    process_pause_packet(0, 0);
}

void disable_packet_mode(void)
{
    close_packet_file();
    kfx_net_state.packet_load_enable = false;
    kfx_net_state.packet_save_enable = false;
    remap_local_user_to_solo();
    net_callbacks->show_onscreen_msg(2*kfx_sim_state.turns_per_second, "Packet mode disabled");
    net_callbacks->set_gui_visible(true);
}
