// kfx_sim coverage: a broad-coverage pass over thing_objects.c's
// class/genre/model_flags predicate family -- the same shape as
// slab_data_test.cpp's pass over slab_data.c. These functions are all
// pure lookups against a Thing's class_id/model and the corresponding
// ObjectConfigStats entry (genre/model_flags), with no navigation or
// map-search involved, so they're tractable with a single fixture thing.
// The mapwho-linked-list traversal functions (find_gold_hoard_at) reuse
// the same "wire a Thing into a Map block's mapwho chain by hand" pattern
// established in creature_states_spdig_test.cpp.
//
// Deliberately deferred: the object update_* state machines
// (object_update_dungeon_heart, object_update_call_to_arms, etc.),
// create_object/create_gold_hoard_object/create_gold_pile (thing
// allocation + animation/sprite setup), gold hoard/treasury room capacity
// bookkeeping (add_gold_to_hoarde, gold_being_dropped_at_treasury), and
// thing_is_trap_crate/thing_is_door_crate (route through
// crate_thing_to_workshop_item_class's SimPort indirection
// rather than a direct config lookup) -- all of these need either a fuller
// room/animation fixture or a stubbed callback table, left for a later
// increment.
#include <catch2/catch_test_macros.hpp>

#include "thing_objects.h"
#include "config_objects.h"
#include "config_strings.h"
#include "config_terrain.h"
#include "config_rules.h"
#include "map_data.h"
#include "kfx_sim_test_fixtures.h"
#include "room_workshop.h"

using namespace kfx_test;

namespace {
struct Thing *make_object(ThingIndex idx, ThingModel model, PlayerNumber owner = 0)
{
    struct Thing *thing = thing_get(idx);
    thing->index = idx;
    thing->class_id = TCls_Object;
    thing->alloc_flags = TAlF_Exists;
    thing->model = model;
    thing->owner = owner;
    return thing;
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "thing_is_object requires a valid thing of class Object", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    CHECK(thing_is_object(obj));

    struct Thing *creature = make_creature(2, 2, 0);
    CHECK_FALSE(thing_is_object(creature));
}

TEST_CASE_METHOD(ResetSimAndConfig, "object_can_be_damaged is true only for the dungeon heart or (mature/growing) food", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    CHECK_FALSE(object_can_be_damaged(obj));

    kfx_config_state.conf.object_conf.object_types_count = 6;
    kfx_config_state.conf.object_conf.object_cfgstats[5].model_flags = OMF_Heart;
    CHECK(object_can_be_damaged(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].model_flags = 0;
    obj->model = ObjMdl_ChickenMature;
    CHECK(object_can_be_damaged(obj));

    obj->model = ObjMdl_ChickenGrowing;
    CHECK(object_can_be_damaged(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "thing_is_object_with_tooltip matches tooltip presence and optionality", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    kfx_config_state.conf.object_conf.object_types_count = 6;
    kfx_config_state.conf.object_conf.object_cfgstats[5].tooltip_stridx = GUIStr_Empty;
    CHECK_FALSE(thing_is_object_with_mandatory_tooltip(obj));
    CHECK_FALSE(thing_is_object_with_optional_tooltip(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].tooltip_stridx = GUIStr_Empty + 1;
    kfx_config_state.conf.object_conf.object_cfgstats[5].tooltip_optional = 0;
    CHECK(thing_is_object_with_mandatory_tooltip(obj));
    CHECK_FALSE(thing_is_object_with_optional_tooltip(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].tooltip_optional = 1;
    CHECK_FALSE(thing_is_object_with_mandatory_tooltip(obj));
    CHECK(thing_is_object_with_optional_tooltip(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "book_thing_to_power_kind looks up the object-to-power-artifact table, guarding invalid things and models", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    kfx_config_state.conf.object_conf.object_types_count = 6;
    kfx_config_state.conf.object_conf.object_to_power_artifact[5] = 7;
    CHECK(book_thing_to_power_kind(obj) == 7);

    struct Thing *creature = make_creature(2, 2, 0);
    CHECK(book_thing_to_power_kind(creature) == 0); // wrong class

    obj->model = 99; // out of range of object_types_count
    CHECK(book_thing_to_power_kind(obj) == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "thing_is_special_box/_hardcoded/_custom classify special boxes by genre and by hardcoded model list", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    kfx_config_state.conf.object_conf.object_types_count = 6;
    CHECK_FALSE(thing_is_special_box(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_SpecialBox;
    CHECK(thing_is_special_box(obj));
    CHECK_FALSE(thing_is_hardcoded_special_box(obj)); // model 5 isn't one of the hardcoded models
    CHECK(thing_is_custom_special_box(obj)); // special-box genre, but not hardcoded

    obj->model = ObjMdl_SpecboxRevealMap;
    CHECK(thing_is_hardcoded_special_box(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "thing_is_workshop_crate/thing_is_dungeon_heart/thing_is_beating_dungeon_heart read genre/model_flags", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    kfx_config_state.conf.object_conf.object_types_count = 6;

    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_WrkshpBox;
    CHECK(thing_is_workshop_crate(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_None;
    CHECK_FALSE(thing_is_workshop_crate(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].model_flags = OMF_Heart;
    CHECK(thing_is_dungeon_heart(obj));
    CHECK_FALSE(thing_is_beating_dungeon_heart(obj)); // OMF_Beating not set

    kfx_config_state.conf.object_conf.object_cfgstats[5].model_flags = OMF_Heart | OMF_Beating;
    CHECK(thing_is_beating_dungeon_heart(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "thing_is_mature_food/object_is_infant_food/_growing_food/_mature_food are direct model checks", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, ObjMdl_ChickenMature);
    CHECK(thing_is_mature_food(obj));
    CHECK(object_is_mature_food(obj));
    CHECK_FALSE(object_is_growing_food(obj));
    CHECK_FALSE(object_is_infant_food(obj));

    obj->model = ObjMdl_ChickenGrowing;
    CHECK(object_is_growing_food(obj));
    CHECK_FALSE(thing_is_mature_food(obj));

    obj->model = ObjMdl_ChickenStb; // one of the food_grow_objects[] infant models
    CHECK(object_is_infant_food(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "object_is_buoyant/thing_is_spellbook/thing_is_lair_totem/object_is_hero_gate read genre/model_flags", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    kfx_config_state.conf.object_conf.object_types_count = 6;

    kfx_config_state.conf.object_conf.object_cfgstats[5].model_flags = OMF_Buoyant;
    CHECK(object_is_buoyant(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].model_flags = 0;
    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_Spellbook;
    CHECK(thing_is_spellbook(obj));
    CHECK_FALSE(thing_is_lair_totem(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_LairTotem;
    CHECK(thing_is_lair_totem(obj));
    CHECK_FALSE(thing_is_spellbook(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_HeroGate;
    CHECK(object_is_hero_gate(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "object_is_gold/_gold_hoard/_gold_pile/_gold_laying_on_ground distinguish the gold representations", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    kfx_config_state.conf.object_conf.object_types_count = 6;
    CHECK_FALSE(object_is_gold(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_GoldHoard;
    CHECK(object_is_gold_hoard(obj));
    CHECK(object_is_gold(obj));
    CHECK(thing_is_gold_hoard(obj));
    CHECK_FALSE(object_is_gold_pile(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_Valuable;
    CHECK(object_is_gold_pile(obj));
    CHECK(object_is_gold(obj));
    CHECK_FALSE(object_is_gold_hoard(obj));

    obj->model = ObjMdl_Goldl;
    CHECK(object_is_gold_laying_on_ground(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "object_is_guard_flag recognizes every guard-flag colour model", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, ObjMdl_GuardFlagRed);
    CHECK(object_is_guard_flag(obj));
    obj->model = ObjMdl_GuardFlagPole;
    CHECK(object_is_guard_flag(obj));
    obj->model = ObjMdl_ChickenMature;
    CHECK_FALSE(object_is_guard_flag(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "object_is_room_equipment matches the fixed per-room-kind equipment model list", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, ObjMdl_Candlestick);
    CHECK(object_is_room_equipment(obj, RoK_TREASURE));
    CHECK_FALSE(object_is_room_equipment(obj, RoK_LIBRARY));

    obj->model = ObjMdl_TortureSpike;
    CHECK(object_is_room_equipment(obj, RoK_TORTURE));

    obj->model = ObjMdl_GuardFlagRed;
    CHECK(object_is_room_equipment(obj, RoK_GUARDPOST)); // delegates to object_is_guard_flag

    CHECK_FALSE(object_is_room_equipment(obj, RoK_ENTRANCE)); // "no objects" rooms always false
}

TEST_CASE_METHOD(ResetSimAndConfig, "object_is_room_inventory matches the requested role against the object's genre/kind", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    kfx_config_state.conf.object_conf.object_types_count = 6;
    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_GoldHoard;

    CHECK(object_is_room_inventory(obj, RoRoF_GoldStorage));
    CHECK_FALSE(object_is_room_inventory(obj, RoRoF_PowersStorage));

    kfx_config_state.conf.object_conf.object_cfgstats[5].model_flags = OMF_Heart;
    CHECK(object_is_room_inventory(obj, RoRoF_KeeperStorage));
}

TEST_CASE_METHOD(ResetSimAndConfig, "object_is_ignored_by_imps checks the OMF_IgnoredByImps model flag", "[kfx_sim][thing_objects]") {
    struct Thing *obj = make_object(1, 5);
    kfx_config_state.conf.object_conf.object_types_count = 6;
    CHECK_FALSE(object_is_ignored_by_imps(obj));

    kfx_config_state.conf.object_conf.object_cfgstats[5].model_flags = OMF_IgnoredByImps;
    CHECK(object_is_ignored_by_imps(obj));
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_wealth_size_types_count is fixed at the size of the gold_hoard_objects table", "[kfx_sim][thing_objects]") {
    CHECK(get_wealth_size_types_count() == 5);
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_wealth_size_of_gold_hoard_model/_object map a hoard model back to its 1-based wealth size", "[kfx_sim][thing_objects]") {
    CHECK(get_wealth_size_of_gold_hoard_model(ObjMdl_GoldHoard1) == 1);
    CHECK(get_wealth_size_of_gold_hoard_model(ObjMdl_GoldHoard5) == 5);
    CHECK(get_wealth_size_of_gold_hoard_model(ObjMdl_ChickenMature) == 0); // not a hoard model

    struct Thing *hoard = make_object(1, ObjMdl_GoldHoard3);
    CHECK(get_wealth_size_of_gold_hoard_object(hoard) == 3);
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_wealth_size_of_gold_amount scales gold amount into a 1..types_count wealth size, clamped at the top", "[kfx_sim][thing_objects]") {
    kfx_config_state.conf.rules[0].gameplay.gold_per_hoard = 2000; // 400 gold per wealth-size step

    CHECK(get_wealth_size_of_gold_amount(1) == 1);
    CHECK(get_wealth_size_of_gold_amount(400) == 1);
    CHECK(get_wealth_size_of_gold_amount(401) == 2);
    CHECK(get_wealth_size_of_gold_amount(2000) == 5);
    CHECK(get_wealth_size_of_gold_amount(999999) == 5); // clamped to types_count
}

TEST_CASE_METHOD(ResetSimAndConfig, "find_gold_hoard_at walks the mapwho chain and returns the first gold hoard object", "[kfx_sim][thing_objects]") {
    kfx_config_state.conf.object_conf.object_types_count = 6;
    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_GoldHoard;

    CHECK(thing_is_invalid(find_gold_hoard_at(3, 3))); // nothing on the map block yet

    struct Thing *not_hoard = make_object(1, ObjMdl_ChickenMature);
    not_hoard->next_on_mapblk = 2;
    struct Thing *hoard = make_object(2, 5);
    get_map_block_at(3, 3)->mapwho = 1;

    struct Thing *found = find_gold_hoard_at(3, 3);
    CHECK(found == hoard);
}

TEST_CASE_METHOD(ResetSimAndConfig, "gold_object_typical_value/add_gold_to_pile scale a pile's sprite by its stored gold relative to the typical value", "[kfx_sim][thing_objects]") {
    struct Thing *pile = make_object(1, ObjMdl_Goldl);
    kfx_config_state.conf.rules[0].gameplay.gold_pile_value = 100;
    CHECK(gold_object_typical_value(pile) == 100);

    // typical_value <= 0: add_gold_to_pile is a no-op that reports false.
    kfx_config_state.conf.rules[0].gameplay.gold_pile_value = 0;
    pile->valuable.gold_stored = 0;
    CHECK_FALSE(add_gold_to_pile(pile, 50));
    CHECK(pile->valuable.gold_stored == 0);

    kfx_config_state.conf.rules[0].gameplay.gold_pile_value = 100;
    CHECK(add_gold_to_pile(pile, 50));
    CHECK(pile->valuable.gold_stored == 50);
}
