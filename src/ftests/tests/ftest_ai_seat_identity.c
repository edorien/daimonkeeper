// Phase M1 of docs/refactor/AI/LLM/01-integration-plan.md section 7 (E1 + E2): a second, packet-driven
// seat in a local game must (1) have its packets dispatched -- get_net_user_player_number() used to
// map only the local human -- and (2) keep that after a save/load, because the user->player mapping
// is process-global state the save does not contain (06-lifecycle-and-robustness.md section 1).
//
// The seat is a converted computer keeper: net_add_external_seat() clears its CompCtrl flag, so the
// built-in AI stops running for it, and its packets are written with ftest_packet_inject.h exactly
// as an external bridge would write them. Effect is observed through PckA_SetPlyrState, which is
// dispatched per user and lands on that user's player's work_state -- so a packet reaching the wrong
// player, or none, shows up immediately.
#include "ftest_ai_seat_identity.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_packet_inject.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "dungeon_data.h"
#include "game_saves.h"
#include "net_game.h"
#include "frontend.h"
#include "packet_data.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define CHECK_TRUE(what, cond) do { if (!(cond)) { s_failures++; FTESTLOG("CHECK FAILED: %s", what); } } while (0)

struct ftest_ai_seat__variables
{
    PlayerNumber seat_player;
    NetUserId seat_user;
};
static struct ftest_ai_seat__variables vars = {-1, -1};

static void inject_set_state(NetUserId user, PlayerState state)
{
    struct FtestPacketInject r = {0};
    r.action = PckA_SetPlyrState;
    r.par1 = state;
    ftest_packet_inject_queue(user, &r);
}

FTestActionResult s01_add_seat(struct FTestActionArgs* const args);
FTestActionResult s02_drive_seat(struct FTestActionArgs* const args);
FTestActionResult s03_check_dispatch(struct FTestActionArgs* const args);
FTestActionResult s04_save_break_load(struct FTestActionArgs* const args);
FTestActionResult s05_drive_seat_after_load(struct FTestActionArgs* const args);
FTestActionResult s06_check_dispatch_after_load(struct FTestActionArgs* const args);

// A two-keeper level: original-pack multiplayer map 50. As Skirmish's Play button does, ask for computer
// players so the second keeper exists (and is computer-controlled) for the seat to take over.
void ftest_ai_seat_identity_pre_start()
{
    fe_computer_players = 1;
}

TbBool ftest_ai_seat_identity_init()
{
    ftest_packet_inject_reset();
    s_failures = 0;
    ftest_append_action(s01_add_seat, 0, &vars);
    ftest_append_action(s02_drive_seat, 3, &vars);
    ftest_append_action(s03_check_dispatch, 2, &vars);
    ftest_append_action(s04_save_break_load, 2, &vars);
    ftest_append_action(s05_drive_seat_after_load, 3, &vars);
    ftest_append_action(s06_check_dispatch_after_load, 2, &vars);
    return true;
}

FTestActionResult s01_add_seat(struct FTestActionArgs* const args)
{
    // Any other keeper with a heart will do; the level's own AI opponent is the usual one.
    vars.seat_player = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
    {
        if (p == my_player_number || p == PLAYER_GOOD || p == PLAYER_NEUTRAL)
            continue;
        if (player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p)))
        {
            vars.seat_player = p;
            break;
        }
    }
    if (vars.seat_player < 0)
    {
        FTEST_FAIL_TEST("no second keeper with a dungeon heart on this level");
        return FTRs_Go_To_Next_Action;
    }
    FTESTLOG("seat player: %" PRId64, (int64_t)vars.seat_player);

    CHECK_TRUE("the player was computer-controlled before conversion", flag_is_set(get_player(vars.seat_player)->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("before: user 1 maps to no player", get_net_user_player_number(1) < 0);

    vars.seat_user = net_add_external_seat(vars.seat_player);
    CHECK_TRUE("net_add_external_seat returned user 1", vars.seat_user == 1);
    CHECK_TRUE("user 1 maps to the seat player", get_net_user_player_number(1) == vars.seat_player);
    CHECK_TRUE("the local human still maps to itself", get_net_user_player_number(SOLO_HUMAN_ID) == my_player_number);
    const struct PlayerInfo* seat = get_player(vars.seat_player);
    CHECK_TRUE("CompCtrl cleared", !flag_is_set(seat->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("ExternalSeat set", flag_is_set(seat->allocflags, PlaF_ExternalSeat));
    CHECK_TRUE("player.user_id is 1", seat->user_id == 1);
    if (s_failures > 0)
        FTEST_FAIL_TEST("%" PRId64 " seat-setup check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult s02_drive_seat(struct FTestActionArgs* const args)
{
    inject_set_state(vars.seat_user, PSt_Slap);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult s03_check_dispatch(struct FTestActionArgs* const args)
{
    CHECK_TRUE("the seat's packet reached its player (work_state is PSt_Slap)", get_player(vars.seat_player)->work_state == PSt_Slap);
    CHECK_TRUE("the local human's work_state was not touched", get_player(my_player_number)->work_state != PSt_Slap);
    if (s_failures > 0)
        FTEST_FAIL_TEST("%" PRId64 " dispatch check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

// Save, then break what a fresh process (or another game) would not have -- the process-global mapping and
// the seat's work state -- and load. Nothing but the save and the post-load fixup can put them back.
FTestActionResult s04_save_break_load(struct FTestActionArgs* const args)
{
    const int64_t slot = 4;
    fill_game_catalogue_slot(slot, "ai_seat_identity_ftest");
    set_flag(kfx_sim_state.operation_flags, GOF_Paused);
    const TbBool saved = save_game(slot);
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    CHECK_TRUE("save_game", saved);
    if (!saved)
    {
        FTEST_FAIL_TEST("save failed");
        return FTRs_Go_To_Next_Action;
    }

    net_clear_external_seats();
    get_player(vars.seat_player)->work_state = PSt_CtrlDungeon;
    CHECK_TRUE("after clearing: user 1 maps to no player", get_net_user_player_number(1) < 0);

    CHECK_TRUE("save is loadable", is_save_game_loadable(slot));
    const TbBool loaded = load_game(slot);
    CHECK_TRUE("load_game", loaded);
    if (loaded)
    {
        CHECK_TRUE("after load: the local human is still player 0's seat", get_net_user_player_number(SOLO_HUMAN_ID) == my_player_number);
        CHECK_TRUE("after load: user 1 maps to the seat player again", get_net_user_player_number(1) == vars.seat_player);
        const struct PlayerInfo* seat = get_player(vars.seat_player);
        CHECK_TRUE("after load: ExternalSeat flag survived", flag_is_set(seat->allocflags, PlaF_ExternalSeat));
        CHECK_TRUE("after load: CompCtrl still clear", !flag_is_set(seat->allocflags, PlaF_CompCtrl));
        CHECK_TRUE("after load: player.user_id survived", seat->user_id == 1);
    }
    if (s_failures > 0)
        FTEST_FAIL_TEST("%" PRId64 " save/load check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult s05_drive_seat_after_load(struct FTestActionArgs* const args)
{
    inject_set_state(vars.seat_user, PSt_Slap);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult s06_check_dispatch_after_load(struct FTestActionArgs* const args)
{
    CHECK_TRUE("after load: the seat's packet reached its player again", get_player(vars.seat_player)->work_state == PSt_Slap);
    if (s_failures > 0)
    {
        FTEST_FAIL_TEST("%" PRId64 " post-load dispatch check(s) failed", s_failures);
        return FTRs_Go_To_Next_Action;
    }
    FTESTLOG("Test passed: an External seat is dispatched, and stays dispatched across a save/load");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
