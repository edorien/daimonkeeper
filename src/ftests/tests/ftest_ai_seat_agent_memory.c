// docs/refactor/AI/omissions/09-persistent-memory.md section 4: what an External seat's agent keeps about its game
// is held by the engine and written into the save (the optional AGNT chunk), so loading a save brings the agent's
// plan back exactly as it stood when the game was saved, and a save made while no agent held anything loads with
// nothing. The payload's own format and its malformed cases are covered by kfx_net's Catch2 agent_memory tests.
#include "ftest_ai_seat_agent_memory.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "agent_memory.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "game_saves.h"
#include "net_game.h"
#include "frontend.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define CHECK_TRUE(what, cond) do { if (!(cond)) { s_failures++; FTESTLOG("CHECK FAILED: %s", what); } } while (0)

static PlayerNumber s_seat = -1;

FTestActionResult am01_seat_and_save(struct FTestActionArgs* const args);
FTestActionResult am02_change_and_load(struct FTestActionArgs* const args);
FTestActionResult am03_save_without_memory(struct FTestActionArgs* const args);

void ftest_ai_seat_agent_memory_pre_start()
{
    fe_computer_players = 1;
}

TbBool ftest_ai_seat_agent_memory_init()
{
    s_failures = 0;
    ftest_append_action(am01_seat_and_save, 0, NULL);
    ftest_append_action(am02_change_and_load, 2, NULL);
    ftest_append_action(am03_save_without_memory, 2, NULL);
    return true;
}

static TbBool memory_is(PlayerNumber p, const char *want)
{
    size_t len = 0;
    const char *got = agent_memory_get(p, &len);
    if (want == NULL)
        return got == NULL;
    return (got != NULL) && (len == strlen(want)) && (memcmp(got, want, len) == 0);
}

static TbBool save_to(int64_t slot, const char *name)
{
    fill_game_catalogue_slot(slot, name);
    set_flag(kfx_sim_state.operation_flags, GOF_Paused);
    const TbBool saved = save_game(slot);
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    return saved;
}

FTestActionResult am01_seat_and_save(struct FTestActionArgs* const args)
{
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        if ((p != my_player_number) && (p != PLAYER_GOOD) && (p != PLAYER_NEUTRAL) && player_exists(get_player(p))
         && !thing_is_invalid(find_players_dungeon_heart(p))) {
            s_seat = p;
            break;
        }
    }
    if ((s_seat < 0) || (net_add_external_seat(s_seat) < 0)) {
        FTEST_FAIL_TEST("no second keeper to make an External seat of");
        return FTRs_Go_To_Next_Action;
    }
    CHECK_TRUE("a new level starts with no agent memory", memory_is(s_seat, NULL));
    CHECK_TRUE("agent_memory_set for the seat", agent_memory_set(s_seat, "{\"plan\":\"A\"}", 12));
    CHECK_TRUE("agent_memory_set for another player", agent_memory_set(my_player_number, "other", 5));
    CHECK_TRUE("save_game with agent memory", save_to(5, "ai_seat_agent_memory_1"));
    if (s_failures > 0)
        FTEST_FAIL_TEST("%" PRId64 " setup check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

// The plan moves on after the save, then the save is loaded: the plan as it was at the save must come back.
FTestActionResult am02_change_and_load(struct FTestActionArgs* const args)
{
    agent_memory_set(s_seat, "{\"plan\":\"B\"}", 12);
    agent_memory_set(my_player_number, NULL, 0);
    CHECK_TRUE("the save is loadable", is_save_game_loadable(5));
    const TbBool loaded = load_game(5);
    CHECK_TRUE("load_game", loaded);
    if (loaded) {
        CHECK_TRUE("after load: the seat's memory is the one saved, not the later one", memory_is(s_seat, "{\"plan\":\"A\"}"));
        CHECK_TRUE("after load: another player's memory came back too", memory_is(my_player_number, "other"));
        CHECK_TRUE("after load: the seat is still an External seat", get_net_user_player_number(get_player(s_seat)->user_id) == s_seat);
    }
    if (s_failures > 0)
        FTEST_FAIL_TEST("%" PRId64 " load check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

// A save made while no agent held anything (like any save from before this existed) loads with no memory, even if an
// agent has written some since.
FTestActionResult am03_save_without_memory(struct FTestActionArgs* const args)
{
    agent_memory_clear_all();
    CHECK_TRUE("save_game without agent memory", save_to(6, "ai_seat_agent_memory_2"));
    agent_memory_set(s_seat, "{\"plan\":\"C\"}", 12);
    const TbBool loaded = load_game(6);
    CHECK_TRUE("load_game", loaded);
    if (loaded) {
        CHECK_TRUE("after loading a save without memory: the seat has none", memory_is(s_seat, NULL));
    }
    if (s_failures > 0) {
        FTEST_FAIL_TEST("%" PRId64 " no-memory check(s) failed", s_failures);
        return FTRs_Go_To_Next_Action;
    }
    FTESTLOG("Test passed: agent memory is saved with the game and comes back exactly as saved");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
