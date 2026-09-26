/**
 * @file ftest_packet_inject.h
 * @brief Lets a functional test write a human-shaped command packet -- action,
 * ambient position, control flags, cursor context -- for one game turn.
 *
 * Why this exists: an ftest action runs from ftest_update(), *before* input()
 * in gameplay_loop_logic(), and input() rewrites the local packet's pos_x/pos_y
 * and control flags from the (headless) cursor every real turn -- see the
 * editor_paint_terrain entry in src/ftests/README.md. So an action can queue a
 * one-shot PckA_* action, but cannot drive the ambient position + PCtr_LBtn*
 * fields that the click/drag verbs (build room, dig, sell, slap, cast on a
 * subtile, place trap/door) are dispatched from.
 *
 * Injection point: gameplay_loop_logic() calls ftest_packet_inject_tick() right
 * after exchange_packets() (next to ftest_packet_capture_tick()) and before
 * process_packets() consumes the packet, so the packet a test queued is exactly
 * what the sim dispatches that turn -- the same determinism boundary a real
 * input device, or an external agent seat, writes to.
 *
 * A request applies for exactly one turn (the turn it was queued in) and is
 * then discarded; queue again next turn to continue a multi-turn gesture.
 */
#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#include "packet_data.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FtestPacketInject {
    unsigned char action;        /* PckA_*, PckA_None for none */
    int64_t par1, par2, par3, par4;
    TbBool has_position;         /* if true: pos_x/pos_y + PCtr_MapCoordsValid + context, and PCtr_Gui cleared */
    int64_t pos_x, pos_y;        /* map coordinates (coord units, like Coord3d .val), not subtiles */
    unsigned char context;       /* cursor state (CSt_*) carried in additional_packet_values */
    uint64_t control_flags;      /* PCtr_* bits ORed onto the packet */
};

/** Queue `req` for `user`'s packet on the current game turn. Replaces an earlier request for the same user. */
void ftest_packet_inject_queue(NetUserId user, const struct FtestPacketInject *req);
/** Convenience for the local human's packet. */
void ftest_packet_inject_queue_local(const struct FtestPacketInject *req);
/** Drop everything queued. */
void ftest_packet_inject_reset(void);
/** Called once per turn from gameplay_loop_logic(), after exchange_packets(). */
void ftest_packet_inject_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* FUNCTESTING */
