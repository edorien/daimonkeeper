/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_commands.h
 *     Header file for game_commands.c, game_commands_input.c and
 *     game_commands_cheats.c.
 * @par Purpose:
 *     Applying the players' packets to the game, moved from kfx_net's
 *     packets.h in refactor pass 2 (S12).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_GAME_COMMANDS_H
#define DK_GAME_COMMANDS_H

#include "bflib_basics.h"
#include "globals.h"
#include "packet_data.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct PlayerInfo;
struct Thing;

/* game_commands.c */
void process_packets(void);
void process_user_packet(NetUserId user);
TbBool process_user_global_packet_action(NetUserId user);
void process_user_dungeon_control_packet_control(NetUserId user);
TbBool process_user_dungeon_control_packet_action(NetUserId user);
void process_user_creature_control_packet_control(NetUserId user);
void process_user_creature_passenger_packet_action(NetUserId user);
void process_user_creature_control_packet_action(NetUserId user);
void process_user_map_packet_control(NetUserId user);
void process_map_packet_clicks(NetUserId user);
void update_double_click_detection(NetUserId user);
TbBool process_dungeon_control_packet_spell_overcharge(NetUserId user);
void update_box_lag_compensation(struct PlayerInfo* player);

/* game_commands_input.c */
TbBool is_mouse_on_map(struct Packet* pckt);
void remember_cursor_subtile(NetUserId user);
struct Thing *get_thing_under_hand(struct PlayerInfo *player, MapCoord x, MapCoord y);
TbBool process_dungeon_control_packet_clicks(NetUserId user);

/* game_commands_cheats.c */
TbBool packets_process_cheats(NetUserId user, PlayerNumber plyr_idx, MapCoord x, MapCoord y,
    struct Packet* pckt, MapSubtlCoord stl_x, MapSubtlCoord stl_y, MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool process_user_global_cheats_packet_action(NetUserId user, struct Packet* pckt);
TbBool process_players_dungeon_control_cheats_packet_action(PlayerNumber plyr_idx, struct Packet* pckt);
// The editor's Fill tool (PckA_EditorFloodFill), callable directly so a test
// can drive it: the packet carries the seed only in the ambient cursor field.
void editor_flood_fill_terrain(MapSlabCoord seed_x, MapSlabCoord seed_y, SlabKind target_kind, PlayerNumber owner);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
