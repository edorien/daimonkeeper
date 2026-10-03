// player_enter_spectator_mode (player_utils.c): hands the local player's own seat to the built-in AI (full
// autopilot, not the co-op "computer assistant") and reveals the whole map, so a human can watch that seat
// played by the AI via the normal camera / floating-spirit view instead of controlling it. Also exercises the
// engine_camera.c fix that keeps updating the local player's camera despite PlaF_CompCtrl (update_player_camera
// is normally skipped for a CompCtrl player -- nothing renders them -- but the local player IS on screen here).
#include "ftest_spectator_handoff.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_packet_inject.h"
#include "../ftest_util.h"

#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "kfx_sim_state.h"
#include "frontend.h"
#include "map_data.h"
#include "player_data.h"
#include "player_utils.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static MapSubtlCoord s_far_x = 2, s_far_y = 2;

FTestActionResult sh01_setup(struct FTestActionArgs* const args);
FTestActionResult sh02_check(struct FTestActionArgs* const args);
FTestActionResult sh03_packet_toggle_on(struct FTestActionArgs* const args);
FTestActionResult sh04_packet_toggle_on_check(struct FTestActionArgs* const args);
FTestActionResult sh04d_inject_viewtype(struct FTestActionArgs* const args);
FTestActionResult sh04e_check_viewtype_restore(struct FTestActionArgs* const args);
FTestActionResult sh04f_check_viewtype_restored(struct FTestActionArgs* const args);
FTestActionResult sh05_packet_toggle_off(struct FTestActionArgs* const args);
FTestActionResult sh06_packet_toggle_off_check(struct FTestActionArgs* const args);

void ftest_spectator_handoff_pre_start() { fe_computer_players = 1; }

TbBool ftest_spectator_handoff_init()
{
    s_failures = 0;
    ftest_packet_inject_reset();
    ftest_append_action(sh01_setup, 0, NULL);
    ftest_append_action(sh02_check, 1, NULL);
    ftest_append_action(sh03_packet_toggle_on, 1, NULL);
    ftest_append_action(sh04_packet_toggle_on_check, 3, NULL);
    ftest_append_action(sh04d_inject_viewtype, 1, NULL);
    ftest_append_action(sh04e_check_viewtype_restore, 1, NULL);
    ftest_append_action(sh04f_check_viewtype_restored, 1, NULL);
    ftest_append_action(sh05_packet_toggle_off, 1, NULL);
    ftest_append_action(sh06_packet_toggle_off_check, 3, NULL);
    return true;
}

FTestActionResult sh01_setup(struct FTestActionArgs* const args)
{
    CHECK_TRUE("an invalid player cannot enter spectator mode", !player_enter_spectator_mode(PLAYER_NEUTRAL));
    CHECK_TRUE("the local player is not CompCtrl before handoff", !flag_is_set(get_player(my_player_number)->allocflags, PlaF_CompCtrl));

    // This level may already reveal the whole map for the local player by its own design (a real campaign level's
    // script, not a test fixture); rather than depend on that, conceal one point ourselves so the "was it revealed
    // by the handoff" check is deterministic regardless of the level.
    s_far_x = kfx_sim_state.map_subtiles_x - 3;
    s_far_y = kfx_sim_state.map_subtiles_y - 3;
    conceal_map_area(my_player_number, s_far_x - 1, s_far_x + 1, s_far_y - 1, s_far_y + 1, true);
    CHECK_TRUE("the far corner starts unrevealed for the local player", !subtile_revealed_directly(s_far_x, s_far_y, my_player_number));

    struct Dungeon* d = get_players_dungeon(get_player(my_player_number));
    d->camera_deviate_quake = 5;

    CHECK_TRUE("player_enter_spectator_mode succeeds for the local player", player_enter_spectator_mode(my_player_number));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sh02_check(struct FTestActionArgs* const args)
{
    struct PlayerInfo* player = get_player(my_player_number);
    CHECK_TRUE("the local player is now CompCtrl (handed off to the built-in AI)", flag_is_set(player->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("the whole map was revealed for them", subtile_revealed_directly(s_far_x, s_far_y, my_player_number));

    struct Dungeon* d = get_players_dungeon(player);
    // A turn of the real game loop has passed (update_all_players_cameras runs every turn, game_session_loop.cpp)
    // since the handoff; the quake decay in update_player_camera only runs if the camera guard's local-spectator
    // exception (engine_camera.c) is actually letting it through despite CompCtrl.
    CHECK_TRUE("the local player's camera still updates despite CompCtrl (engine_camera.c exception)", d->camera_deviate_quake < 5);

    CHECK_TRUE("re-entering spectator mode on an already-handed-off seat is a harmless no-op", player_enter_spectator_mode(my_player_number));

    // Consecutive ftests share one running process (no fresh init_players() between them, unlike a real game
    // start), so leaving the local player CompCtrl here would carry over into whichever test runs next and break
    // its assumption of a normal human seat. Restore it before handing control to the next test.
    clear_flag(player->allocflags, PlaF_CompCtrl);

    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " spectator handoff check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: spectator handoff sets CompCtrl, reveals the map, and keeps the local camera live");
    return FTRs_Go_To_Next_Action;
}

// The UI checkbox (frontgui_screens.cpp, Options -> Game -> Computer Assist) never calls
// player_enter/leave_spectator_mode directly -- it sends PckA_ToggleSpectate like any other synced command, and
// packets.c's process_user_dungeon_control_packet_action toggles based on the seat's current CompCtrl state.
// sh01/sh02 above already proved the engine function itself; this proves the packet round trip that is what a
// live game actually dispatches.
FTestActionResult sh03_packet_toggle_on(struct FTestActionArgs* const args)
{
    CHECK_TRUE("starts not CompCtrl, for a clean toggle-on test", !flag_is_set(get_player(my_player_number)->allocflags, PlaF_CompCtrl));
    struct FtestPacketInject r = {0};
    r.action = PckA_ToggleSpectate;
    ftest_packet_inject_queue_local(&r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sh04_packet_toggle_on_check(struct FTestActionArgs* const args)
{
    CHECK_TRUE("PckA_ToggleSpectate turned CompCtrl on from off", flag_is_set(get_player(my_player_number)->allocflags, PlaF_CompCtrl));
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " packet toggle-on check(s) failed", s_failures); }
    return FTRs_Go_To_Next_Action;
}

// The regression this pins: before packets.c moved the CompCtrl gate from around process_user_packet() to
// inside it (guarding only PVT_DungeonTop's dungeon-mutating dispatch), *every* packet for a CompCtrl local
// player was dropped, not just build/dig/spell ones -- so switching to the parchment map view (handled by
// process_user_global_packet_action, unconditionally, regardless of view_type) was just as broken as
// PckA_ToggleSpectate itself was. Live-reported: "can't view the full map... can't quit." (Pause itself isn't
// exercised here the same way: actually pausing the sim also freezes this test's own turn-counted action
// delays, which would need a much more careful, timer-independent check to test safely -- the view-type switch
// already proves process_user_global_packet_action reaches a CompCtrl local player without that complication.)
FTestActionResult sh04d_inject_viewtype(struct FTestActionArgs* const args)
{
    struct FtestPacketInject r = {0};
    r.action = PckA_SetViewType;
    r.par1 = PVT_MapScreen;
    ftest_packet_inject_queue_local(&r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sh04e_check_viewtype_restore(struct FTestActionArgs* const args)
{
    CHECK_TRUE("switching to the parchment map view (\"can't view the full map\") also reaches a CompCtrl local player",
        get_player(my_player_number)->view_type == PVT_MapScreen);
    struct FtestPacketInject r = {0};
    r.action = PckA_SetViewType;
    r.par1 = PVT_DungeonTop;
    ftest_packet_inject_queue_local(&r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sh04f_check_viewtype_restored(struct FTestActionArgs* const args)
{
    CHECK_TRUE("view switches back cleanly too", get_player(my_player_number)->view_type == PVT_DungeonTop);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " global-action-while-spectating check(s) failed", s_failures); }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sh05_packet_toggle_off(struct FTestActionArgs* const args)
{
    struct FtestPacketInject r = {0};
    r.action = PckA_ToggleSpectate;
    ftest_packet_inject_queue_local(&r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sh06_packet_toggle_off_check(struct FTestActionArgs* const args)
{
    CHECK_TRUE("PckA_ToggleSpectate turned CompCtrl back off from on", !flag_is_set(get_player(my_player_number)->allocflags, PlaF_CompCtrl));
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " packet toggle-off check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: PckA_ToggleSpectate round-trips CompCtrl on and off, matching the UI checkbox's own path");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
