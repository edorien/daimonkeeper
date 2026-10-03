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
#include "kfx_sim_state.h"
#include "map_data.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Packet sim_packets[PACKETS_COUNT];
struct Packet bad_packet;

/**
 * Gives the NetUserId THIS machine's own input device writes to.
 *
 * For a genuinely local game (not networked, not replaying a packet file) this is always SOLO_HUMAN_ID,
 * full stop -- independent of PlayerInfo::user_id, which net_add_external_seat() can now reassign on purpose
 * (a campaign/scenario seat handed to an agent): front_input.c's writes (get_local_packet(), 90+ call sites)
 * must keep going to the local human's own slot regardless of who is currently driving the dungeon they're
 * watching. This used to just read player->user_id directly -- safe only because nothing had ever reassigned
 * my_player_number's own user_id away from SOLO_HUMAN_ID before.
 *
 * The networked case is unaffected (my_local_user_id, mirrored by setup_players_from_startup_packets()
 * alongside PlayerInfo::user_id there, same as before this existed): a real multiplayer session's local user
 * id genuinely isn't always 0, and nothing there ever reassigns it independently the way net_add_external_seat
 * does for a local game.
 *
 * kfx_net_state.packet_load_enable (replay) can't be checked here -- kfx_net is above kfx_sim -- so replay
 * also takes the SOLO_HUMAN_ID branch; the one thing that costs is get_local_user()-derived effects (palette,
 * lightning) not following cycle_replay_player()'s Tab-cycling to a different recorded player's perspective,
 * a purely cosmetic replay-review detail, not a live-game concern.
 * @return This machine's own NetUserId.
 */
NetUserId get_local_user(void)
{
    if (!network_is_active()) {
        return SOLO_HUMAN_ID;
    }
    return my_local_user_id;
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

// "For player" writer: always means "my own local input" (see get_players_own_packet's comment in
// packets_misc.c for why player->user_id alone stopped being safe once net_add_external_seat() could
// reassign my_player_number's own user_id).
void set_players_packet_action(struct PlayerInfo *player, unsigned char pcktype,
        uint64_t par1, uint64_t par2, int64_t par3, int64_t par4)
{
    struct Packet* pckt = (player->id_number == my_player_number) ? get_local_packet() : get_packet(player->user_id);
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
