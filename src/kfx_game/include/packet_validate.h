/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file packet_validate.h
 *     What a packet may ask for, checked before the game acts on it.
 * @par Purpose:
 *     Every packet reaches the game through process_user_packet(), whether it came from the keyboard, a network peer,
 *     a replay file or an agent. The values in it are untrusted: an action number the game does not know, a thing,
 *     player, event or kind index outside its table, a map position off the map. packet_check() judges one packet
 *     against a table that says, for each action, what its four parameters are; the one that fails drops the action
 *     (the same on every machine, so a lock-step game stays in step). See docs/security-pass (S05).
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_PACKET_VALIDATE_H
#define DK_PACKET_VALIDATE_H

#include "bflib_basics.h"
#include "globals.h"
#include "packet_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What an action parameter is. */
enum PacketParamKind {
    PK_ANY = 0,        /**< Not constrained here (a flag, a mode, a number the handler range-checks itself). */
    PK_PLAYER,         /**< A player number, 0 .. PLAYERS_COUNT-1. */
    PK_PLAYER_LOW8,    /**< Packed: the low byte is a player number (the rest is another value). */
    PK_THING,          /**< A thing index, 0 .. THINGS_COUNT-1 (0 is "none"). */
    PK_EVENT,          /**< An event slot, 0 .. EVENTS_COUNT-1. */
    PK_ROOM,           /**< A room index. */
    PK_BATTLE,         /**< A battle index. */
    PK_SUBTILE_X,      /**< A subtile coordinate on the map. */
    PK_SUBTILE_Y,
    PK_MAPCOORD_X,     /**< A map coordinate (a subtile times 256) on the map. */
    PK_MAPCOORD_Y,
    PK_SLAB,           /**< A slab kind of the loaded configuration. */
    PK_CREATURE,       /**< A creature model. */
    PK_TRAP,           /**< A trap kind. */
    PK_DOOR,           /**< A door kind. */
    PK_POWER,          /**< A power kind. */
    PK_SPELL,          /**< A spell kind. */
    PK_INSTANCE,       /**< A creature instance. */
    PK_LEVEL,          /**< A creature level, 0 .. CREATURE_MAX_LEVEL-1. */
    PK_ROOMSPACE_SIZE, /**< The side of a room-sized box, 0 .. MAX_ROOMSPACE_WIDTH-1. */
    PK_COMPUTER_ASSIST,/**< A computer-assist type, 1 .. COMPUTER_ASSIST_TYPES_COUNT. */
};

/** What the game currently allows for each kind of value. */
struct PacketLimits {
    int64_t things;
    int64_t events;
    int64_t rooms;
    int64_t battles;
    int64_t players;
    int64_t subtiles_x;
    int64_t subtiles_y;
    int64_t slab_kinds;
    int64_t creature_models;
    int64_t trap_kinds;
    int64_t door_kinds;
    int64_t power_kinds;
    int64_t spell_kinds;
    int64_t instances;
    int64_t levels;
    int64_t roomspace_size;
    int64_t computer_assist_types;
};

enum PacketVerdict {
    PKT_OK = 0,
    PKT_UNKNOWN_ACTION,   /**< Not an action this game has (an unused number, a network message type, past the last). */
    PKT_CHEAT_REFUSED,    /**< A cheat, editor or assist action in a game that refuses cheats (multiplayer). */
    PKT_NOT_ALLOWED_HERE, /**< A host-only action from another peer. */
    PKT_BAD_PARAMETER,    /**< A parameter outside what its kind allows; `*bad_param` says which (1-4). */
};

/** The limits of the game that is running (the loaded configuration and the map). */
void packet_limits_current(struct PacketLimits *limits);

/**
 * Judges one packet's action and parameters. `cheats_refused`: this is a game in which cheat and editor actions are
 * not accepted (multiplayer). `from_host`: the packet is the host's (or the only human's). Does not look at the
 * game state and does not change the packet.
 */
enum PacketVerdict packet_check(const struct PacketLimits *limits, const struct Packet *pckt, TbBool cheats_refused,
    TbBool from_host, int *bad_param);

/**
 * packet_check() for the running game, applied: a packet that fails loses its action (it becomes PckA_None, so what
 * the player did with the mouse still counts and the action does not), and the reason goes to the sync log.
 * @return the verdict; PKT_OK means the packet was kept as it was.
 */
enum PacketVerdict packet_validate_for_user(NetUserId user, struct Packet *pckt);

#ifdef __cplusplus
}
#endif

#endif
