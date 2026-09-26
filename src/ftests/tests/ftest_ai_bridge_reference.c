// Game side of the reference-bridge end-to-end test (scripts/llm_bridge/, docs/refactor/AI/LLM/02 section 4a).
// Two processes: this game (real time, the agent never pauses it) and scripts/ai_bridge_reference_e2e.py running the
// bridge with its scripted policy. Run through scripts/run_ftest_ai_bridge_reference.sh; it sits in long_running_tests_list.
//
// The seat is made here the way the Skirmish page would (net_add_external_seat), so the bridge discovers it with get_seats.
// The scene gives the seat a wounded creature, the heal power, gold, a claimed patch to build on and earth to dig. When the
// client signals done (FLAG0 on player 0: 1 ok, 2 failed) this side checks what the agent actually achieved in the world:
// a treasure room, dig marks, a healed creature, and that the game was never paused by the agent.
#include "ftest_ai_bridge_reference.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "kfx_sim_state.h"
#include "bflib_datetm.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_players.h"
#include "config_terrain.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
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

static TbClockMSec s_wait_started = 0;
static PlayerNumber P = -1;
static ThingIndex wounded = 0;
static TbBool s_agent_paused_the_game = false;

void ftest_ai_bridge_reference_pre_start() { fe_computer_players = 1; }

FTestActionResult ref_a01_prepare(struct FTestActionArgs* const args);
FTestActionResult ref_a02_wait_and_verify(struct FTestActionArgs* const args);

TbBool ftest_ai_bridge_reference_init()
{
    s_agent_paused_the_game = false;
    ftest_append_action(ref_a01_prepare, 0, NULL);
    ftest_append_action(ref_a02_wait_and_verify, 1, NULL);
    return true;
}

FTestActionResult ref_a01_prepare(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("could not make the rival an External seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    const MapSlabCoord hsx = subtile_slab(hx), hsy = subtile_slab(hy);
    struct Dungeon* d = get_players_dungeon(get_player(P));
    d->total_money_owned = 100000;
    d->room_buildable[get_rid(room_desc, "TREASURE")] |= 1;
    d->magic_level[power_model_id("POWER_HEAL_CREATURE")] = 8;
    ftest_util_replace_slabs(hsx + 4, hsy - 2, hsx + 8, hsy + 2, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(hsx - 9, hsy - 3, hsx - 5, hsy + 1, SlbT_EARTH, PLAYER_NEUTRAL);
    struct Thing* c = ftest_util_create_creature(subtile_coord_center(hx - 3), subtile_coord_center(hy + 3), P, 9, (ThingModel)creature_model_id("ORC"));
    if (thing_is_invalid(c)) { FTEST_FAIL_TEST("no creature"); return FTRs_Go_To_Next_Action; }
    wounded = c->index;
    c->health = 20;
    get_players_dungeon(get_player(my_player_number))->script_flags[1] = 1; // ready
    s_wait_started = LbTimerClock();
    return FTRs_Go_To_Next_Action;
}

static int64_t treasure_slabs(void)
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

static int64_t dig_marks(void)
{
    const struct Dungeon* d = get_players_dungeon(get_player(P));
    int64_t n = 0;
    for (int64_t i = 0; i < MAPTASKS_COUNT; i++) if (d->task_list[i].kind != 0) n++;
    return n;
}

FTestActionResult ref_a02_wait_and_verify(struct FTestActionArgs* const args)
{
    // FLAG1: 1 = scene ready, 2 = the client has finished the bridge phase (it then pauses to compare views, which is fine).
    const int64_t phase = get_players_dungeon(get_player(my_player_number))->script_flags[1];
    if ((phase == 1) && (extseat_agent_owns_pause() || flag_is_set(kfx_sim_state.operation_flags, GOF_Paused))) s_agent_paused_the_game = true;
    const int64_t done = get_players_dungeon(get_player(my_player_number))->script_flags[0];
    if (done == 2) { FTEST_FAIL_TEST("the bridge client reported failure (see its output)"); return FTRs_Go_To_Next_Action; }
    if (done != 1)
    {
        if ((int64_t)(LbTimerClock() - s_wait_started) > WAIT_LIMIT_MS) { FTEST_FAIL_TEST("the bridge client did not finish within %" PRId64 " ms", (int64_t)WAIT_LIMIT_MS); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    int64_t failures = 0;
    const int64_t room = treasure_slabs(), marks = dig_marks();
    const struct Thing* c = thing_get(wounded);
    const int64_t hp = thing_is_invalid(c) ? -1 : c->health;
    FTESTLOG("what the bridge achieved: treasure slabs %" PRId64 ", dig marks %" PRId64 ", creature health %" PRId64 " (was 20)", room, marks, hp);
    if (room < 9) { failures++; FTESTLOG("CHECK FAILED: the agent's build_room produced %" PRId64 " treasure slabs, expected 9", room); }
    if (marks < 9) { failures++; FTESTLOG("CHECK FAILED: the agent's mark_dig left %" PRId64 " dig marks, expected 9", marks); }
    if (hp < 60) { failures++; FTESTLOG("CHECK FAILED: the agent's heal left the creature at %" PRId64 " health", hp); }
    if (s_agent_paused_the_game) { failures++; FTESTLOG("CHECK FAILED: the game was paused; the bridge must run in real time"); }
    if (failures > 0) { FTEST_FAIL_TEST("%" PRId64 " check(s) failed", failures); }
    else { FTESTLOG("Test passed: the reference bridge played a seat in real time and its orders took effect"); }
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
