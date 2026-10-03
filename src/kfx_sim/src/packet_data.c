/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file packet_data.c
 *     struct Packet's storage and trivial accessors -- see packet_data.h.
 * @par Purpose:
 *     Moved down from kfx_net's packets.c/packets_misc.c/kfx_net_state.h
 *     (docs/refactor/todo/remove-symbol-level-layering-residuals.md):
 *     sim_packets[]/bad_packet and these four accessors never actually
 *     needed anything net-specific, just get_player() (kfx_sim) and
 *     bounds checks, so nothing about them justified kfx_sim/kfx_render
 *     reaching up into kfx_net to call them. kfx_net's own
 *     packets.c/packets_misc.c/net_exchange_gameplay.c keep writing into
 *     sim_packets[] directly for their own genuinely net-owned logic
 *     (checksums, wire buffer packing, turn-history exchange) -- that's a
 *     higher-ranked library writing into a lower-ranked library's state,
 *     which is fine; only the reverse (what this file replaces) was the
 *     violation.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "packet_data.h"

#include "globals.h"
#include "bflib_basics.h"
#include "player_data.h"
#include "camera_data.h"
#include "map_data.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Packet sim_packets[PACKETS_COUNT];
struct Packet bad_packet;

/**
 * Gives the network user id of the local player. Defined purely in terms
 * of kfx_sim's own state (my_player_number/PlayerInfo.user_id) rather
 * than reading kfx_net's netstate directly (kfx_net is above kfx_sim) --
 * player->user_id is kept correct in both modes: set to SOLO_HUMAN_ID by
 * stop_network_game_state() and to the real NetUserId by
 * setup_players_from_startup_packets(), so this needs no network-mode
 * branch of its own.
 * @return The local player's associated NetUserId.
 */
NetUserId get_local_user(void)
{
    return get_player(my_player_number)->user_id;
}

/**
 * Gives a pointer to the local player's packet.
 * @return Returns Packet pointer. On error, returns a dummy structure.
 */
struct Packet *get_local_packet(void)
{
    return get_packet(get_local_user());
}

/**
 * Gives a pointer to the packet of a given network user.
 * @param user Network user id. Note that it may differ from the player index.
 * @return Returns Packet pointer. On error, returns a dummy structure.
 */
struct Packet *get_packet(NetUserId user)
{
    if ((user < 0) || (user >= PACKETS_COUNT))
        return INVALID_PACKET;
    return &sim_packets[user];
}

void set_packet_action(struct Packet *pckt, unsigned char pcktype, int64_t par1, int64_t par2, int64_t par3, int64_t par4)
{
    pckt->actn_par1 = par1;
    pckt->actn_par2 = par2;
    pckt->actn_par3 = par3;
    pckt->actn_par4 = par4;
    pckt->action = pcktype;
}

void set_players_packet_action(struct PlayerInfo *player, unsigned char pcktype,
        uint64_t par1, uint64_t par2, int64_t par3, int64_t par4)
{
    struct Packet* pckt = get_packet(player->user_id);
    pckt->actn_par1 = par1;
    pckt->actn_par2 = par2;
    pckt->actn_par3 = par3;
    pckt->actn_par4 = par4;
    pckt->action = pcktype;
}

TbBool packet_action_has_camera_position(enum TbPacketAction action)
{
    // Some packets require an additional par3 or par4, replacing the usual camera coordinates sent on that turn.
    // (This is fine so long as such packets are occasional, the camera coordinates don't need to be exact.)
    
    switch (action)
    {
    case PckA_ApplyRoomspaceDigTag:
    case PckA_UsePwrOnThing:
    // In-game level editor verbs (docs/refactor/editor/): these carry real
    // per-action data in actn_par3/actn_par4 (model/owner/thing_idx/z/...),
    // read back by packets_cheats.c's dispatch switch. The camera-position
    // piggyback above (packet_set_camera_position(), called every turn from
    // exchange_packets() via camera_packet_set_state()) predates these
    // fork-only actions and doesn't know about them, so without this
    // exclusion it silently clobbers their actn_par3/actn_par4 with encoded
    // camera coordinates before the packet is processed -- same root-cause
    // class as the pos_x/pos_y clobbering documented next to
    // PckA_EditorRedoCreature and friends in packet_data.h, just via the
    // cam_x/cam_y union instead.
    case PckA_EditorPlaceObject:
    case PckA_EditorPlaceTerrainRect:
    case PckA_EditorRectSetOwner:
    case PckA_EditorSetThingPosition:
    case PckA_EditorRedoCreature:
    case PckA_EditorRedoDigger:
    case PckA_EditorRedoTrap:
    case PckA_EditorRedoDoor:
        return false;
    default:
        return true;
    }
}

// Some packets set the user's camera angle directly in par3.
// (Used where the action's effect depends on camera angle, e.g. power slap)
TbBool packet_action_has_camera_angle(const struct Packet *pckt)
{
    return (pckt->action == PckA_UsePwrOnThing) && (pckt->actn_par4 == CamIV_Isometric);
}

// shift that fits camera position in 16 bits.
static int64_t camera_position_shift(void)
{
    const int32_t max_coord = max(MAX_SUBTILES_X, MAX_SUBTILES_Y) * COORD_PER_STL - 1;
    int64_t shift = 0;
    while ((max_coord >> shift) >= UINT16_MAX)
        shift++;
    return shift;
}

void packet_set_camera_position(struct Packet *pckt, MapCoord x, MapCoord y)
{
    if (!packet_action_has_camera_position(pckt->action))
        return;
    const int64_t shift = camera_position_shift();
    pckt->cam_x = (uint16_t)((max(x, 0) >> shift) + 1);
    pckt->cam_y = (uint16_t)((max(y, 0) >> shift) + 1);
}

void packet_clear_camera_position(struct Packet *pckt)
{
    if (!packet_action_has_camera_position(pckt->action))
        return;
    pckt->cam_x = 0;
    pckt->cam_y = 0;
}

TbBool packet_get_camera_position(const struct Packet *pckt, MapCoord *x, MapCoord *y)
{
    if (!packet_action_has_camera_position(pckt->action))
        return false;
    if (pckt->cam_x == 0 || pckt->cam_y == 0)
        return false;
    const int64_t shift = camera_position_shift();
    const MapCoord half = (1 << shift) >> 1;
    *x = (((MapCoord)pckt->cam_x - 1) << shift) + half;
    *y = (((MapCoord)pckt->cam_y - 1) << shift) + half;
    return true;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
