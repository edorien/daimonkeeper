// kfx_sim coverage: player_computer.c's dungeon-list-counting helpers used
// throughout the computer-AI checks -- count_creatures_in_dungeon,
// count_diggers_in_dungeon, and count_entrances. These walk a Dungeon's
// creatr_list_start/digger_list_start (via CreatureControl::
// players_next_creature_idx) and the global entrance-room chain (via
// Room::next_of_kind) respectively -- both wired up here through the new
// link_creature_into_player_list/link_room_into_kind_list fixture helpers
// in kfx_sim_test_fixtures.h.
//
// Caught while wiring this up (confirmed empirically, not assumed from
// reading the source): count_creatures_in_dungeon's CREATURE_ANY wildcard
// resolves through creature_model_matches_model(), which excludes
// whatever get_players_spectator_model() returns -- and with a zeroed
// config that's model 0. A fixture creature left at its default model 0
// is therefore invisible to CREATURE_ANY and silently counts as zero; the
// tests below give their creatures a non-zero model to avoid that trap.
#include <catch2/catch_test_macros.hpp>

#include "player_computer.h"
#include "dungeon_data.h"
#include "room_data.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "count_creatures_in_dungeon counts every real creature threaded onto creatr_list_start", "[kfx_sim][player_computer]") {
    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(count_creatures_in_dungeon(dungeon) == 0);

    struct Thing *c1 = make_creature(1, 1, 0);
    c1->model = 1;
    link_creature_into_player_list(&dungeon->creatr_list_start, 1);
    CHECK(count_creatures_in_dungeon(dungeon) == 1);

    struct Thing *c2 = make_creature(2, 2, 0);
    c2->model = 1;
    link_creature_into_player_list(&dungeon->creatr_list_start, 2);
    CHECK(count_creatures_in_dungeon(dungeon) == 2);
}

TEST_CASE_METHOD(ResetSimAndConfig, "count_diggers_in_dungeon walks digger_list_start independently of creatr_list_start", "[kfx_sim][player_computer]") {
    struct Dungeon *dungeon = get_dungeon(0);
    struct Thing *creature = make_creature(1, 1, 0);
    creature->model = 1;
    link_creature_into_player_list(&dungeon->creatr_list_start, 1);

    CHECK(count_creatures_in_dungeon(dungeon) == 1);
    CHECK(count_diggers_in_dungeon(dungeon) == 0); // not on the digger list

    struct Thing *digger = make_creature(2, 2, 0);
    digger->model = 1;
    link_creature_into_player_list(&dungeon->digger_list_start, 2);
    CHECK(count_diggers_in_dungeon(dungeon) == 1);
}

TEST_CASE_METHOD(ResetSimAndConfig, "count_entrances counts entrance rooms owned by (or, for a negative player, any) player that aren't marked player_interested", "[kfx_sim][player_computer]") {
    struct Computer2 *comp = make_computer_player(0);

    struct Room *room1 = make_room_at_slab(1, 0, 0, RoK_ENTRANCE, 0);
    link_room_into_kind_list(&kfx_sim_state.entrance_room_id, 1);
    struct Room *room2 = make_room_at_slab(2, 1, 0, RoK_ENTRANCE, 1);
    link_room_into_kind_list(&kfx_sim_state.entrance_room_id, 2);
    CHECK(room2->owner == 1);

    CHECK(count_entrances(comp, -1) == 2); // negative plyr_idx counts every uninterested entrance
    CHECK(count_entrances(comp, 0) == 1);  // only room1
    CHECK(count_entrances(comp, 1) == 1);  // only room2

    room1->player_interested[computer_dungeon(comp)->owner] |= 0x01;
    CHECK(count_entrances(comp, 0) == 0);  // room1 excluded once marked interested
    CHECK(count_entrances(comp, -1) == 1); // only room2 remains
}
