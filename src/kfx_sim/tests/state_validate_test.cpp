// Security pass S06 (docs/security-pass/README.md): a simulation state that came from a file or a peer is checked
// before it replaces the running one (state_validate.h). Each case starts from the zeroed state, makes one thing
// wrong and expects the check to name it; the structure checks (a list that never ends) are the ones a field-by-field
// look would miss.
#include <catch2/catch_test_macros.hpp>

#include "state_validate.h"
#include "kfx_sim_state.h"
#include "thing_list.h"
#include "config_cubes.h"

#include <cstring>
#include <string>

namespace {

struct Zeroed {
    Zeroed() { std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state)); }
    ~Zeroed() { std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state)); }
};

std::string why_not()
{
    char why[256] = "";
    if (sim_state_validate(&kfx_sim_state, why, sizeof(why)))
        return "";
    return std::string(why).empty() ? "(no reason given)" : std::string(why);
}

bool refused_for(const char *field)
{
    const std::string why = why_not();
    INFO(why);
    return !why.empty() && (why.find(field) != std::string::npos);
}

/** A thing that exists, in its own slot. */
struct Thing &make_thing(int64_t i)
{
    struct Thing &t = kfx_sim_state.things_data[i];
    t.alloc_flags = TAlF_Exists;
    t.index = i;
    t.class_id = TCls_Creature;
    t.model = 1;
    return t;
}

} // namespace

TEST_CASE_METHOD(Zeroed, "an empty state is fine", "[kfx_sim][state_validate]") {
    CHECK(why_not().empty());
    CHECK(sim_state_validate(&kfx_sim_state, nullptr, 0));
}

TEST_CASE_METHOD(Zeroed, "a state with a few ordinary things is fine", "[kfx_sim][state_validate]") {
    struct Thing &a = make_thing(5);
    struct Thing &b = make_thing(6);
    a.next_on_mapblk = 6;
    b.prev_on_mapblk = 5;
    kfx_sim_state.map[100].mapwho = 5;
    kfx_sim_state.thing_lists[TngList_Creatures].index = 5;
    kfx_sim_state.thing_lists[TngList_Creatures].count = 2;
    a.next_of_class = 6;
    a.mappos.x.val = 3000;
    a.mappos.y.val = 4000;
    CHECK(why_not().empty());
}

TEST_CASE_METHOD(Zeroed, "an index past its array is named", "[kfx_sim][state_validate]") {
    struct Thing &t = make_thing(7);
    t.next_on_mapblk = THINGS_COUNT;
    CHECK(refused_for("next_on_mapblk"));
    t.next_on_mapblk = -1;
    CHECK(refused_for("next_on_mapblk"));
    t.next_on_mapblk = 0;
    CHECK(why_not().empty());
    t.parent_idx = 1 << 20;
    CHECK(refused_for("parent_idx"));
    t.parent_idx = 0;
    t.ccontrol_idx = CREATURES_COUNT;
    CHECK(refused_for("ccontrol_idx"));
    t.ccontrol_idx = CREATURES_COUNT - 1;
    CHECK(why_not().empty());
    t.class_id = 200;
    CHECK(refused_for("class_id"));
    t.class_id = TCls_Creature;
    t.model = CREATURE_TYPES_MAX;
    CHECK(refused_for("model"));
    t.model = 1;
    t.owner = PLAYERS_COUNT;
    CHECK(refused_for("owner"));
    t.owner = 0;
    t.mappos.x.val = 5000000;
    CHECK(refused_for("mappos"));
}

TEST_CASE_METHOD(Zeroed, "a thing that does not exist is not checked, one that says it is in the wrong slot is", "[kfx_sim][state_validate]") {
    kfx_sim_state.things_data[3].next_on_mapblk = 999999999; // a free slot: whatever it holds is not used
    CHECK(why_not().empty());
    struct Thing &t = make_thing(3);
    t.index = 4;
    CHECK(refused_for("index"));
}

TEST_CASE_METHOD(Zeroed, "the things on a map block must end", "[kfx_sim][state_validate]") {
    make_thing(10).next_on_mapblk = 11;
    make_thing(11).next_on_mapblk = 10; // 10 -> 11 -> 10 -> ...
    kfx_sim_state.map[50].mapwho = 10;
    CHECK(refused_for("never ends"));
    kfx_sim_state.things_data[11].next_on_mapblk = 0;
    CHECK(why_not().empty());
    // a thing freed while still on the block is tolerated (real play does it) as long as its link stays in the array
    kfx_sim_state.things_data[11].next_on_mapblk = 12;
    CHECK(why_not().empty());
    kfx_sim_state.things_data[12].next_on_mapblk = THINGS_COUNT + 5;
    CHECK(refused_for("freed thing"));
}

TEST_CASE_METHOD(Zeroed, "the lists of things by class must end", "[kfx_sim][state_validate]") {
    make_thing(20).next_of_class = 21;
    make_thing(21).next_of_class = 20;
    kfx_sim_state.thing_lists[TngList_Creatures].index = 20;
    kfx_sim_state.thing_lists[TngList_Creatures].count = 2;
    CHECK(refused_for("thing_lists"));
    kfx_sim_state.things_data[21].next_of_class = 0;
    CHECK(why_not().empty());
    kfx_sim_state.thing_lists[TngList_Creatures].count = THINGS_COUNT + 1;
    CHECK(refused_for("thing_lists.count"));
}

TEST_CASE_METHOD(Zeroed, "the none markers real levels use are accepted", "[kfx_sim][state_validate]") {
    struct Thing &t = make_thing(30);
    t.parent_idx = -1;
    CHECK(why_not().empty());
    t.parent_idx = 65535;
    CHECK(why_not().empty());
    t.parent_idx = 65534;
    CHECK(refused_for("parent_idx"));
    t.parent_idx = 0;
    kfx_sim_state.dungeon[2].current_research_idx = -1;
    CHECK(why_not().empty());
    kfx_sim_state.dungeon[2].current_research_idx = -2;
    CHECK(refused_for("current_research_idx"));
}

TEST_CASE_METHOD(Zeroed, "the lists of lights are lists of lights, and end", "[kfx_sim][state_validate]") {
    // an index into the light registry, which is a different table from the things
    kfx_sim_state.thing_lists[TngList_StaticLights].index = 44;
    kfx_sim_state.thing_lists[TngList_StaticLights].count = 2;
    kfx_sim_state.light_registry.lights[44].index = 44;
    kfx_sim_state.light_registry.lights[44].flags = LgtF_Allocated;
    kfx_sim_state.light_registry.lights[44].flags2 = LgtF2_InList;
    kfx_sim_state.light_registry.lights[44].next_in_list = 45;
    kfx_sim_state.light_registry.lights[45].index = 45;
    kfx_sim_state.light_registry.lights[45].flags = LgtF_Allocated;
    kfx_sim_state.light_registry.lights[45].flags2 = LgtF2_InList;
    CHECK(why_not().empty());
    kfx_sim_state.light_registry.lights[45].next_in_list = 44; // 44 -> 45 -> 44
    CHECK(refused_for("list of lights never ends"));
    kfx_sim_state.light_registry.lights[45].next_in_list = 0;
    kfx_sim_state.light_registry.lights[45].next_in_list = LIGHTS_COUNT;
    CHECK(refused_for("lights.next_in_list"));
    kfx_sim_state.light_registry.lights[45].next_in_list = 0;
    kfx_sim_state.thing_lists[TngList_DynamLights].index = LIGHTS_COUNT;
    CHECK(refused_for("thing_lists.index (lights)"));
}

TEST_CASE_METHOD(Zeroed, "a light on a list is marked as listed", "[kfx_sim][state_validate]") {
    // found by the corruption fuzzer: a listed light whose flags were cleared is added to the list a second time by
    // the next call that lists it, which makes the list loop
    kfx_sim_state.thing_lists[TngList_DynamLights].index = 60;
    kfx_sim_state.thing_lists[TngList_DynamLights].count = 1;
    kfx_sim_state.light_registry.lights[60].index = 60;
    kfx_sim_state.light_registry.lights[60].flags = LgtF_Allocated | LgtF_Dynamic;
    kfx_sim_state.light_registry.lights[60].flags2 = LgtF2_InList;
    CHECK(why_not().empty());
    kfx_sim_state.light_registry.lights[60].flags2 = 0;
    CHECK(refused_for("not marked as listed"));
}

TEST_CASE_METHOD(Zeroed, "a light that is in use is its own index", "[kfx_sim][state_validate]") {
    // found by the corruption fuzzer (ftest state_corruption_fuzz): a light whose index was overwritten hung the game
    // in light_remove_light_from_list(), which walks the list with no end test of its own
    kfx_sim_state.light_registry.lights[23].index = 23;
    CHECK(why_not().empty());
    kfx_sim_state.light_registry.lights[23].index = 7;
    CHECK(refused_for("lights[23].index"));
    kfx_sim_state.light_registry.lights[23].index = 0; // an unused slot
    CHECK(why_not().empty());
}

TEST_CASE_METHOD(Zeroed, "map blocks and slabs name columns, rooms and things that exist", "[kfx_sim][state_validate]") {
    kfx_sim_state.map[7].col_idx = COLUMNS_COUNT;
    CHECK(refused_for("map.col_idx"));
    kfx_sim_state.map[7].col_idx = COLUMNS_COUNT - 1;
    CHECK(why_not().empty());
    kfx_sim_state.map[7].mapwho = THINGS_COUNT;
    CHECK(refused_for("map.mapwho"));
    kfx_sim_state.map[7].mapwho = 0;
    kfx_sim_state.slabmap[9].room_index = ROOMS_COUNT;
    CHECK(refused_for("slabmap.room_index"));
    kfx_sim_state.slabmap[9].room_index = 0;
    kfx_sim_state.slabmap[9].next_in_room = (uint64_t)MAX_TILES_X * MAX_TILES_Y;
    CHECK(refused_for("slabmap.next_in_room"));
    kfx_sim_state.slabmap[9].next_in_room = 0;
    kfx_sim_state.columns_data[3].cubes[2] = CUBE_ITEMS_MAX;
    CHECK(refused_for("cubes"));
}

TEST_CASE_METHOD(Zeroed, "a room's slabs must end, and its links name rooms", "[kfx_sim][state_validate]") {
    struct Room &r = kfx_sim_state.rooms[4];
    r.alloc_flags = 1;
    r.index = 4;
    r.slabs_list = 30;
    kfx_sim_state.slabmap[30].next_in_room = 31;
    kfx_sim_state.slabmap[31].next_in_room = 30; // a loop
    CHECK(refused_for("never ends"));
    kfx_sim_state.slabmap[31].next_in_room = 0;
    CHECK(why_not().empty());
    r.next_of_owner = ROOMS_COUNT;
    CHECK(refused_for("next_of_owner"));
    r.next_of_owner = 0;
    CHECK(why_not().empty()); // (a room kind is a byte, so it can't leave the 256-entry table)
}

TEST_CASE_METHOD(Zeroed, "creature controls, dungeons, players and battles name things that exist", "[kfx_sim][state_validate]") {
    kfx_sim_state.cctrl_data[2].group_leader_idx = THINGS_COUNT;
    CHECK(refused_for("group_leader_idx"));
    kfx_sim_state.cctrl_data[2].group_leader_idx = 0;
    kfx_sim_state.cctrl_data[2].opponents_melee_count = COMBAT_MELEE_OPPONENTS_LIMIT + 1;
    CHECK(refused_for("opponents_melee_count"));
    kfx_sim_state.cctrl_data[2].opponents_melee_count = 0;
    kfx_sim_state.cctrl_data[2].arid.current_waypoint = ARID_WAYPOINTS_COUNT + 1;
    CHECK(refused_for("current_waypoint"));
    kfx_sim_state.cctrl_data[2].arid.current_waypoint = 0;
    kfx_sim_state.dungeon[1].num_things_in_hand = MAX_THINGS_IN_HAND + 1;
    CHECK(refused_for("num_things_in_hand"));
    kfx_sim_state.dungeon[1].num_things_in_hand = 0;
    kfx_sim_state.dungeon[1].digger_stack_length = DIGGER_TASK_MAX_COUNT + 1;
    CHECK(refused_for("digger_stack_length"));
    kfx_sim_state.dungeon[1].digger_stack_length = 0;
    kfx_sim_state.players[3].controlled_thing_idx = THINGS_COUNT;
    CHECK(refused_for("controlled_thing_idx"));
    kfx_sim_state.players[3].controlled_thing_idx = 0;
    kfx_sim_state.players[3].active_camera_idx = 4;
    CHECK(refused_for("active_camera_idx"));
    kfx_sim_state.players[3].active_camera_idx = 0;
    kfx_sim_state.battles[5].first_creatr = -2;
    CHECK(refused_for("battles.first_creatr"));
    kfx_sim_state.battles[5].first_creatr = 0;
    CHECK(why_not().empty());
}

TEST_CASE_METHOD(Zeroed, "a door's orientation is one of its two slab kinds", "[kfx_sim][state_validate]") {
    // found by AddressSanitizer in the cheat golden test: find_door_angle()'s -1 indexed slbkind[2]
    struct Thing &t = kfx_sim_state.things_data[40];
    t.index = 40;
    t.class_id = TCls_Door;
    t.alloc_flags = TAlF_Exists;
    t.door.orientation = 1;
    CHECK(why_not().empty());
    t.door.orientation = 2;
    CHECK(refused_for("door.orientation"));
    t.door.orientation = -1;
    CHECK(refused_for("door.orientation"));
}

TEST_CASE_METHOD(Zeroed, "a light is on the list its Dynamic flag names", "[kfx_sim][state_validate]") {
    // found by the corruption fuzzer: removing a light searches the list for its flag, so one on the other list is
    // never unlinked and the list loops
    struct Light &l = kfx_sim_state.light_registry.lights[5];
    l.index = 5;
    l.flags = LgtF_Allocated;
    l.flags2 = LgtF2_InList;
    kfx_sim_state.thing_lists[TngList_StaticLights].index = 5;
    kfx_sim_state.thing_lists[TngList_StaticLights].count = 1;
    CHECK(why_not().empty());
    l.flags = LgtF_Allocated | LgtF_Dynamic;
    CHECK(refused_for("wrong list"));
    l.flags = 0;
    CHECK(refused_for("not allocated"));
}

TEST_CASE_METHOD(Zeroed, "a computer player's wander points stay in their array and their loops stay short", "[kfx_sim][state_validate]") {
    // found by the corruption fuzzer: wander_point_add() writes points[point_insert_idx] unchecked, and the search
    // loops to num_check_per_run
    kfx_sim_state.players[2].wandr_within.point_insert_idx = 199;
    kfx_sim_state.players[2].wandr_within.points_count = 200;
    kfx_sim_state.players[2].wandr_within.num_check_per_run = 20;
    kfx_sim_state.players[2].wandr_within.max_found_per_check = 4;
    CHECK(why_not().empty());
    kfx_sim_state.players[2].wandr_within.point_insert_idx = 200;
    CHECK(refused_for("point_insert_idx"));
    kfx_sim_state.players[2].wandr_within.point_insert_idx = 0;
    kfx_sim_state.players[2].wandr_outside.num_check_per_run = 0xffff00000000ULL;
    CHECK(refused_for("num_check_per_run"));
    kfx_sim_state.players[2].wandr_outside.num_check_per_run = 0;
    kfx_sim_state.players[2].wandr_outside.points_count = 201;
    CHECK(refused_for("points_count"));
    kfx_sim_state.players[2].wandr_outside.points_count = 0;
    kfx_sim_state.players[2].wandr_outside.points[7].stl_x = MAX_SUBTILES_X + 1;
    CHECK(refused_for("points.stl_x"));
}

TEST_CASE_METHOD(Zeroed, "the free-thing stacks can't claim more than they hold", "[kfx_sim][state_validate]") {
    kfx_sim_state.synced_free_things_count = SYNCED_THINGS_COUNT + 1;
    CHECK(refused_for("synced_free_things_count"));
    kfx_sim_state.synced_free_things_count = 2;
    kfx_sim_state.synced_free_things[1] = THINGS_COUNT;
    CHECK(refused_for("synced_free_things"));
}

TEST_CASE_METHOD(Zeroed, "the map can't be larger than the arrays that hold it", "[kfx_sim][state_validate]") {
    kfx_sim_state.map_subtiles_x = MAX_SUBTILES_X + 1;
    CHECK(refused_for("map_subtiles_x"));
    kfx_sim_state.map_subtiles_x = MAX_SUBTILES_X;
    kfx_sim_state.map_tiles_y = -1;
    CHECK(refused_for("map_tiles_y"));
}
