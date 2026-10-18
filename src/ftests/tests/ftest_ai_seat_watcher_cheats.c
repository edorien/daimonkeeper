// The human watching an agent play their own keeper (the campaign "Seat control: LLM" seat) may step in through the
// cheat menu in an emergency, with cheat mode on: the menu writes the human's own packet (user 0), which the engine
// otherwise ignores for an External seat's player beyond pause, chat and the camera. Drives that packet as the menu
// does: "all rooms" takes effect; the "make good creature" placement mode is entered, a click on the map makes the
// creature, and the mode is left back to dungeon control; an ordinary control state (build room) stays refused.
#include "ftest_ai_seat_watcher_cheats.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_packet_inject.h"

#include "cheat_mode.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "config_terrain.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "player_availability.h"
#include "player_data.h"
#include "slab_data.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static MapSlabCoord hsx, hsy;
static int64_t s_rooms_before = 0, s_creatures_before = 0;
static unsigned char s_eggs = 0;

static int64_t rooms_available(void)
{
    int64_t n = 0;
    for (int64_t rk = 1; rk < kfx_config_state.conf.slab_conf.room_types_count; rk++) n += is_room_available(my_player_number, (RoomKind)rk);
    return n;
}
static int64_t creatures_on_slab(MapSlabCoord sx, MapSlabCoord sy)
{
    int64_t n = 0;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t g = 0; (idx > 0) && (g < 4000); g++) {
        const struct Thing* t = thing_get(idx); idx = t->next_of_class;
        if ((subtile_slab(t->mappos.x.stl.num) == sx) && (subtile_slab(t->mappos.y.stl.num) == sy)) n++;
    }
    return n;
}
static void watcher_action(unsigned char action, int64_t par1, int64_t par2)
{
    struct FtestPacketInject req; memset(&req, 0, sizeof(req));
    req.action = action; req.par1 = par1; req.par2 = par2;
    ftest_packet_inject_queue_local(&req);
}

FTestActionResult wc01_all_rooms(struct FTestActionArgs* const args);
FTestActionResult wc02_placement_mode(struct FTestActionArgs* const args);
FTestActionResult wc03_click(struct FTestActionArgs* const args);
FTestActionResult wc04_check_creature_then_leave(struct FTestActionArgs* const args);
FTestActionResult wc05_check_left_and_refusal(struct FTestActionArgs* const args);
FTestActionResult wc06_finish(struct FTestActionArgs* const args);

void ftest_ai_seat_watcher_cheats_pre_start()
{
    // The campaign select screen's "Seat control: LLM": the local player's own keeper becomes an External seat.
    net_pending_external_seats_clear();
    net_pending_external_seats_add(my_player_number);
}

TbBool ftest_ai_seat_watcher_cheats_init()
{
    s_failures = 0;
    ftest_append_action(wc01_all_rooms, 0, NULL);
    ftest_append_action(wc02_placement_mode, 2, NULL);
    ftest_append_action(wc03_click, 2, NULL);
    ftest_append_action(wc04_check_creature_then_leave, 2, NULL);
    ftest_append_action(wc05_check_left_and_refusal, 2, NULL);
    ftest_append_action(wc06_finish, 2, NULL);
    return true;
}

FTestActionResult wc01_all_rooms(struct FTestActionArgs* const args)
{
    struct PlayerInfo* player = get_my_player();
    if (!flag_is_set(player->allocflags, PlaF_ExternalSeat)) { FTEST_FAIL_TEST("the local keeper is not an External seat"); return FTRs_Go_To_Next_Action; }
    s_eggs = kfx_sim_state.easter_eggs_enabled;
    kfx_sim_state.easter_eggs_enabled = 1;   // cheat mode on
    extseat_resume();                         // an agent-only level starts paused for the agent
    const struct Thing* heart = find_players_dungeon_heart(my_player_number);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    ftest_util_replace_slabs(hsx - 2, hsy + 3, hsx + 2, hsy + 4, SlbT_CLAIMED, my_player_number);
    ftest_util_reveal_map(my_player_number);
    s_rooms_before = rooms_available();
    watcher_action(PckA_CheatAllRooms, 0, 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult wc02_placement_mode(struct FTestActionArgs* const args)
{
    const int64_t after = rooms_available();
    FTESTLOG("rooms available: %d before, %d after the watcher's (all rooms)", (int)s_rooms_before, (int)after);
    CHECK_TRUE("the watcher's one-shot cheat took effect", after > s_rooms_before);
    watcher_action(PckA_SetPlyrState, PSt_MkGoodCreatr, 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult wc03_click(struct FTestActionArgs* const args)
{
    struct PlayerInfo* player = get_my_player();
    FTESTLOG("work state after the watcher's placement mode: %d (make good creature %d)", (int)player->work_state, (int)PSt_MkGoodCreatr);
    CHECK_TRUE("the watcher entered the placement mode", player->work_state == PSt_MkGoodCreatr);
    s_creatures_before = creatures_on_slab(hsx, hsy + 3);
    struct FtestPacketInject req; memset(&req, 0, sizeof(req));
    req.has_position = true;
    req.pos_x = subtile_coord_center(slab_subtile_center(hsx));
    req.pos_y = subtile_coord_center(slab_subtile_center(hsy + 3));
    req.control_flags = PCtr_LBtnRelease;
    ftest_packet_inject_queue_local(&req);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult wc04_check_creature_then_leave(struct FTestActionArgs* const args)
{
    const int64_t after = creatures_on_slab(hsx, hsy + 3);
    FTESTLOG("creatures on the clicked slab: %d before, %d after", (int)s_creatures_before, (int)after);
    CHECK_TRUE("the watcher's click made a creature", after == s_creatures_before + 1);
    watcher_action(PckA_SetPlyrState, PSt_CtrlDungeon, 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult wc05_check_left_and_refusal(struct FTestActionArgs* const args)
{
    struct PlayerInfo* player = get_my_player();
    CHECK_TRUE("the watcher left the placement mode", player->work_state == PSt_CtrlDungeon);
    watcher_action(PckA_SetPlyrState, PSt_BuildRoom, 1); // not a cheat: the agent's to choose
    return FTRs_Go_To_Next_Action;
}

FTestActionResult wc06_finish(struct FTestActionArgs* const args)
{
    struct PlayerInfo* player = get_my_player();
    CHECK_TRUE("an ordinary control state from the watcher stays refused", player->work_state == PSt_CtrlDungeon);
    kfx_sim_state.easter_eggs_enabled = s_eggs;
    net_release_external_seat(my_player_number); // later tests share the process
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " watcher cheat check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: a human watching an agent can use the cheat menu on their keeper in an emergency");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
