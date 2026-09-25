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
/******************************************************************************/
#ifdef __cplusplus
}
#endif
