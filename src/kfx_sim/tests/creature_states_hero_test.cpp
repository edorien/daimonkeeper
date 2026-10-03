// kfx_sim "creature" cluster, hard-tail depth increment: the first test
// for creature_states_hero.c. This file is almost entirely hero-AI
// wandering/attack-target-selection/tunnelling logic reaching into
// navigation and dungeon-heart-reachability checks (creature_can_get_to_dungeon_heart,
// find_nearest_room_of_role_for_thing_with_spare_capacity) -- genuinely
// the hard tail, left for a later increment. Two functions are tractable
// without any of that: check_out_hero_has_money_for_treasure_room's
// cheap early-return, and is_hero_tunnelling_to_attack, which resolves
// through get_players_special_digger_model's default (non-roaming,
// zeroed digger breed config) fallback chain -- traced by reading the
// body rather than assumed, since it routes through two
// SimPort members (get_player_special_digger,
// player_is_roaming) before falling back to the plain
// special_digger_good/_evil config fields.
#include <catch2/catch_test_macros.hpp>

#include "creature_states_hero.h"
#include "creature_states.h"
#include "globals.h"
#include "kfx_sim_test_fixtures.h"
#include "player_availability.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "check_out_hero_has_money_for_treasure_room is a no-op for a hero carrying no gold", "[kfx_sim][creature_states_hero]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->creature.gold_carried = 0;

    CHECK(check_out_hero_has_money_for_treasure_room(thing) == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "is_hero_tunnelling_to_attack requires the player's special digger model and a tunnelling-related state", "[kfx_sim][creature_states_hero]") {
    struct Thing *thing = make_creature(1, 1, 0);
    // Default SimPort: get_player_special_digger returns 0,
    // player_is_roaming returns false -- falls back to special_digger_evil,
    // and (since that's also 0 by default) then to special_digger_good.
    kfx_config_state.conf.crtr_conf.special_digger_good = 7;
    thing->model = 3; // not the digger model yet

    thing->active_state = CrSt_Tunnelling;
    CHECK_FALSE(is_hero_tunnelling_to_attack(thing)); // wrong model

    thing->model = 7;
    CHECK(is_hero_tunnelling_to_attack(thing));

    thing->active_state = CrSt_CreatureSleep; // right model, unrelated state
    CHECK_FALSE(is_hero_tunnelling_to_attack(thing));

    thing->active_state = CrSt_TunnellerDoingNothing;
    CHECK(is_hero_tunnelling_to_attack(thing));
}
