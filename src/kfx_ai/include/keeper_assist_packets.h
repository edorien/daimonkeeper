/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file keeper_assist_packets.h
 *     How the combat assistant's rules travel: packets (docs/newFeatures/llm/02-rule-language.md section 8).
 * @par Purpose:
 *     A rule change reaches the simulation only as packets, so a replay and every machine in a game see the same rules.
 *     keeper_assist_plan_packets() turns a wanted rule set into the packets that make it live, sending only the slots
 *     that changed; keeper_assist_apply_packet() is what the game does with each one. Changes are written into the
 *     keeper's staging copy and go live together on the commit: a half-sent set is never live, and a rule that did not
 *     change keeps its cooldowns and budget.
 */
/******************************************************************************/
#ifndef DK_KEEPER_ASSIST_PACKETS_H
#define DK_KEEPER_ASSIST_PACKETS_H

#include "globals.h"
#include "keeper_assist_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

struct KeeperAssistState;

/** The globals, one per PckA_AssistStageGlobal. */
enum KeeperAssistGlobalField {
    KAG_None = 0,
    KAG_Enabled,
    KAG_Interval,
    KAG_ChargeTime,
    KAG_MinGap,
    KAG_Reserve,
    KAG_MaxSpend,
    KAG_Count,
};

/** One packet's action and the two parameters the assistant uses. */
struct KeeperAssistPacket {
    uint8_t action;  /**< PckA_Assist* */
    int32_t par1;
    int32_t par2;
};

/** Most packets one rule set needs: two per rule and one per condition, every global, and the commit. */
#define KEEPER_ASSIST_MAX_PACKETS (KEEPER_ASSIST_RULES * (2 + KEEPER_ASSIST_CONDS) + (KAG_Count - 1) + 1)

/** The packets that make `rules` (slot = index; `used` 0 = empty) and `globals` the keeper's live set, given its
 *  state `st`. Only changed slots are staged; the globals are sent whole if any of them changed. Returns the count, 0
 *  when nothing would change. `out` holds KEEPER_ASSIST_MAX_PACKETS. */
int keeper_assist_plan_packets(const struct KeeperAssistState *st, const struct KeeperAssistRule *rules,
    const struct KeeperAssistGlobals *globals, struct KeeperAssistPacket *out);

/** Applies one PckA_Assist* packet to a keeper's state (the caller has refused it in a multiplayer game). A rule that
 *  fails validation at the commit is dropped from the live set, as are globals out of range (the old ones stay). */
void keeper_assist_apply_packet(struct KeeperAssistState *st, uint8_t action, int32_t par1, int32_t par2);

/** Encoding helpers (tested on their own). */
int32_t keeper_assist_pack_rule(int slot, const struct KeeperAssistRule *r, int32_t *par2);
void keeper_assist_unpack_rule(int32_t par1, int32_t par2, int *slot, struct KeeperAssistRule *r);

#ifdef __cplusplus
}
#endif
#endif
