// Game side of the agent-vs-agent end-to-end test (scripts/llm_bridge/agent_vs_agent.py, docs/refactor/AI/LLM/01
// section "Agent-vs-agent harness"). Two External seats made here the way the Skirmish page would
// (net_add_external_seat), each given its own separate build/dig scene and its own periodic fight event, so the
// test can tell whether the harness's single TCP connection is correctly dispatching each DECISION_DUE by its
// "player" field rather than mixing the two seats up. Run through scripts/run_ftest_ai_bridge_vs_agent.sh; it
// sits in long_running_tests_list.
#include "ftest_ai_bridge_vs_agent.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "bflib_datetm.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "config_terrain.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_sim_state.h"
#include "map_events.h"
#include "net_game.h"
#include "player_data.h"
#include "room_data.h"
#include "slab_data.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAIT_LIMIT_MS 240000
#define SEAT_COUNT 2

static TbClockMSec s_wait_started = 0;
static PlayerNumber s_seat[SEAT_COUNT] = { -1, -1 };
static int64_t s_next_fight_turn[SEAT_COUNT] = { 0, 0 };

void ftest_ai_bridge_vs_agent_pre_start() { fe_computer_players = 1; }

FTestActionResult va01_prepare(struct FTestActionArgs* const args);
FTestActionResult va02_wait_and_verify(struct FTestActionArgs* const args);

TbBool ftest_ai_bridge_vs_agent_init()
{
    s_next_fight_turn[0] = 0;
    s_next_fight_turn[1] = 0;
    ftest_append_action(va01_prepare, 0, NULL);
    ftest_append_action(va02_wait_and_verify, 1, NULL);
    return true;
}

FTestActionResult va01_prepare(struct FTestActionArgs* const args)
{
    int64_t found = 0;
    for (PlayerNumber p = 0; (p < PLAYERS_COUNT) && (found < SEAT_COUNT); p++)
    {
        if (p == my_player_number || p == PLAYER_GOOD || p == PLAYER_NEUTRAL || !player_exists(get_player(p)) || thing_is_invalid(find_players_dungeon_heart(p)))
            continue;
        s_seat[found] = p;
        found++;
    }
    if (found < SEAT_COUNT) { FTEST_FAIL_TEST("could not find %" PRId64 " rival seats (found %" PRId64 ")", (int64_t)SEAT_COUNT, found); return FTRs_Go_To_Next_Action; }

    for (int64_t i = 0; i < SEAT_COUNT; i++)
    {
        const PlayerNumber P = s_seat[i];
        if (net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("could not make player %" PRId64 " an External seat", (int64_t)P); return FTRs_Go_To_Next_Action; }
        ftest_util_reveal_map(P);
        const struct Thing* heart = find_players_dungeon_heart(P);
        const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
        const MapSlabCoord hsx = subtile_slab(hx), hsy = subtile_slab(hy);
        struct Dungeon* d = get_players_dungeon(get_player(P));
        d->total_money_owned = 100000;
        d->room_buildable[get_rid(room_desc, "TREASURE")] |= 1;
        ftest_util_replace_slabs(hsx + 4, hsy - 2, hsx + 8, hsy + 2, SlbT_CLAIMED, P);
        ftest_util_replace_slabs(hsx - 9, hsy - 3, hsx - 5, hsy + 1, SlbT_EARTH, PLAYER_NEUTRAL);
    }
    get_players_dungeon(get_player(my_player_number))->script_flags[1] = 1; // ready
    s_wait_started = LbTimerClock();
    return FTRs_Go_To_Next_Action;
}

static int64_t treasure_slabs(PlayerNumber P)
{
    const RoomKind treasure = (RoomKind)get_rid(room_desc, "TREASURE");
    int64_t n = 0;
    for (MapSlabCoord y = 0; y < kfx_sim_state.map_tiles_y; y++)
        for (MapSlabCoord x = 0; x < kfx_sim_state.map_tiles_x; x++)
        {
            const struct SlabMap* slb = get_slabmap_block(x, y);
            if (slb->room_index > 0 && room_get(slb->room_index)->kind == treasure && slabmap_owner(slb) == P) n++;
        }
    return n;
}

static int64_t dig_marks(PlayerNumber P)
{
    const struct Dungeon* d = get_players_dungeon(get_player(P));
    int64_t n = 0;
    for (int64_t i = 0; i < MAPTASKS_COUNT; i++) if (d->task_list[i].kind != 0) n++;
    return n;
}

FTestActionResult va02_wait_and_verify(struct FTestActionArgs* const args)
{
    // FLAG1: 1 = scene ready, 2 = the client has finished the harness phase.
    const int64_t phase = get_players_dungeon(get_player(my_player_number))->script_flags[1];
    for (int64_t i = 0; (phase == 1) && (i < SEAT_COUNT); i++)
    {
        if (s_next_fight_turn[i] == 0) s_next_fight_turn[i] = (int64_t)get_gameturn() + 120;
        if ((int64_t)get_gameturn() >= s_next_fight_turn[i])
        {
            const PlayerNumber P = s_seat[i];
            const struct Thing* h = find_players_dungeon_heart(P);
            static int k[SEAT_COUNT] = { 0, 0 };
            event_create_event_or_update_nearby_existing_event(subtile_coord_center(h->mappos.x.stl.num + 12 * (k[i] % 4) - 20),
                subtile_coord_center(h->mappos.y.stl.num + 5 + 12 * (k[i] / 4)), EvKind_EnemyFight, P, 0);
            k[i]++;
            s_next_fight_turn[i] += 120;
        }
    }
    const int64_t done = get_players_dungeon(get_player(my_player_number))->script_flags[0];
    if (done == 2) { FTEST_FAIL_TEST("the harness reported failure (see its output)"); return FTRs_Go_To_Next_Action; }
    if (done != 1)
    {
        if ((int64_t)(LbTimerClock() - s_wait_started) > WAIT_LIMIT_MS) { FTEST_FAIL_TEST("the harness did not finish within %" PRId64 " ms", (int64_t)WAIT_LIMIT_MS); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    int64_t failures = 0;
    for (int64_t i = 0; i < SEAT_COUNT; i++)
    {
        const PlayerNumber P = s_seat[i];
        const int64_t room = treasure_slabs(P), marks = dig_marks(P);
        FTESTLOG("seat %" PRId64 " (player %" PRId64 ") achieved: treasure slabs %" PRId64 ", dig marks %" PRId64, i, (int64_t)P, room, marks);
        if (room < 9) { failures++; FTESTLOG("CHECK FAILED: seat %" PRId64 "'s build_room produced %" PRId64 " treasure slabs, expected 9", i, room); }
        if (marks < 9) { failures++; FTESTLOG("CHECK FAILED: seat %" PRId64 "'s mark_dig left %" PRId64 " dig marks, expected 9", i, marks); }
    }
    if (failures > 0) { FTEST_FAIL_TEST("%" PRId64 " check(s) failed", failures); }
    else { FTESTLOG("Test passed: both seats were played independently, in real time, over one shared connection"); }
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
