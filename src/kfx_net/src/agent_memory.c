/******************************************************************************/
/** @file agent_memory.c
 *     What an External seat's agent keeps about the game it is playing, stored for it by the engine.
 * @par Purpose:
 *     See agent_memory.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "agent_memory.h"
#include <stdlib.h>
#include <string.h>
#include "player_data.h"
#include "post_inc.h"

// Process-global, like the seat's chat log: never part of a kfx_*_state struct, so it has no effect on saves' fixed
// chunks, on sync or on replays. It reaches save files only through the optional AGNT chunk.
static char *s_data[PLAYERS_COUNT];
static size_t s_len[PLAYERS_COUNT];

TbBool agent_memory_set(PlayerNumber plyr_idx, const char *data, size_t len)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT) || (len > AGENT_MEMORY_MAX) || ((len > 0) && (data == NULL))) {
        return false;
    }
    char *copy = NULL;
    if (len > 0) {
        copy = (char *)malloc(len + 1);
        if (copy == NULL) {
            return false;
        }
        memcpy(copy, data, len);
        copy[len] = '\0';
    }
    free(s_data[plyr_idx]);
    s_data[plyr_idx] = copy;
    s_len[plyr_idx] = len;
    return true;
}

const char *agent_memory_get(PlayerNumber plyr_idx, size_t *len)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT) || (s_data[plyr_idx] == NULL)) {
        if (len != NULL) *len = 0;
        return NULL;
    }
    if (len != NULL) *len = s_len[plyr_idx];
    return s_data[plyr_idx];
}

void agent_memory_clear_all(void)
{
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        free(s_data[p]);
        s_data[p] = NULL;
        s_len[p] = 0;
    }
}

static void put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static uint32_t get_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t agent_memory_serialise(char **out)
{
    *out = NULL;
    uint32_t count = 0;
    size_t total = 4;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        if (s_data[p] != NULL) {
            count++;
            total += 8 + s_len[p];
        }
    }
    if (count == 0) {
        return 0;
    }
    unsigned char *buf = (unsigned char *)malloc(total);
    if (buf == NULL) {
        return 0;
    }
    put_u32(buf, count);
    size_t at = 4;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        if (s_data[p] == NULL) {
            continue;
        }
        put_u32(buf + at, (uint32_t)(int32_t)p);
        put_u32(buf + at + 4, (uint32_t)s_len[p]);
        memcpy(buf + at + 8, s_data[p], s_len[p]);
        at += 8 + s_len[p];
    }
    *out = (char *)buf;
    return total;
}

TbBool agent_memory_deserialise(const char *buf, size_t len)
{
    agent_memory_clear_all();
    if ((buf == NULL) || (len == 0)) {
        return true;
    }
    const unsigned char *b = (const unsigned char *)buf;
    if (len < 4) {
        return false;
    }
    const uint32_t count = get_u32(b);
    if (count > PLAYERS_COUNT) {
        return false;
    }
    size_t at = 4;
    for (uint32_t i = 0; i < count; i++) {
        if (len - at < 8) {
            agent_memory_clear_all();
            return false;
        }
        const int32_t plyr = (int32_t)get_u32(b + at);
        const uint32_t n = get_u32(b + at + 4);
        at += 8;
        if ((plyr < 0) || (plyr >= PLAYERS_COUNT) || (n > AGENT_MEMORY_MAX) || (n > len - at)
         || !agent_memory_set((PlayerNumber)plyr, buf + at, n)) {
            agent_memory_clear_all();
            return false;
        }
        at += n;
    }
    if (at != len) {
        agent_memory_clear_all();
        return false;
    }
    return true;
}
