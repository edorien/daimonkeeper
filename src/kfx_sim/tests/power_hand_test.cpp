// kfx_sim coverage: a first pass over power_hand.c's power-hand list
// bookkeeping -- unlike the linked lists tested elsewhere this session
// (mapwho chains, dungeon creature lists, ComputerTask lists),
// Dungeon::things_in_hand[] is a small fixed-size array with
// insert-at-front/remove-and-shift semantics capped by the per-player
// gameplay.max_things_in_hand config value, making insert/remove real
// array-shifting logic rather than pointer relinking.
//
// insert_thing_into_power_hand_list's thing_play_sample/
// remove_all_traces_of_combat/play_creature_sound side effects were
// confirmed safe against a zeroed fixture by running the test, not
// assumed from reading the source: the ports' unwired defaults cover the
// sound calls, and
// remove_all_traces_of_combat's cctrl->combat_flags==0 default skips its
// only real branch.
//
// Deliberately deferred: can_thing_be_picked_up_by_player/
// can_thing_be_picked_up2_by_player/thing_is_pickable_by_hand (need
// thing_pickup_is_blocked_by_hand_rule's HandRule config array on top of
// what's tested here), the drop/dump/draw functions (map+room fixture
// heavy), and the static hand_rule_* predicates (no external linkage).
#include <catch2/catch_test_macros.hpp>

#include "power_hand.h"
#include "dungeon_data.h"
#include "config_rules.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "power_hand_is_empty/power_hand_is_full read Dungeon::num_things_in_hand against the configured max", "[kfx_sim][power_hand]") {
    struct PlayerInfo *player = make_player_active(0);
    struct Dungeon *dungeon = get_dungeon(0);
    kfx_config_state.conf.rules[0].gameplay.max_things_in_hand = 2;

    CHECK(power_hand_is_empty(player));
    CHECK_FALSE(power_hand_is_full(player));

    dungeon->num_things_in_hand = 1;
    CHECK_FALSE(power_hand_is_empty(player));
    CHECK_FALSE(power_hand_is_full(player));

    dungeon->num_things_in_hand = 2;
    CHECK(power_hand_is_full(player));
}

TEST_CASE_METHOD(ResetSimAndConfig, "insert_thing_into_power_hand_list pushes onto the front, shifting existing entries up", "[kfx_sim][power_hand]") {
    kfx_config_state.conf.rules[0].gameplay.max_things_in_hand = 3;
    struct Thing *first = make_creature(1, 1, 0);
    struct Thing *second = make_creature(2, 2, 0);

    CHECK(insert_thing_into_power_hand_list(first, 0));
    CHECK(get_dungeon(0)->num_things_in_hand == 1);
    CHECK(get_dungeon(0)->things_in_hand[0] == 1);
    CHECK(first->holding_player == 0);

    CHECK(insert_thing_into_power_hand_list(second, 0));
    CHECK(get_dungeon(0)->num_things_in_hand == 2);
    CHECK(get_dungeon(0)->things_in_hand[0] == 2); // newest at the front
    CHECK(get_dungeon(0)->things_in_hand[1] == 1); // first thing shifted up
}

TEST_CASE_METHOD(ResetSimAndConfig, "insert_thing_into_power_hand_list refuses once the hand is full", "[kfx_sim][power_hand]") {
    kfx_config_state.conf.rules[0].gameplay.max_things_in_hand = 1;
    get_dungeon(0)->num_things_in_hand = 1;

    struct Thing *thing = make_creature(1, 1, 0);
    CHECK_FALSE(insert_thing_into_power_hand_list(thing, 0));
}

TEST_CASE_METHOD(ResetSimAndConfig, "thing_is_in_power_hand_list/get_thing_in_hand_id/get_first_thing_in_power_hand read the hand array directly", "[kfx_sim][power_hand]") {
    struct PlayerInfo *player = make_player_active(0);
    struct Dungeon *dungeon = get_dungeon(0);
    struct Thing *t1 = make_creature(1, 1, 0);
    struct Thing *t2 = make_creature(2, 2, 0);
    dungeon->things_in_hand[0] = 1;
    dungeon->things_in_hand[1] = 2;
    dungeon->num_things_in_hand = 2;

    CHECK(thing_is_in_power_hand_list(t1, 0));
    CHECK(thing_is_in_power_hand_list(t2, 0));
    CHECK(get_thing_in_hand_id(t1, 0) == 0);
    CHECK(get_thing_in_hand_id(t2, 0) == 1);
    CHECK(get_first_thing_in_power_hand(player) == t1);

    struct Thing *not_in_hand = make_creature(3, 3, 0);
    CHECK_FALSE(thing_is_in_power_hand_list(not_in_hand, 0));
    CHECK(get_thing_in_hand_id(not_in_hand, 0) == -1);
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_first_thing_from_power_hand_list shifts every remaining entry down by one", "[kfx_sim][power_hand]") {
    kfx_config_state.conf.rules[0].gameplay.max_things_in_hand = 3;
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->things_in_hand[0] = 1;
    dungeon->things_in_hand[1] = 2;
    dungeon->things_in_hand[2] = 3;
    dungeon->num_things_in_hand = 3;

    CHECK(remove_first_thing_from_power_hand_list(0));
    CHECK(dungeon->num_things_in_hand == 2);
    CHECK(dungeon->things_in_hand[0] == 2);
    CHECK(dungeon->things_in_hand[1] == 3);
    CHECK(dungeon->things_in_hand[2] == 0); // vacated slot cleared

    dungeon->num_things_in_hand = 0;
    CHECK_FALSE(remove_first_thing_from_power_hand_list(0)); // nothing to remove
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_thing_from_power_hand_list finds and removes a specific entry, shifting the tail down", "[kfx_sim][power_hand]") {
    kfx_config_state.conf.rules[0].gameplay.max_things_in_hand = 3;
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->things_in_hand[0] = 1;
    dungeon->things_in_hand[1] = 2;
    dungeon->things_in_hand[2] = 3;
    dungeon->num_things_in_hand = 3;
    struct Thing *middle = make_creature(2, 2, 0);

    CHECK(remove_thing_from_power_hand_list(middle, 0));
    CHECK(dungeon->num_things_in_hand == 2);
    CHECK(dungeon->things_in_hand[0] == 1);
    CHECK(dungeon->things_in_hand[1] == 3); // thing 3 shifted down into thing 2's slot

    struct Thing *not_present = make_creature(9, 9, 0);
    CHECK_FALSE(remove_thing_from_power_hand_list(not_present, 0));
}

TEST_CASE_METHOD(ResetSimAndConfig, "place_thing_in_limbo/remove_thing_from_limbo toggle TAlF_IsInLimbo and mapwho membership", "[kfx_sim][power_hand]") {
    struct Thing *thing = make_creature(1, 1, 0);

    place_thing_in_limbo(thing);
    CHECK((thing->alloc_flags & TAlF_IsInLimbo) != 0);
    CHECK((thing->rendering_flags & TRF_Invisible) != 0);

    remove_thing_from_limbo(thing);
    CHECK_FALSE((thing->alloc_flags & TAlF_IsInLimbo) != 0);
    CHECK_FALSE((thing->rendering_flags & TRF_Invisible) != 0);
    CHECK((thing->alloc_flags & TAlF_IsInMapWho) != 0); // re-inserted into the map
}

TEST_CASE_METHOD(ResetSimAndConfig, "object_is_pickable_by_hand_to_hold reads the OMF_HoldInHand model flag", "[kfx_sim][power_hand]") {
    struct Thing *obj = thing_get(1);
    obj->index = 1;
    obj->alloc_flags = TAlF_Exists;
    obj->class_id = TCls_Object;
    obj->model = 5;
    kfx_config_state.conf.object_conf.object_types_count = 6;

    CHECK_FALSE(object_is_pickable_by_hand_to_hold(obj));
    kfx_config_state.conf.object_conf.object_cfgstats[5].model_flags = OMF_HoldInHand;
    CHECK(object_is_pickable_by_hand_to_hold(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "armageddon_blocks_creature_pickup is true only once the armageddon countdown has elapsed", "[kfx_sim][power_hand]") {
    struct Thing *thing = make_creature(1, 1, 0);
    CHECK_FALSE(armageddon_blocks_creature_pickup(thing, 0)); // armageddon_cast_turn == 0: not cast

    kfx_sim_state.armageddon_cast_turn = 10;
    kfx_sim_state.armageddon_caster_idx = 0;
    kfx_config_state.conf.rules[0].magic.armageddon_count_down = 100;
    CHECK_FALSE(armageddon_blocks_creature_pickup(thing, 0)); // countdown (110) hasn't been reached (gameturn defaults to 0)
}
