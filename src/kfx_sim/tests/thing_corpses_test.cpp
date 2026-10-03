// kfx_sim coverage, per docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md's "still open" list: thing_corpses.c's
// corpse-classification predicates and the dead-creature-list bookkeeping
// (a ring buffer once dungeon->dead_creatures[] fills up, DEAD_CREATURES_
// MAX_COUNT==128). creature_can_be_resurrected/corpse_is_rottable's
// CMF_NoCorpseRotting/CMF_NoResurrect branches aren't driven true here:
// the corpses keep the zeroed thing->model 0, for which
// get_creature_model_flags() returns 0 flags (it short-circuits whenever
// model<1), so only their default (flags-clear) behavior is exercised.
#include <catch2/catch_test_macros.hpp>

#include "thing_corpses.h"
#include "thing_data.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "player_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    }
};
}

TEST_CASE_METHOD(ResetState, "corpse_is_rottable requires an existing TCls_DeadCreature thing not currently controlled by a player", "[kfx_sim][thing_corpses]") {
    struct Thing *thing = thing_get(1);
    CHECK_FALSE(corpse_is_rottable(thing)); // doesn't exist yet

    thing->alloc_flags = TAlF_Exists;
    thing->class_id = TCls_Object; // wrong class
    CHECK_FALSE(corpse_is_rottable(thing));

    thing->class_id = TCls_DeadCreature;
    CHECK(corpse_is_rottable(thing)); // uncontrolled -- INVALID_PLAYER is "invalid"

    thing->alloc_flags |= TAlF_IsControlled;
    thing->owner = 0;
    CHECK_FALSE(corpse_is_rottable(thing)); // controlled by a real player now
}

TEST_CASE_METHOD(ResetState, "corpse_ready_for_collection requires rottable, not-yet-laid-to-rest, undragged, and DCrSt_Dead", "[kfx_sim][thing_corpses]") {
    struct Thing *thing = thing_get(1);
    thing->alloc_flags = TAlF_Exists;
    thing->class_id = TCls_DeadCreature;
    thing->active_state = DCrSt_Dead;

    CHECK(corpse_ready_for_collection(thing));

    thing->corpse.laid_to_rest = 1;
    CHECK_FALSE(corpse_ready_for_collection(thing));
    thing->corpse.laid_to_rest = 0;

    thing->alloc_flags |= TAlF_IsDragged;
    CHECK_FALSE(corpse_ready_for_collection(thing));
    thing->alloc_flags &= ~TAlF_IsDragged;

    thing->active_state = DCrSt_Dying;
    CHECK_FALSE(corpse_ready_for_collection(thing));
}

TEST_CASE_METHOD(ResetState, "dead_creature_is_room_inventory requires both the DeadStorage role and a rottable corpse", "[kfx_sim][thing_corpses]") {
    struct Thing *thing = thing_get(1);
    thing->alloc_flags = TAlF_Exists;
    thing->class_id = TCls_DeadCreature;

    CHECK(dead_creature_is_room_inventory(thing, RoRoF_DeadStorage));
    CHECK_FALSE(dead_creature_is_room_inventory(thing, RoRoF_LairStorage)); // wrong role
}

TEST_CASE_METHOD(ResetState, "find_item_in_dead_creature_list returns -1 for an invalid dungeon or no match, else the matching index", "[kfx_sim][thing_corpses]") {
    CHECK(find_item_in_dead_creature_list(INVALID_DUNGEON, 1, 2) == -1);

    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->dead_creatures_count = 2;
    dungeon->dead_creatures[0].model = 1;
    dungeon->dead_creatures[0].exp_level = 2;
    dungeon->dead_creatures[1].model = 3;
    dungeon->dead_creatures[1].exp_level = 4;

    CHECK(find_item_in_dead_creature_list(dungeon, 1, 2) == 0);
    CHECK(find_item_in_dead_creature_list(dungeon, 3, 4) == 1);
    CHECK(find_item_in_dead_creature_list(dungeon, 1, 4) == -1); // model matches, exp_level doesn't
}

TEST_CASE_METHOD(ResetState, "add_item_to_dead_creature_list increments an existing entry's count rather than duplicating it", "[kfx_sim][thing_corpses]") {
    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(add_item_to_dead_creature_list(dungeon, 1, 2));
    CHECK(dungeon->dead_creatures_count == 1);
    CHECK(dungeon->dead_creatures[0].count == 0);

    CHECK(add_item_to_dead_creature_list(dungeon, 1, 2)); // same model+exp_level again
    CHECK(dungeon->dead_creatures_count == 1); // no new slot
    CHECK(dungeon->dead_creatures[0].count == 1); // just incremented
}

TEST_CASE_METHOD(ResetState, "add_item_to_dead_creature_list overwrites ring-buffer-style once dead_creatures_count hits DEAD_CREATURES_MAX_COUNT", "[kfx_sim][thing_corpses]") {
    struct Dungeon *dungeon = get_dungeon(0);
    for (int64_t i = 0; i < DEAD_CREATURES_MAX_COUNT; i++) {
        CHECK(add_item_to_dead_creature_list(dungeon, i + 1, 0)); // every model distinct -- always a fresh slot
    }
    CHECK(dungeon->dead_creatures_count == DEAD_CREATURES_MAX_COUNT);
    CHECK(dungeon->dead_creature_idx == 0);

    CHECK(add_item_to_dead_creature_list(dungeon, 999, 0)); // list is full -- overwrites slot 0
    CHECK(dungeon->dead_creatures_count == DEAD_CREATURES_MAX_COUNT); // unchanged, no growth
    CHECK(dungeon->dead_creature_idx == 1); // ring pointer advanced
    CHECK(dungeon->dead_creatures[0].model == 999);
}

TEST_CASE_METHOD(ResetState, "remove_item_from_dead_creature_list decrements a positive count without removing the slot", "[kfx_sim][thing_corpses]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->dead_creatures_count = 1;
    dungeon->dead_creatures[0].model = 1;
    dungeon->dead_creatures[0].exp_level = 2;
    dungeon->dead_creatures[0].count = 3;

    CHECK(remove_item_from_dead_creature_list(dungeon, 1, 2));
    CHECK(dungeon->dead_creatures[0].count == 2);
    CHECK(dungeon->dead_creatures_count == 1); // slot still occupied
}

TEST_CASE_METHOD(ResetState, "remove_item_from_dead_creature_list shifts later entries down once a zero-count slot is fully removed", "[kfx_sim][thing_corpses]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->dead_creatures_count = 2;
    dungeon->dead_creatures[0].model = 1;
    dungeon->dead_creatures[0].exp_level = 0;
    dungeon->dead_creatures[0].count = 0;
    dungeon->dead_creatures[1].model = 2;
    dungeon->dead_creatures[1].exp_level = 0;
    dungeon->dead_creatures[1].count = 0;

    CHECK(remove_item_from_dead_creature_list(dungeon, 1, 0));

    CHECK(dungeon->dead_creatures_count == 1);
    CHECK(dungeon->dead_creatures[0].model == 2); // shifted down from slot 1
    CHECK(find_item_in_dead_creature_list(dungeon, 1, 0) == -1); // gone
}

TEST_CASE_METHOD(ResetState, "remove_item_from_dead_creature_list returns false when nothing matches, or the dungeon is invalid", "[kfx_sim][thing_corpses]") {
    CHECK_FALSE(remove_item_from_dead_creature_list(INVALID_DUNGEON, 1, 0));

    struct Dungeon *dungeon = get_dungeon(0);
    CHECK_FALSE(remove_item_from_dead_creature_list(dungeon, 1, 0));
}

TEST_CASE_METHOD(ResetState, "update_dead_creatures_list adds the thing's model/exp_level via its CreatureControl", "[kfx_sim][thing_corpses]") {
    struct Dungeon *dungeon = get_dungeon(0);
    struct Thing *thing = thing_get(1);
    thing->model = 5;
    thing->ccontrol_idx = 1;
    creature_control_get(1)->exp_level = 3;

    CHECK(update_dead_creatures_list(dungeon, thing));
    CHECK(dungeon->dead_creatures[0].model == 5);
    CHECK(dungeon->dead_creatures[0].exp_level == 3);
}

TEST_CASE_METHOD(ResetState, "update_dead_creatures_list refuses a thing with no CreatureControl assigned", "[kfx_sim][thing_corpses]") {
    struct Dungeon *dungeon = get_dungeon(0);
    struct Thing *thing = thing_get(1);
    thing->ccontrol_idx = 0; // invalid

    CHECK_FALSE(update_dead_creatures_list(dungeon, thing));
}

TEST_CASE_METHOD(ResetState, "creature_can_be_resurrected is true by default (CMF_NoResurrect unset)", "[kfx_sim][thing_corpses]") {
    struct Thing *thing = thing_get(1);
    CHECK(creature_can_be_resurrected(thing));
}
