// kfx_sim coverage, per docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md's "still open" list: thing_doors.c's pure
// door-angle geometry (determine_door_angle/get_door_orientation/
// find_door_angle), thing_is_deployed_door/_is_sellable_door, and
// door_can_stand. Reuses the small map+slabmap fixture shape
// room_garden_test.cpp introduced, extended with one Column: a wall
// subtile is any subtile whose Map block's col_idx points at a Column
// with floor_filled_subtiles>=COLUMN_WALL_HEIGHT (subtile_is_wall's own
// check) -- every subtile defaults to col_idx 0 (a zeroed, non-wall
// Column), so only the specific neighbor subtiles under test need their
// own column_idx pointed at the shared "wall" Column (index 1).
//
// door_can_stand's slabst->category check reads the fixture's
// slab_cfgstats, which are zeroed (category SlbAtCtg_Unclaimed, not a wall
// category) for every kind. Until refactor pass 2's S15 get_slab_stats()
// reached the slab through an unwired port here and always resolved to
// slab_cfgstats[0]; it now reads the real slab kind. The direct slb->kind
// checks (SlbT_ROCK and friends) are what these tests drive: SlbT_ROCK is
// 0, so a freshly-zeroed slabmap is already "solid" everywhere and needs
// explicit clearing to test the "can stand" branches.
//
// The rest of thing_doors.c (door creation/locking/opening state machine)
// needs a full Thing+CreatureControl fixture and is left for a later
// increment.
#include <catch2/catch_test_macros.hpp>

#include "thing_doors.h"
#include "thing_data.h"
#include "map_data.h"
#include "map_columns.h"
#include "slab_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct DoorAngleFixture {
    // Slab (1,1)'s center subtile is (4,4) -- slab_subtile_center(1) ==
    // 1*STL_PER_SLB+1 == 4. determine_door_angle probes the four subtiles
    // 2 subtiles out from there in each cardinal direction.
    static constexpr MapSubtlCoord kCenterX = 4;
    static constexpr MapSubtlCoord kCenterY = 4;

    DoorAngleFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_sim_state.map_subtiles_x = 10;
        kfx_sim_state.map_subtiles_y = 10;
        kfx_sim_state.map_tiles_x = 4;
        kfx_sim_state.map_tiles_y = 4;

        // A shared "wall" column: floor_filled_subtiles (bitfields' top
        // nibble) >= COLUMN_WALL_HEIGHT (5).
        kfx_sim_state.columns_data[1].bitfields = 0x50;
    }

    void mark_wall(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
        set_mapblk_column_index(get_map_block_at(stl_x, stl_y), 1);
    }
};
}

TEST_CASE_METHOD(DoorAngleFixture, "determine_door_angle resolves a north-south wall pair to angle 1", "[kfx_sim][thing_doors]") {
    mark_wall(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY - 2); // north
    mark_wall(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY + 2); // south

    CHECK(determine_door_angle(1, 1) == 1);
}

TEST_CASE_METHOD(DoorAngleFixture, "determine_door_angle resolves an east-west wall pair to angle 0", "[kfx_sim][thing_doors]") {
    mark_wall(DoorAngleFixture::kCenterX + 2, DoorAngleFixture::kCenterY); // east
    mark_wall(DoorAngleFixture::kCenterX - 2, DoorAngleFixture::kCenterY); // west

    CHECK(determine_door_angle(1, 1) == 0);
}

TEST_CASE_METHOD(DoorAngleFixture, "determine_door_angle returns -1 when no neighboring subtile is a wall", "[kfx_sim][thing_doors]") {
    CHECK(determine_door_angle(1, 1) == -1);
}

TEST_CASE_METHOD(DoorAngleFixture, "determine_door_angle returns -1 when every neighboring subtile is a wall", "[kfx_sim][thing_doors]") {
    mark_wall(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY - 2);
    mark_wall(DoorAngleFixture::kCenterX + 2, DoorAngleFixture::kCenterY);
    mark_wall(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY + 2);
    mark_wall(DoorAngleFixture::kCenterX - 2, DoorAngleFixture::kCenterY);

    CHECK(determine_door_angle(1, 1) == -1);
}

TEST_CASE_METHOD(DoorAngleFixture, "get_door_orientation returns -1 for a slab that isn't a door, regardless of walls", "[kfx_sim][thing_doors]") {
    mark_wall(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY - 2);
    mark_wall(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY + 2);
    // slab_types_count defaults to 0 -- get_slab_kind_stats falls back to
    // slab_cfgstats[0], whose block_flags is zeroed (no SlbAtFlg_IsDoor).

    CHECK(get_door_orientation(1, 1) == -1);
}

TEST_CASE_METHOD(DoorAngleFixture, "get_door_orientation delegates to determine_door_angle once the slab is configured as a door", "[kfx_sim][thing_doors]") {
    struct SlabMap *slb = get_slabmap_block(1, 1);
    slb->kind = 1;
    kfx_config_state.conf.slab_conf.slab_types_count = 2;
    kfx_config_state.conf.slab_conf.slab_cfgstats[1].block_flags = SlbAtFlg_IsDoor;
    mark_wall(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY - 2);
    mark_wall(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY + 2);

    CHECK(get_door_orientation(1, 1) == 1);
}

TEST_CASE_METHOD(DoorAngleFixture, "find_door_angle refuses a slab that isn't SlbT_CLAIMED", "[kfx_sim][thing_doors]") {
    struct SlabMap *slb = get_slabmap_block(1, 1);
    slb->kind = SlbT_ROCK;
    slb->owner = 0;

    CHECK(find_door_angle(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY, 0) == -1);
}

TEST_CASE_METHOD(DoorAngleFixture, "find_door_angle refuses a claimed slab owned by a different player", "[kfx_sim][thing_doors]") {
    struct SlabMap *slb = get_slabmap_block(1, 1);
    slb->kind = SlbT_CLAIMED;
    slb->owner = 3;

    CHECK(find_door_angle(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY, 0) == -1);
}

TEST_CASE_METHOD(DoorAngleFixture, "find_door_angle delegates to determine_door_angle for the caller's own claimed slab", "[kfx_sim][thing_doors]") {
    struct SlabMap *slb = get_slabmap_block(1, 1);
    slb->kind = SlbT_CLAIMED;
    slb->owner = 0;
    mark_wall(DoorAngleFixture::kCenterX + 2, DoorAngleFixture::kCenterY);
    mark_wall(DoorAngleFixture::kCenterX - 2, DoorAngleFixture::kCenterY);

    CHECK(find_door_angle(DoorAngleFixture::kCenterX, DoorAngleFixture::kCenterY, 0) == 0);
}

TEST_CASE_METHOD(DoorAngleFixture, "thing_is_deployed_door requires the thing to exist and be class TCls_Door", "[kfx_sim][thing_doors]") {
    struct Thing *doortng = thing_get(1);
    CHECK_FALSE(thing_is_deployed_door(doortng)); // doesn't exist yet

    doortng->alloc_flags = TAlF_Exists;
    doortng->class_id = TCls_Object; // wrong class
    CHECK_FALSE(thing_is_deployed_door(doortng));

    doortng->class_id = TCls_Door;
    CHECK(thing_is_deployed_door(doortng));
}

TEST_CASE_METHOD(DoorAngleFixture, "thing_is_sellable_door checks class and the configured unsellable flag", "[kfx_sim][thing_doors]") {
    kfx_config_state.conf.trapdoor_conf.door_types_count = 2;
    struct Thing *doortng = thing_get(1);
    doortng->class_id = TCls_Door;
    doortng->model = 1;

    kfx_config_state.conf.trapdoor_conf.door_cfgstats[1].unsellable = 0;
    CHECK(thing_is_sellable_door(doortng));

    kfx_config_state.conf.trapdoor_conf.door_cfgstats[1].unsellable = 1;
    CHECK_FALSE(thing_is_sellable_door(doortng));

    doortng->class_id = TCls_Object;
    CHECK_FALSE(thing_is_sellable_door(doortng)); // wrong class, regardless of unsellable
}

TEST_CASE_METHOD(DoorAngleFixture, "door_can_stand is false against a freshly-zeroed (all-SlbT_ROCK) slabmap", "[kfx_sim][thing_doors]") {
    struct Thing *thing = thing_get(1);
    thing->mappos.x.val = subtile_coord_center(4); // slab (1,1)
    thing->mappos.y.val = subtile_coord_center(4);

    CHECK_FALSE(door_can_stand(thing)); // all four neighbors are SlbT_ROCK -- no valid door_angle for "surrounded on all sides"
}

TEST_CASE_METHOD(DoorAngleFixture, "door_can_stand is true when flanked by solid rock on two opposite sides", "[kfx_sim][thing_doors]") {
    struct Thing *thing = thing_get(1);
    thing->mappos.x.val = subtile_coord_center(4); // slab (1,1)
    thing->mappos.y.val = subtile_coord_center(4);
    // Clear the east (2,1) and west (0,1) neighbor slabs -- north (1,0) and
    // south (1,2) stay at the default SlbT_ROCK (0).
    get_slabmap_block(2, 1)->kind = SlbT_CLAIMED;
    get_slabmap_block(0, 1)->kind = SlbT_CLAIMED;

    CHECK(door_can_stand(thing));
}

TEST_CASE_METHOD(DoorAngleFixture, "door_can_stand is false when no neighboring slab is solid", "[kfx_sim][thing_doors]") {
    struct Thing *thing = thing_get(1);
    thing->mappos.x.val = subtile_coord_center(4);
    thing->mappos.y.val = subtile_coord_center(4);
    get_slabmap_block(1, 0)->kind = SlbT_CLAIMED; // north
    get_slabmap_block(2, 1)->kind = SlbT_CLAIMED; // east
    get_slabmap_block(1, 2)->kind = SlbT_CLAIMED; // south
    get_slabmap_block(0, 1)->kind = SlbT_CLAIMED; // west

    CHECK_FALSE(door_can_stand(thing));
}
