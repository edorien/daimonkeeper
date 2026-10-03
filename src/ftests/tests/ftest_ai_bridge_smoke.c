// Game side of the external-process smoke test (docs/refactor/AI/LLM/05-testing-and-rollout.md section 1.2 step 5).
// Not a standalone test: it needs scripts/ai_bridge_smoke.py talking to the in-game TCP API, so it sits in
// long_running_tests_list (needs -includelongtests) and is run through scripts/run_ftest_ai_bridge_smoke.sh.
//
// It only prepares the world and hands over: gives the rival keeper stock, a creature, a power, gold and a
// claimed corridor for a door; then pauses the game as the agent's pause and waits. The Python client claims
// the seat, plays, and signals with script flags on PLAYER0 -- FLAG1 = ready (set here), FLAG0 = 1 done / 2 failed.
// FLAG2 asks this side to save (1) and then load that save (2), which only the game itself can do: the client checks
// the GAME_SAVED / GAME_LOADED events it gets and that its agent memory comes back as saved (09-persistent-memory.md).
// All assertions about what the agent sees and does live in the client; this side fails only if the client never
// finishes or reports failure.
#include "ftest_ai_bridge_smoke.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "bflib_datetm.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_saves.h"
#include "kfx_sim_state.h"
#include "player_data.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAIT_LIMIT_MS 180000

static TbClockMSec s_wait_started = 0;
// Not in the dungeon's script flags: those come back from the save as they were when it was made.
static int s_saveload_phase = 0;
#define SAVELOAD_SLOT 7

void ftest_ai_bridge_smoke_pre_start()
{
    fe_computer_players = 1;
}

FTestActionResult bridge_a01_prepare_and_hand_over(struct FTestActionArgs* const args);
FTestActionResult bridge_a02_wait_for_client(struct FTestActionArgs* const args);

TbBool ftest_ai_bridge_smoke_init()
{
    s_saveload_phase = 0;
    ftest_append_action(bridge_a01_prepare_and_hand_over, 0, NULL);
    ftest_append_action(bridge_a02_wait_for_client, 1, NULL);
    return true;
}

FTestActionResult bridge_a01_prepare_and_hand_over(struct FTestActionArgs* const args)
{
    PlayerNumber rival = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
    {
        if (p == my_player_number || p == PLAYER_GOOD || p == PLAYER_NEUTRAL) continue;
        if (player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { rival = p; break; }
    }
    if (rival < 0) { FTEST_FAIL_TEST("no rival keeper"); return FTRs_Go_To_Next_Action; }
    FTESTLOG("rival keeper (the seat the client will claim): player %" PRId64, (int64_t)rival);

    const struct Thing* heart = find_players_dungeon_heart(rival);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    struct Dungeon* d = get_players_dungeon(get_player(rival));

    struct Thing* c = ftest_util_create_creature(subtile_coord_center(hx - 3), subtile_coord_center(hy), rival, 9, (ThingModel)creature_model_id("ORC"));
    if (thing_is_invalid(c)) { FTEST_FAIL_TEST("no creature"); return FTRs_Go_To_Next_Action; }
    set_trap_buildable_and_add_to_amount(rival, (ThingModel)trap_model_id("BOULDER"), 1, 2);
    set_door_buildable_and_add_to_amount(rival, (ThingModel)door_model_id("WOOD"), 1, 2);
    d->magic_level[PwrK_CALL2ARMS] = 1;
    d->total_money_owned += 5000;
    ftest_util_replace_slabs(subtile_slab(hx), subtile_slab(hy) + 2, subtile_slab(hx), subtile_slab(hy) + 2, SlbT_CLAIMED, rival);
    const MapSlabCoord dsx = subtile_slab(hx) + 4, dsy = subtile_slab(hy);
    ftest_util_replace_slabs(dsx - 1, dsy, dsx + 1, dsy, SlbT_CLAIMED, rival);
    ftest_util_replace_slabs(dsx - 1, dsy - 1, dsx + 1, dsy - 1, SlbT_WALLDRAPE, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(dsx - 1, dsy + 1, dsx + 1, dsy + 1, SlbT_WALLDRAPE, PLAYER_NEUTRAL);

    // Scene for the area verbs (relative to the heart, in slabs): floor to build a room on and a second patch for the
    // cancelled build, earth to dig. The client derives the same coordinates from the heart it can see.
    const MapSlabCoord hsx = subtile_slab(hx), hsy = subtile_slab(hy);
    d->room_buildable[get_rid(room_desc, "TREASURE")] |= 1;
    ftest_util_replace_slabs(hsx + 2, hsy + 3, hsx + 7, hsy + 7, SlbT_CLAIMED, rival);
    ftest_util_replace_slabs(hsx + 8, hsy + 3, hsx + 11, hsy + 6, SlbT_CLAIMED, rival);
    ftest_util_replace_slabs(hsx - 9, hsy - 1, hsx - 6, hsy + 1, SlbT_EARTH, PLAYER_NEUTRAL);

    extseat_pause();
    get_players_dungeon(get_player(my_player_number))->script_flags[1] = 1; // ready
    s_wait_started = LbTimerClock();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult bridge_a02_wait_for_client(struct FTestActionArgs* const args)
{
    const int64_t done = get_players_dungeon(get_player(my_player_number))->script_flags[0];
    const int64_t saveload = get_players_dungeon(get_player(my_player_number))->script_flags[2];
    if ((s_saveload_phase == 0) && (saveload == 1))
    {
        s_saveload_phase = 1;
        fill_game_catalogue_slot(SAVELOAD_SLOT, "ai_bridge_smoke");
        const TbBool paused = flag_is_set(kfx_sim_state.operation_flags, GOF_Paused);
        set_flag(kfx_sim_state.operation_flags, GOF_Paused);
        if (!save_game(SAVELOAD_SLOT))
            FTESTLOG("save for the client failed");
        if (!paused)
            clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
        return FTRs_Repeat_Current_Action;
    }
    if ((s_saveload_phase == 1) && (saveload == 2))
    {
        s_saveload_phase = 2;
        if (!load_game(SAVELOAD_SLOT))
            FTESTLOG("load for the client failed");
        return FTRs_Repeat_Current_Action;
    }
    if (done == 1)
    {
        // The one thing the client can't see over the API: its claim_seat/release_seat of the local human's own
        // slot (M10) gave it back to the human, not to the built-in AI.
        const struct PlayerInfo* me = get_player(my_player_number);
        if (flag_is_set(me->allocflags, PlaF_CompCtrl) || flag_is_set(me->allocflags, PlaF_ExternalSeat) || (me->user_id != SOLO_HUMAN_ID))
        {
            FTEST_FAIL_TEST("the local human's slot was not handed back to the human after the client released it");
            return FTRs_Go_To_Next_Action;
        }
        FTESTLOG("Test passed: the external client finished its session");
        return FTRs_Go_To_Next_Action;
    }
    if (done == 2)
    {
        FTEST_FAIL_TEST("the external client reported failure (see its output)");
        return FTRs_Go_To_Next_Action;
    }
    if ((int64_t)(LbTimerClock() - s_wait_started) > WAIT_LIMIT_MS)
    {
        FTEST_FAIL_TEST("the external client did not finish within %" PRId64 " ms", (int64_t)WAIT_LIMIT_MS);
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Repeat_Current_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
