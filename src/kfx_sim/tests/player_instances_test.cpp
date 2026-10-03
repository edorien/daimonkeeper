// kfx_sim coverage: a first pass over player_instances.c's
// thing-control-ownership predicates and selection helpers. The
// pinstf*/set_player_instance/process_player_instance* state-machine
// entry points (each PI_* instance's start/mid/end handler) and
// player_build_room_at/player_place_trap_at/player_place_door_at
// (money/room-availability/sound-feedback heavy) are deliberately
// deferred -- this pass covers only the pure "does player X currently
// control/select thing Y" queries, which read PlayerInfo fields directly.
//
// is_thing_directly_controlled_by_player's PI_HeartZoom/_HeartZoomOut/
// _Drop branch: only its influenced_thing_idx half is exercised; the
// CMF_IsSpectator half (get_creature_model_flags on the thing's model)
// isn't covered yet.
#include <catch2/catch_test_macros.hpp>

#include "player_instances.h"
#include "player_data.h"
#include "kfx_sim_test_fixtures.h"
#include "thing_stats.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "is_thing_passenger_controlled matches the owning player's work_state/instance_num against the thing", "[kfx_sim][player_instances]") {
    struct Thing *thing = make_creature(1, 1, 0);
    struct PlayerInfo *player = get_player(0);

    CHECK_FALSE(is_thing_passenger_controlled(thing)); // work_state defaults to something other than PSt_CtrlPassngr

    player->work_state = PSt_CtrlPassngr;
    player->instance_num = PI_PsngrCtrl;
    player->influenced_thing_idx = 1;
    CHECK(is_thing_passenger_controlled(thing));

    player->influenced_thing_idx = 2;
    CHECK_FALSE(is_thing_passenger_controlled(thing));

    player->instance_num = PI_Unset;
    player->controlled_thing_idx = 1;
    CHECK(is_thing_passenger_controlled(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "is_thing_passenger_controlled is false for a neutral-owned thing", "[kfx_sim][player_instances]") {
    struct Thing *thing = make_creature(1, 1, PLAYER_NEUTRAL);
    CHECK_FALSE(is_thing_passenger_controlled(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "is_thing_directly_controlled matches the owning player's work_state/instance_num against the thing", "[kfx_sim][player_instances]") {
    struct Thing *thing = make_creature(1, 1, 0);
    struct PlayerInfo *player = get_player(0);

    player->work_state = PSt_CtrlDirect;
    player->instance_num = PI_DirctCtrl;
    player->influenced_thing_idx = 1;
    CHECK(is_thing_directly_controlled(thing));

    player->instance_num = PI_ZoomToPos; // falls through to final "return false"
    CHECK_FALSE(is_thing_directly_controlled(thing));

    player->work_state = PSt_FreeCtrlDirect;
    player->instance_num = PI_WhipEnd;
    player->controlled_thing_idx = 1;
    CHECK(is_thing_directly_controlled(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "is_thing_some_way_controlled is a direct controlled_thing_idx comparison", "[kfx_sim][player_instances]") {
    struct Thing *thing = make_creature(1, 1, 0);
    CHECK_FALSE(is_thing_some_way_controlled(thing));

    get_player(0)->controlled_thing_idx = 1;
    CHECK(is_thing_some_way_controlled(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "set_selected_thing_f/set_selected_creature_f/clear_selected_thing update controlled_thing_idx/_creatrn", "[kfx_sim][player_instances]") {
    struct PlayerInfo *player = get_player(0);
    struct Thing *thing = make_creature(1, 1, 0);
    thing->creation_turn = 42;

    CHECK(set_selected_thing_f(player, thing, "test"));
    CHECK(player->controlled_thing_idx == 1);
    CHECK(player->controlled_thing_creatrn == 42);

    CHECK(clear_selected_thing(player)); // always returns true
    CHECK(player->controlled_thing_idx == 0);
    CHECK(player->controlled_thing_creatrn == 0);

    CHECK(set_selected_creature_f(player, thing, "test"));
    CHECK(player->controlled_thing_idx == 1);

    struct Thing *not_a_creature = thing_get(2);
    not_a_creature->index = 2;
    not_a_creature->alloc_flags = TAlF_Exists;
    not_a_creature->class_id = TCls_Object;
    CHECK_FALSE(set_selected_creature_f(player, not_a_creature, "test"));
}

TEST_CASE_METHOD(ResetSimAndConfig, "is_thing_directly_controlled_by_player checks a specific player's control state, guarding an invalid player", "[kfx_sim][player_instances]") {
    struct Thing *thing = make_creature(1, 1, 0);
    struct PlayerInfo *player = get_player(0);

    player->work_state = PSt_CtrlDungeon;
    player->instance_num = PI_CrCtrlFade;
    player->controlled_thing_idx = 1;
    CHECK(is_thing_directly_controlled_by_player(thing, 0));
    CHECK_FALSE(is_thing_directly_controlled_by_player(thing, 1)); // player 1 has no such state

    player->instance_num = PI_DirctCtLeave;
    player->influenced_thing_idx = 1;
    CHECK(is_thing_directly_controlled_by_player(thing, 0));
}

TEST_CASE_METHOD(ResetSimAndConfig, "is_thing_passenger_controlled_by_player checks a specific player's passenger-control state", "[kfx_sim][player_instances]") {
    struct Thing *thing = make_creature(1, 1, 0);
    struct PlayerInfo *player = get_player(0);

    player->work_state = PSt_FreeCtrlPassngr;
    player->instance_num = PI_PsngrCtrl;
    player->influenced_thing_idx = 1;
    player->view_type = PVT_CreaturePasngr;
    CHECK(is_thing_passenger_controlled_by_player(thing, 0));

    player->view_type = PVT_CreatureContrl; // wrong view type for PI_PsngrCtrl
    CHECK_FALSE(is_thing_passenger_controlled_by_player(thing, 0));
}

TEST_CASE_METHOD(ResetSimAndConfig, "filter_creatures_owned_by_keepers maximizes for Keeper-owned things and excludes everyone else", "[kfx_sim][player_instances]") {
    struct Thing *thing = make_creature(1, 1, 0);
    CHECK(filter_creatures_owned_by_keepers(thing, nullptr, 0) == INT32_MAX); // player 0 defaults to PT_Keeper

    get_player(0)->player_type = PT_Roaming;
    CHECK(filter_creatures_owned_by_keepers(thing, nullptr, 0) == -1);
}
