// Game side of the raid scenario (docs/plans/llm-bridge-realtime.md, Phase 0): how fast the seat reacts to a raid, with no
// model in the loop. Two processes: this game (real time) and scripts/ai_bridge_raid_e2e.py running bridge.py's loop with
// the evacuate policy (policies.EvacuatePolicy: lift the imps near an intruder to the heart). Run it through
// scripts/run_ftest_ai_bridge_raid.sh; it sits in long_running_tests_list.
//
// The scene: the rival keeper is made an External seat, with four imps of its own on a claimed patch beside its heart, and
// a sealed pocket of its claimed floor 10 slabs away on the other side (walled in by earth no imp is marked to dig: an imp
// dropped there stays out of the raid's way -- the "safe" spot a trigger can lift imps to).
// Once the bridge has made its first decision (the seat's agent memory appears), and RAID_DELAY turns later, two hero
// knights appear on that patch: an intruder, then a fight. The raid's turn goes to FLAG2 on player 0, for the client to
// time its latency chain from. When the client signals done (FLAG0: 1 ok, 2 failed), this side reports how many of the
// four imps the raid killed. FLAG3 = 1 from the client before it connects asks for the Skirmish page's LLM Pause (the
// game may then be held for the agent's answers: docs/plans/llm-bridge-realtime.md, Phase 6). That number and the client's chain are the baseline every later phase must improve; neither
// is a pass/fail threshold here (a knight may kill an imp before any reaction could have saved it).
#include "ftest_ai_bridge_raid.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include <stdlib.h>
#include <json.h>
#include <json-dom.h>

#include "agent_memory.h"
#include "api_json_out.h"
#include "api_seat_diff.h"
#include "api_seat_view.h"
#include "bflib_datetm.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_terrain.h"
#include "creature_control.h"
#include "creature_states.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "player_data.h"
#include "slab_data.h"
#include "thing_creature.h"
#include "thing_list.h"
#include "thing_navigate.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAIT_LIMIT_MS 240000
#define RAID_DELAY 60       /**< turns after the bridge's first decision: the seat's first wake-ups are over by then */
#define IMPS 4
#define KNIGHTS 2

static TbClockMSec s_wait_started = 0;
static PlayerNumber P = -1;
static MapSlabCoord s_px, s_py;     /**< the imps' patch, its centre slab */
static ThingIndex s_imp[IMPS];
static GameTurn s_imp_born[IMPS];
static int64_t s_raid_turn = 0;     /**< 0: not yet planned; else the turn it comes (or came) */
static TbBool s_raided = false;

void ftest_ai_bridge_raid_pre_start() { fe_computer_players = 1; }

FTestActionResult raid_a01_prepare(struct FTestActionArgs* const args);
FTestActionResult raid_a02_raid_and_wait(struct FTestActionArgs* const args);

TbBool ftest_ai_bridge_raid_init()
{
    s_raid_turn = 0;
    s_raided = false;
    ftest_append_action(raid_a01_prepare, 0, NULL);
    ftest_append_action(raid_a02_raid_and_wait, 1, NULL);
    return true;
}

static struct Thing* creature_on_slab(const char* kind, PlayerNumber owner, MapSlabCoord sx, MapSlabCoord sy)
{
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, sx, sy);
    return ftest_util_create_creature(pos.x.val, pos.y.val, owner, 1, (ThingModel)creature_model_id(kind));
}

FTestActionResult raid_a01_prepare(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("could not make the rival an External seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    agent_memory_set(P, "", 0);
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSlabCoord hsx = subtile_slab(heart->mappos.x.stl.num), hsy = subtile_slab(heart->mappos.y.stl.num);
    get_players_dungeon(get_player(P))->total_money_owned = 20000;
    // The imps' patch: claimed floor east of the heart, joined to it.
    s_px = hsx + 6; s_py = hsy;
    ftest_util_replace_slabs(hsx + 2, hsy - 2, hsx + 8, hsy + 2, SlbT_CLAIMED, P);
    // The safe pocket: 3x3 claimed floor inside a ring of earth, west of the heart (east if the map edge is too near).
    const MapSlabCoord px = (hsx - 12 >= 1) ? (hsx - 10) : (hsx + 12);
    ftest_util_replace_slabs(px - 2, hsy - 2, px + 2, hsy + 2, SlbT_EARTH, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(px - 1, hsy - 1, px + 1, hsy + 1, SlbT_CLAIMED, P);
    for (int i = 0; i < IMPS; i++) {
        struct Thing* imp = creature_on_slab("IMP", P, s_px - 1 + (i % 2) * 2, s_py - 1 + (i / 2) * 2);
        if (thing_is_invalid(imp)) { FTEST_FAIL_TEST("could not make imp %d", i); return FTRs_Go_To_Next_Action; }
        s_imp[i] = imp->index;
        s_imp_born[i] = imp->creation_turn;
    }
    get_players_dungeon(get_player(my_player_number))->script_flags[1] = 1; // ready
    s_wait_started = LbTimerClock();
    return FTRs_Go_To_Next_Action;
}

static int imps_alive(void)
{
    int n = 0;
    for (int i = 0; i < IMPS; i++) {
        const struct Thing* t = thing_get(s_imp[i]);
        if (!thing_is_invalid(t) && thing_is_creature(t) && (t->creation_turn == s_imp_born[i]) && (t->owner == P)
            && !creature_is_dying(t)) n++;
    }
    return n;
}

/** What a get_player_view costs the game loop (the plan's Phase 1 polls one every few turns): building the view, then
 *  numbering it as a full view or diffing it against the last one, and serialising it, averaged over VIEW_TIMING_N. Run
 *  once the client is gone, since it replaces the seat's diff baseline. */
#define VIEW_TIMING_N 40
static void time_views(void)
{
    size_t full_bytes = 0, diff_bytes = 0;
    int64_t since = 0;
    const TbClockMSec t0 = LbTimerClock();
    for (int i = 0; i < VIEW_TIMING_N; i++) { VALUE v; api_seat_build_view(&v, P); value_fini(&v); }
    const TbClockMSec t1 = LbTimerClock();
    for (int i = 0; i < VIEW_TIMING_N; i++) {
        VALUE v; api_seat_build_view(&v, P);
        api_seat_finish_view(&v, P, false, 0);
        since = value_int64(value_dict_get(&v, "view_id"));
        char *msg = api_json_serialise(&v, 1024 * 1024, &full_bytes);
        free(msg);
        value_fini(&v);
    }
    const TbClockMSec t2 = LbTimerClock();
    for (int i = 0; i < VIEW_TIMING_N; i++) {
        VALUE v; api_seat_build_view(&v, P);
        api_seat_finish_view(&v, P, true, since);
        since = value_int64(value_dict_get(&v, "view_id"));
        char *msg = api_json_serialise(&v, 1024 * 1024, &diff_bytes);
        free(msg);
        value_fini(&v);
    }
    const TbClockMSec t3 = LbTimerClock();
    FTESTLOG("view timing over %d: build %.2f ms, build+full+serialise %.2f ms (%d B), build+diff+serialise %.2f ms (%d B, nothing changed)",
        VIEW_TIMING_N, (double)(t1 - t0) / VIEW_TIMING_N, (double)(t2 - t1) / VIEW_TIMING_N, (int)full_bytes,
        (double)(t3 - t2) / VIEW_TIMING_N, (int)diff_bytes);
}

FTestActionResult raid_a02_raid_and_wait(struct FTestActionArgs* const args)
{
    struct Dungeon* me = get_players_dungeon(get_player(my_player_number));
    const int64_t now = (int64_t)get_gameturn();
    if (me->script_flags[3] == 1) { extseat_allow_pause_with_human(true); me->script_flags[3] = 2; }
    size_t mem_len = 0;
    agent_memory_get(P, &mem_len);
    if ((s_raid_turn == 0) && (mem_len > 0)) s_raid_turn = now + RAID_DELAY; // the bridge has decided once
    if ((s_raid_turn > 0) && (now >= s_raid_turn) && !s_raided) {
        s_raided = true;
        s_raid_turn = now;
        // Beside the imps, wherever their work took them (else on their patch): on the seat's own ground.
        MapSlabCoord kx = s_px, ky = s_py;
        for (int i = 0; i < IMPS; i++) {
            const struct Thing* t = thing_get(s_imp[i]);
            if (thing_is_invalid(t) || (t->creation_turn != s_imp_born[i])) continue;
            const MapSlabCoord sx = subtile_slab(t->mappos.x.stl.num), sy = subtile_slab(t->mappos.y.stl.num);
            if (slabmap_owner(get_slabmap_block(sx, sy)) == P) { kx = sx; ky = sy; break; }
        }
        for (int k = 0; k < KNIGHTS; k++) {
            struct Thing* knight = creature_on_slab("KNIGHT", PLAYER_GOOD, kx + k, ky);
            if (thing_is_invalid(knight)) knight = creature_on_slab("KNIGHT", PLAYER_GOOD, s_px + k, s_py);
            if (thing_is_invalid(knight)) { FTEST_FAIL_TEST("could not make knight %d", k); return FTRs_Go_To_Next_Action; }
            set_creature_level(knight, 3);
        }
        me->script_flags[2] = s_raid_turn;
        FTESTLOG("raid at turn %d: %d knights at slab (%d,%d), %d imps alive", (int)s_raid_turn, KNIGHTS, (int)kx, (int)ky, imps_alive());
    }
    const int64_t done = me->script_flags[0];
    if (done == 2) { FTEST_FAIL_TEST("the bridge client reported failure (see its output)"); return FTRs_Go_To_Next_Action; }
    if (done != 1) {
        if ((int64_t)(LbTimerClock() - s_wait_started) > WAIT_LIMIT_MS) { FTEST_FAIL_TEST("the bridge client did not finish within %" PRId64 " ms", (int64_t)WAIT_LIMIT_MS); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    if (!s_raided) { FTEST_FAIL_TEST("the raid never happened (the bridge never wrote the seat's memory)"); return FTRs_Go_To_Next_Action; }
    const int alive = imps_alive();
    FTESTLOG("raid result: %d of %d imps lost (raid at turn %d, now %d)", IMPS - alive, IMPS, (int)s_raid_turn, (int)now);
    time_views();
    FTESTLOG("Test passed: the raid scenario ran; the client printed its latency chain");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
