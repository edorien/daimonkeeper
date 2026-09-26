#include "ftest_skirmish_external_slot.h"

#ifdef FUNCTESTING

extern "C" {
#include "pre_inc.h"
#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_packet_inject.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "net_game.h"
#include "packet_data.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "post_inc.h"
}
#include "skirmish_setup.h"

extern "C" {
extern int64_t fe_computer_players; // frontend.h -- set by Skirmish's Play button
}

namespace {
int64_t s_failures = 0;
#define CHECK_TRUE(what, cond) do { if (!(cond)) { s_failures++; FTESTLOG("CHECK FAILED: %s", what); } } while (0)
constexpr PlayerNumber kSeatSlot = 1;
constexpr PlayerNumber kAiSlot = 2;
} // namespace

extern "C" {

FTestActionResult sk01_check_seat(struct FTestActionArgs* const args);
FTestActionResult sk02_drive(struct FTestActionArgs* const args);
FTestActionResult sk03_check_drive(struct FTestActionArgs* const args);

void ftest_skirmish_external_slot_pre_start()
{
    const LevelNumber level = get_selected_level_number();
    fe_computer_players = 1; // what Skirmish's Play does
    skirmish_setup_sync(level, 0);
    if (!skirmish_setup().enabled())
    {
        FTEST_FAIL_TEST("Setup tab disabled for level %" PRId64 ": %s", (int64_t)level, skirmish_setup().unavailable_reason.c_str());
        return;
    }
    skirmish_setup_set_controller(kSeatSlot, SkirmishCtl_External, 0);
    skirmish_setup_set_controller(kAiSlot, SkirmishCtl_Model, 13);
    skirmish_setup_install_for_play(level);
    if (net_pending_external_seats_count() != 1)
        FTEST_FAIL_TEST("Play queued %" PRId64 " External slot(s), expected 1", (int64_t)net_pending_external_seats_count());
}

TbBool ftest_skirmish_external_slot_init()
{
    ftest_packet_inject_reset();
    s_failures = 0;
    ftest_append_action(sk01_check_seat, 2, nullptr);
    ftest_append_action(sk02_drive, 1, nullptr);
    ftest_append_action(sk03_check_drive, 3, nullptr);
    return true;
}

FTestActionResult sk01_check_seat(struct FTestActionArgs* const args)
{
    const struct PlayerInfo *seat = get_player(kSeatSlot);
    const struct PlayerInfo *ai = get_player(kAiSlot);
    CHECK_TRUE("the pending list was consumed by the game start", net_pending_external_seats_count() == 0);
    CHECK_TRUE("slot 1 is an External seat", flag_is_set(seat->allocflags, PlaF_ExternalSeat));
    CHECK_TRUE("slot 1 is no longer computer-controlled", !flag_is_set(seat->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("slot 1's user maps back to it", get_net_user_player_number(seat->user_id) == kSeatSlot);
    CHECK_TRUE("slot 2 is still the built-in AI", flag_is_set(ai->allocflags, PlaF_CompCtrl) && !flag_is_set(ai->allocflags, PlaF_ExternalSeat));
    CHECK_TRUE("the human is not a seat", !flag_is_set(get_player(my_player_number)->allocflags, PlaF_ExternalSeat));
    if (s_failures > 0) FTEST_FAIL_TEST("%" PRId64 " slot-setup check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sk02_drive(struct FTestActionArgs* const args)
{
    struct FtestPacketInject r = {};
    r.action = PckA_SetPlyrState;
    r.par1 = PSt_Slap;
    ftest_packet_inject_queue(get_player(kSeatSlot)->user_id, &r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sk03_check_drive(struct FTestActionArgs* const args)
{
    CHECK_TRUE("the seat's packet reached slot 1", get_player(kSeatSlot)->work_state == PSt_Slap);
    CHECK_TRUE("and not the built-in AI's slot", get_player(kAiSlot)->work_state != PSt_Slap);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: the Skirmish page's External slot becomes a packet-driven seat at game start");
    return FTRs_Go_To_Next_Action;
}

} // extern "C"

#endif // FUNCTESTING
