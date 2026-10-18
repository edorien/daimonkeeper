/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_replay.h
 *     Header file for game_replay.c.
 * @par Purpose:
 *     Replay (packet file) recording and playback, moved from kfx_net's
 *     packets.h in refactor pass 2 (S12).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_GAME_REPLAY_H
#define DK_GAME_REPLAY_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_netsp.h"
#include "packet_data.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct CatalogueEntry;
struct Packet;

/** Where the replay decoder is in the packet file, within a code included (what a long turn's records are re-read from). */
struct PacketDecoderState {
    int64_t file_pos;
    uint64_t replay_file_pos;
    uint32_t bits_acc;
    int bits_count;
    uint32_t dec_zeros;
    unsigned char dec_lit[4];
    int dec_lit_pos;
    int dec_lit_len;
    uint32_t dec_raw_left;
    TbBool dec_reserved;
};

/**
 * Everything the replay reader carries from one turn to the next (upstream #5450's ReplayReaderPos, with what this
 * fork's reader adds): the decoder's place, the previous packets the next turn's residuals are against, the turn to
 * load next, the checksum still to verify and whether the file ends in an encoding this build doesn't know. What a
 * replay rewind keyframe keeps to read the file again from there.
 */
struct ReplayReaderPos {
    struct PacketDecoderState dec;
    struct Packet codec_prev[MAX_NET_USERS];
    GameTurn pckt_gameturn;
    TbBool checksum_pending;
    TbBigChecksum expected_checksum;
    TbBool ends_in_reserved;
};
/** Keeps where the reader is; false while a recorded resync waits to be applied at the end of the turn (there is
 *  then no place to come back to between two turns). */
TbBool replay_save_reader_pos(struct ReplayReaderPos *pos);
/** Puts the reader back where replay_save_reader_pos() kept it. False if the file can't be seeked there. */
TbBool replay_restore_reader_pos(const struct ReplayReaderPos *pos);
/** How many turns failed the replay's checksum since it was opened ("Out of sync"). */
uint64_t replay_checksum_mismatch_count(void);


TbBigChecksum compute_replay_integrity(void);
void post_init_packets(void);
TbBool setup_auto_replay_save(void);
TbBool open_new_packet_file_for_save(void);
void load_packets_for_turn(GameTurn nturn);
void verify_replay_checksum(void);
TbBool open_packet_file_for_load(char *fname, struct CatalogueEntry *centry);
int64_t save_packets(void);
void close_packet_file(void);
void replay_forget_saved_turn(void);
void replay_apply_pending_resync(void);
void replay_record_resync(const void *message, size_t message_size);
void replay_record_network_stopped(void);
void stop_replay_recording(const char *reason);
void replay_record_chat_message(NetUserId user, const char *message, MapCoord cursor_x, MapCoord cursor_y);
void replay_record_paused_action(NetUserId user, const struct Packet *pckt);
void set_replay_playback_paused(TbBool paused);
TbBool reinit_packets_after_load(void);
void disable_packet_mode(void);
TbBool verify_replay_map_checksums(void);
void restore_users_from_packet_save(void);
struct PacketSaveHead;
int64_t replay_open_turns_for_test(const char *fname, uint64_t data_offset, const struct PacketSaveHead *head);
void write_debug_packets(void);
void write_debug_screenpackets(void);
void dump_memory_to_file(const char * fname, const char * buf, size_t len);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
