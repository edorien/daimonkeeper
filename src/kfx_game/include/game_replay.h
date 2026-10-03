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

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct CatalogueEntry;
struct Packet;


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
void write_debug_packets(void);
void write_debug_screenpackets(void);
void dump_memory_to_file(const char * fname, const char * buf, size_t len);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
