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

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct CatalogueEntry;

extern uint64_t initial_replay_seed;

TbBigChecksum compute_replay_integrity(void);
void post_init_packets(void);
TbBool setup_auto_replay_save(void);
TbBool open_new_packet_file_for_save(void);
void load_packets_for_turn(GameTurn nturn);
TbBool open_packet_file_for_load(char *fname, struct CatalogueEntry *centry);
int64_t save_packets(void);
void close_packet_file(void);
TbBool reinit_packets_after_load(void);
void disable_packet_mode(void);
void restore_users_from_packet_save(void);
void write_debug_packets(void);
void write_debug_screenpackets(void);
void dump_memory_to_file(const char * fname, const char * buf, size_t len);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
