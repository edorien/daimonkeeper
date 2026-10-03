// kfx_sim "creature" cluster, hard-tail depth increment: the first test
// for creature_states_spdig.c. This 2100-line special-digger (imp) state
// machine is almost entirely map-search (check_out_unclaimed_*,
// check_out_*_drop_place) and navigation (setup_person_move_to_position,
// creature_move_to) driven -- genuinely the hard tail, left for a later
// increment. A handful of functions are pure field/config lookups with no
// navigation dependency, verified by reading their bodies (and the
// dependencies they call) before writing each test:
//   - slab_is_my_door routes through get_slab_stats(); the fixture's slab
//     is kind 0, so the test configures slab_cfgstats[0].
//   - digger_work_experience is a straight creature_can_gain_experience()
//     gate (already exercised via creature_states_train_test.cpp) over a
//     per-player config value.
//   - too_much_gold_lies_around_thing and take_from_gold_pile both walk a
//     Map block's mapwho linked list of Things; the fixture below wires up
//     a single gold-pile object thing into that list by hand. Neither
//     object_is_gold_laying_on_ground (ObjMdl_Goldl-only) nor
//     object_is_gold_pile (needs object_cfgstats[model].genre) touches
//     navigation, and take_from_gold_pile's destroy_object()/
//     delete_thing_structure() path was traced to confirm it's a safe
//     no-op for a thing that was never flagged TAlF_IsInMapWho/
//     TAlF_IsInStrucList (this fixture's object isn't).
//   - creature_is_dragging_or_being_dragged/set_creature_being_dragged_by
//     operate purely on CreatureControl::dragtng_idx.
#include <catch2/catch_test_macros.hpp>

#include "creature_states_spdig.h"
#include "creature_control.h"
#include "thing_objects.h"
#include "map_data.h"
#include "slab_data.h"
#include "config_rules.h"
#include "config_objects.h"
#include "config_terrain.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "slab_is_my_door requires both matching ownership and the door flag", "[kfx_sim][creature_states_spdig]") {
    get_slabmap_block(1, 1)->owner = 2;
    kfx_config_state.conf.slab_conf.slab_cfgstats[0].block_flags = 0;
    CHECK_FALSE(slab_is_my_door(2, 1, 1)); // no door flag

    kfx_config_state.conf.slab_conf.slab_cfgstats[0].block_flags = SlbAtFlg_IsDoor;
    CHECK(slab_is_my_door(2, 1, 1));
    CHECK_FALSE(slab_is_my_door(3, 1, 1)); // wrong owner
}

TEST_CASE_METHOD(ResetSimAndConfig, "digger_work_experience yields the per-player config value only when the creature can gain experience", "[kfx_sim][creature_states_spdig]") {
    struct Thing *thing = make_creature(1, 1, 0);
    kfx_config_state.conf.rules[0].workers.digger_work_experience = 5;

    CHECK(digger_work_experience(thing) == 0); // not eligible: dungeon->creature_max_level[model] defaults to 0, so exp_level(0) >= 0

    get_dungeon(0)->creature_max_level[thing->model] = 10;
    CHECK(digger_work_experience(thing) == 5);
}

TEST_CASE_METHOD(ResetSimAndConfig, "too_much_gold_lies_around_thing checks the gold pile at the thing's own position against the per-player maximum", "[kfx_sim][creature_states_spdig]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->mappos.x.stl.num = 3;
    thing->mappos.y.stl.num = 3;

    struct Thing *pile = thing_get(2);
    pile->index = 2;
    pile->alloc_flags = TAlF_Exists;
    pile->class_id = TCls_Object;
    pile->model = ObjMdl_Goldl;
    pile->owner = 0;
    pile->valuable.gold_stored = 100;
    get_map_block_at(3, 3)->mapwho = 2;

    kfx_config_state.conf.rules[0].gameplay.gold_pile_maximum = 200;
    CHECK_FALSE(too_much_gold_lies_around_thing(thing));

    kfx_config_state.conf.rules[0].gameplay.gold_pile_maximum = 50;
    CHECK(too_much_gold_lies_around_thing(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "take_from_gold_pile removes gold piles at a subtile up to the requested limit", "[kfx_sim][creature_states_spdig]") {
    struct Thing *pile = thing_get(2);
    pile->index = 2;
    pile->alloc_flags = TAlF_Exists;
    pile->class_id = TCls_Object;
    pile->model = ObjMdl_Goldl;
    pile->valuable.gold_stored = 100;
    get_map_block_at(3, 3)->mapwho = 2;
    kfx_config_state.conf.object_conf.object_types_count = ObjMdl_Goldl + 1;
    kfx_config_state.conf.object_conf.object_cfgstats[ObjMdl_Goldl].genre = OCtg_Valuable;

    // limit big enough to take the whole pile: it's destroyed outright.
    // (destroy_object's delete_thing_structure() only unlinks the thing from
    // the mapwho chain when TAlF_IsInMapWho is set -- this fixture's pile
    // isn't, so the check is on the thing slot itself being wiped.)
    CHECK(take_from_gold_pile(3, 3, 1000) == 100);
    CHECK(pile->alloc_flags == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "take_from_gold_pile leaves the remainder in the pile when the limit is smaller", "[kfx_sim][creature_states_spdig]") {
    struct Thing *pile = thing_get(2);
    pile->index = 2;
    pile->alloc_flags = TAlF_Exists;
    pile->class_id = TCls_Object;
    pile->model = ObjMdl_Goldl;
    pile->valuable.gold_stored = 100;
    get_map_block_at(3, 3)->mapwho = 2;
    kfx_config_state.conf.object_conf.object_types_count = ObjMdl_Goldl + 1;
    kfx_config_state.conf.object_conf.object_cfgstats[ObjMdl_Goldl].genre = OCtg_Valuable;

    CHECK(take_from_gold_pile(3, 3, 40) == 40);
    CHECK(pile->valuable.gold_stored == 60); // 100 - 40 taken
    CHECK(get_map_block_at(3, 3)->mapwho == 2); // pile itself remains
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_dragging_or_being_dragged reflects CreatureControl::dragtng_idx", "[kfx_sim][creature_states_spdig]") {
    struct Thing *thing = make_creature(1, 1, 0);
    CHECK_FALSE(creature_is_dragging_or_being_dragged(thing));

    creature_control_get(1)->dragtng_idx = 7;
    CHECK(creature_is_dragging_or_being_dragged(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "set_creature_being_dragged_by links the carrier and the dragged creature's dragtng_idx fields both ways", "[kfx_sim][creature_states_spdig]") {
    // Call convention (from thing_creature.c's set_creature_being_dragged_by(picktng, creatng)):
    // first arg is the thing being picked up/dragged, second is the carrier creature.
    struct Thing *draggedThing = make_creature(1, 1, 0);
    struct Thing *carrier = make_creature(2, 2, 0);

    set_creature_being_dragged_by(draggedThing, carrier);

    CHECK(creature_control_get(2)->dragtng_idx == 1); // carrier's cctrl now points at what it's dragging
    CHECK(creature_control_get(1)->dragtng_idx == 2); // dragged thing's cctrl points back at its carrier
    CHECK((draggedThing->state_flags & TF1_IsDragged1) != 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "set_creature_being_dragged_by refuses when the carrier is already dragging something else", "[kfx_sim][creature_states_spdig]") {
    struct Thing *draggedThing = make_creature(1, 1, 0);
    struct Thing *carrier = make_creature(2, 2, 0);
    make_creature(3, 3, 0); // `other`, already being dragged by carrier

    creature_control_get(2)->dragtng_idx = 3; // carrier is already dragging `other`
    CHECK_FALSE(set_creature_being_dragged_by(draggedThing, carrier));
}
