/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file keeper_assist_outbox.h
 *     Sends the local player's combat assistant profile to the game (docs/newFeatures/llm/01 section 10).
 * @par Purpose:
 *     A player's standing orders reach the simulation the same way an agent's do: as PckA_Assist* packets in the
 *     player's own packet, so a replay and every machine agree. This queues the packets that make the profile
 *     (config_combat_assist.h) the player's live rules -- at the start of a level, after a load, and when the
 *     Options screen changes it -- and writes one a turn, only into a packet the player's own input left empty, so it
 *     never delays a click. It does nothing in a multiplayer game, a replay, the demo, or for a keeper played by an
 *     agent (an External seat) or by the built-in AI.
 */
/******************************************************************************/
#ifndef DK_KEEPER_ASSIST_OUTBOX_H
#define DK_KEEPER_ASSIST_OUTBOX_H

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Packet;

/** A new level, or a load: the profile is sent again (only what differs from the rules live then). */
void keeper_assist_outbox_reset(void);
/** The profile changed: plan again now. */
void keeper_assist_outbox_request(void);
/** Once a turn, after the local player's input: writes the next packet into `pckt` if it carries no action. */
void keeper_assist_outbox_tick(struct Packet *pckt, PlayerNumber plyr_idx);
/** Packets still to send. */
int64_t keeper_assist_outbox_pending(void);

#ifdef __cplusplus
}
#endif
#endif
