/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file keeper_assist_outbox.c
 *     Sends the local player's combat assistant profile to the game (keeper_assist_outbox.h).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "keeper_assist_outbox.h"

#include <string.h>

#include "bflib_basics.h"
#include "cheat_mode.h"
#include "config_combat_assist.h"
#include "config_keeperfx.h"
#include "keeper_assist_packets.h"
#include "keeper_assist_state.h"
#include "kfx_sim_state.h"
#include "packet_data.h"
#include "player_data.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static struct KeeperAssistPacket s_queue[KEEPER_ASSIST_MAX_PACKETS];
static int s_count = 0, s_next = 0;
static TbBool s_planned = false;   /**< the profile was planned for this level */
static TbBool s_dirty = false;     /**< the profile changed since */
static TbBool s_presets_loaded = false;
static VALUE s_presets;

void keeper_assist_outbox_reset(void)
{
    s_count = s_next = 0;
    s_planned = false;
    s_dirty = false;
}

void keeper_assist_outbox_request(void)
{
    s_dirty = true;
}

int64_t keeper_assist_outbox_pending(void)
{
    return s_count - s_next;
}

static void plan(PlayerNumber plyr_idx)
{
    s_count = s_next = 0;
    if (!s_presets_loaded) {
        keeper_assist_load_presets(NULL, &s_presets);
        s_presets_loaded = true;
    }
    const struct KeeperAssistProfile *profile = keeper_assist_profile();
    const struct KeeperAssistState *st = &kfx_sim_state.keeper_assist[plyr_idx];
    if (!profile->globals.enabled) {
        // Off: a player who never uses the assistant sends nothing at all; one who switches it off sends just that.
        if (st->globals.enabled) {
            s_queue[0].action = PckA_AssistEnable;
            s_queue[0].par1 = 0;
            s_queue[0].par2 = 0;
            s_count = 1;
        }
        return;
    }
    struct KeeperAssistRule rules[KEEPER_ASSIST_RULES];
    struct KeeperAssistGlobals globals;
    const char *err = keeper_assist_profile_expand(profile, &s_presets, rules, &globals);
    if (err != NULL) {
        WARNLOG("Combat assistant profile: %s; no rules sent", err);
        memset(rules, 0, sizeof(rules));
        globals.enabled = 0;
    }
    s_count = keeper_assist_plan_packets(&kfx_sim_state.keeper_assist[plyr_idx], rules, &globals, s_queue);
    if (s_count > 0) {
        SYNCDBG(7, "Combat assistant: %d packet(s) to send for player %d", s_count, (int)plyr_idx);
    }
}

void keeper_assist_outbox_tick(struct Packet *pckt, PlayerNumber plyr_idx)
{
    if (game_refuses_cheats() || replay.load_enable || ((kfx_sim_state.mode_flags & MFlg_IsDemoMode) != 0)) {
        s_count = s_next = 0;
        return;
    }
    const struct PlayerInfo *player = get_player(plyr_idx);
    if (player_invalid(player) || !player_exists(player) || !is_active_keeper(player)
     || flag_is_set(player->allocflags, PlaF_ExternalSeat) || flag_is_set(player->allocflags, PlaF_CompCtrl)) {
        return; // an agent or the built-in AI plays this keeper: its rules are not the player's to send
    }
    if (!s_planned || s_dirty) {
        s_planned = true;
        s_dirty = false;
        plan(plyr_idx);
    }
    if ((s_next >= s_count) || (pckt->action != PckA_None)) {
        return; // nothing to send, or the player's own action goes first
    }
    const struct KeeperAssistPacket *k = &s_queue[s_next++];
    set_packet_action(pckt, k->action, k->par1, k->par2, 0, 0);
}

#ifdef __cplusplus
}
#endif
