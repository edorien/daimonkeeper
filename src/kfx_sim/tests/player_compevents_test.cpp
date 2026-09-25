// kfx_sim coverage: player_compevents.c's computer-AI "event" handlers
// are almost entirely built on computer_able_to_use_power (itself gated
// by is_power_available's config_reload_callbacks/dungeon_availability
// chain, whose defaults aren't simple enough to reason about without a
// dedicated fixture) and task-creation calls, so only the cheap early-out
// guard clauses are tractable here: computer_event_check_payday returns
// immediately once a dungeon already has enough gold for its pay bill,
// and computer_event_check_fighters returns immediately when there are no
// fights in progress at all -- both true of a freshly zeroed dungeon by
// construction (0 >= 0, 0 <= 0), so both are exercised without any
// fixture setup beyond ResetSimAndConfig.
//
// player_compevents.c is the last of kfx_sim/src's originally-header-less
// files (see player_compchecks_test.cpp's note) to get any coverage.
// Forward-declared locally since no header exists for this file.
//
// Deliberately deferred: everything past those guards in this file
// (computer_event_battle/_find_link/_attack_magic_foe/_check_rooms_full/
// _save_tortured/_rebuild_room/_handle_prisoner/_attack_door) -- all
// power-availability, task-queue, and creature-search heavy.
#include <catch2/catch_test_macros.hpp>

#include "player_computer.h"
#include "dungeon_data.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

extern "C" {
int64_t computer_event_check_payday(struct Computer2 *comp, struct ComputerEvent *cevent, struct Event *event);
int64_t computer_event_check_fighters(struct Computer2 *comp, struct ComputerEvent *cevent);
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_event_check_payday is a no-op once the dungeon already has enough gold to cover its pay bill", "[kfx_sim][player_compevents]") {
    struct Computer2 *comp = make_computer_player(0);
    computer_dungeon(comp)->total_money_owned = 100;
    computer_dungeon(comp)->creatures_total_pay = 100; // exactly enough: >= holds

    struct ComputerEvent cevent = {};
    CHECK(computer_event_check_payday(comp, &cevent, nullptr) == CTaskRet_Unk4);
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_event_check_fighters is a no-op when the dungeon has no fights in progress", "[kfx_sim][player_compevents]") {
    struct Computer2 *comp = make_computer_player(0);
    computer_dungeon(comp)->fights_num = 0;

    struct ComputerEvent cevent = {};
    CHECK(computer_event_check_fighters(comp, &cevent) == CTaskRet_Unk4);
}
