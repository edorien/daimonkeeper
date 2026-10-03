/******************************************************************************/
/** @file agent_memory.h
 *     What an External seat's agent keeps about the game it is playing, stored for it by the engine.
 * @par Purpose:
 *     One opaque text per player, set and read through the API (set_agent_memory / get_agent_memory) and
 *     written into save files as the optional AGNT chunk, so a loaded save brings back the agent's plan as
 *     it stood when the game was saved (docs/refactor/AI/omissions/09-persistent-memory.md section 4).
 *     The engine never parses it and the simulation never reads it: it is not synced, replayed or hashed.
 */
/******************************************************************************/
#ifndef DK_AGENT_MEMORY_H
#define DK_AGENT_MEMORY_H

#include <stddef.h>
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Largest text one player may keep. */
#define AGENT_MEMORY_MAX 65536
/** Version of the AGNT save chunk's payload. */
#define AGENT_MEMORY_CHUNK_VER 1

/** Replaces the player's text (len 0 clears it). False, leaving it unchanged, for a bad player or a text over
 *  AGENT_MEMORY_MAX. */
TbBool agent_memory_set(PlayerNumber plyr_idx, const char *data, size_t len);
/** The player's text and its length, or NULL when there is none. */
const char *agent_memory_get(PlayerNumber plyr_idx, size_t *len);
/** Forgets every player's text: a new level starts. */
void agent_memory_clear_all(void);
/** The AGNT chunk payload for every player holding a text (u32 count, then per entry i32 player, u32 len, bytes;
 *  little-endian), in a malloc'd buffer the caller frees. Returns its length; 0 (and *out NULL) when nobody holds one. */
size_t agent_memory_serialise(char **out);
/** Replaces every player's text with a chunk payload. A NULL or empty payload clears them all; a malformed one (a
 *  count or length past the end, a bad player, a text over the cap) clears them all and returns false. */
TbBool agent_memory_deserialise(const char *buf, size_t len);

#ifdef __cplusplus
}
#endif
#endif
