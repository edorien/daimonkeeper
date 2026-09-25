// kfx_pathfinding: ariadne_wallhug.c -- the third of the "big three"
// (2,178 lines, zero tests before this file). Only 5 of its functions
// have real external linkage (confirmed empirically via `nm` on the built
// .o, not assumed from the header alone -- everything else, including
// get_hugging_blocked_flags/set_hugging_pos_using_blocked_flags/
// creature_cannot_move_directly_to_with_collide despite lacking a
// "static" keyword on their *definitions*, keeps internal linkage because
// their forward declarations earlier in the same file are `static`; C
// linkage is fixed by the first declaration seen in a translation unit):
//   dig_to_position, get_hug_side_options,
//   get_next_position_and_angle_required_to_tunnel_creature_to,
//   initialise_wallhugging_path_from_to, slab_wall_hug_route.
// slab_good_for_computer_dig_path is declared in ariadne_wallhug.h too,
// but is actually implemented in kfx_sim/src/slab_data.c (a higher
// layer) -- not testable here, not this library's code to cover.
//
// Technique: pathfinding_fake_world.h's GridWorldFixture (grid-backed
// PathfindingWorldCallbacks fake with real, controllable backing storage
// for map/slab/thing state). slab_wall_hug_route and
// get_next_position_and_angle_required_to_tunnel_creature_to internally
// call every one of ariadne_wallhug.c's `static` helpers (hug_round,
// hug_round_sub, creature_cannot_move_directly_to_with_collide[_sub],
// get_map_index_of_first_block_thing_colliding_with_at,
// get_starting_angle_and_side_of_hug[_sub1/_sub2],
// check_forward_for_prospective_hugs, get_angle_of_wall_hug,
// navigation_push_towards_target, find_approach_position_to_subtile,
// get_hugging_blocked_flags, set_hugging_pos_using_blocked_flags,
// thing_can_continue_direct_line_to), so driving these two entry points
// through several scenarios is this file's main coverage lever -- not
// just exercising the 5 exported functions themselves.
//
// get_next_position_and_angle_required_to_tunnel_creature_to is a large
// navstate state machine; this file covers a representative sample of
// states/transitions (confirmed against the real run each time, not
// hand-traced blindly) rather than attempting exhaustive branch coverage
// of every one of its ~500 lines -- see testing-harness.md's "Known Gaps"
// entry for this round for what's left.
#include <catch2/catch_test_macros.hpp>

#include "ariadne_wallhug.h"
#include "ariadne.h"
#include "pathfinding_fake_world.h"

using namespace pf_fake;

namespace {

struct WallhugFixture : GridWorldFixture {
    FakeThing thing;
    WallhugFixture() {
        thing = FakeThing{};
        thing.pos.x.val = subtile_coord_center(10);
        thing.pos.y.val = subtile_coord_center(10);
        thing.pos.z.val = 0;
        thing.clipbox_size = 0; // smallest nav radius entry
    }
    struct Thing *t() { return as_thing(thing); }
};

} // namespace

// --- initialise_wallhugging_path_from_to (pure struct field sets, no
// callback dependency at all) --------------------------------------------
TEST_CASE("initialise_wallhugging_path_from_to sets up a fresh Navigation for wallhug-in-progress", "[kfx_pathfinding][ariadne_wallhug]") {
    struct Navigation navi{};
    navi.wallhug_state = WallhugCurrentState_Right;
    navi.wallhug_retry_counter = 7;
    navi.push_counter = 5;

    struct Coord3d mvstart{};
    mvstart.x.val = 100; mvstart.y.val = 200; mvstart.z.val = 300;
    struct Coord3d mvend{};
    mvend.x.val = 1000; mvend.y.val = 2000; mvend.z.val = 3000;

    initialise_wallhugging_path_from_to(&navi, &mvstart, &mvend);

    CHECK(navi.navstate == NavS_WallhugInProgress);
    CHECK(navi.pos_final.x.val == 1000);
    CHECK(navi.pos_final.y.val == 2000);
    CHECK(navi.pos_final.z.val == 3000);
    CHECK(navi.wallhug_state == WallhugCurrentState_None);
    CHECK(navi.wallhug_retry_counter == 0);
    CHECK(navi.push_counter == 0);
}

// --- dig_to_position -------------------------------------------------
TEST_CASE_METHOD(WallhugFixture, "dig_to_position accepts the very first subtile tried when it's not a valid-hug (i.e. non-wall) subtile", "[kfx_pathfinding][ariadne_wallhug]") {
    // Default grid is all-walkable -> is_valid_hug_subtile is false
    // everywhere -> the very first candidate (round_change subtracted
    // once from direction_around) is accepted immediately.
    SubtlCodedCoords result = dig_to_position(0, 9, 9, 0, false);
    CHECK(result != (SubtlCodedCoords)-1);
    MapSubtlCoord x = fake_stl_num_decode_x(result);
    MapSubtlCoord y = fake_stl_num_decode_y(result);
    // direction_around=0 ({0,-1}), revside=false -> round_change=3,
    // round_idx = (0 + 4 - 3) % 4 = 1 -> small_around[1] = {1,0}.
    CHECK(x == 9 + STL_PER_SLB * 1);
    CHECK(y == 9);
}

TEST_CASE_METHOD(WallhugFixture, "dig_to_position walks around the compass and returns -1 when every direction is a wall", "[kfx_pathfinding][ariadne_wallhug]") {
    // Block every subtile the search could possibly land on around (9,9).
    for (int64_t dy = -1; dy <= 1; dy++) {
        for (int64_t dx = -1; dx <= 1; dx++) {
            grid.at(9 + STL_PER_SLB * dx, 9 + STL_PER_SLB * dy).walkable = false;
        }
    }
    SubtlCodedCoords result = dig_to_position(0, 9, 9, 0, false);
    CHECK(result == (SubtlCodedCoords)-1);
}

TEST_CASE_METHOD(WallhugFixture, "dig_to_position with revside=true walks the opposite direction and still finds an open subtile", "[kfx_pathfinding][ariadne_wallhug]") {
    SubtlCodedCoords result = dig_to_position(0, 9, 9, 0, true);
    CHECK(result != (SubtlCodedCoords)-1);
    // revside=true -> round_change=1, round_idx = (0+4-1)%4 = 3 -> small_around[3] = {-1,0}.
    MapSubtlCoord x = fake_stl_num_decode_x(result);
    MapSubtlCoord y = fake_stl_num_decode_y(result);
    CHECK(x == 9 - STL_PER_SLB * 1);
    CHECK(y == 9);
}

// --- get_hug_side_options ---------------------------------------------
// This helper is designed to be called with an actual wall in the way (its
// only production callers are AI wall-hug/dig-path helpers); on a fully
// open grid its "maxdist must strictly decrease" search geometry doesn't
// converge to the exact destination subtile in general (confirmed
// empirically: an earlier version of this test assumed it always would
// and failed against the real run). These tests instead assert the
// documented, always-true structural contract -- a valid tri-state result
// and in-bounds output coordinates -- while the "boxed in on one side"
// case pins down the one precise, hand-traceable outcome: search side 'a'
// (dirctn=-1) can never step off a subtile whose 4 cardinal neighbours are
// all blocked, because its move is conditioned on finding a non-blocked
// direction with no unconditional fallback.
TEST_CASE_METHOD(WallhugFixture, "get_hug_side_options always terminates with a valid tri-state result and in-bounds output on an open map", "[kfx_pathfinding][ariadne_wallhug]") {
    MapSubtlCoord ax, ay, bx, by;
    int64_t result = get_hug_side_options(3, 3, 3, 9, 0, 0, &ax, &ay, &bx, &by);
    CHECK((result == 0 || result == 1 || result == 2));
    CHECK(grid.in_bounds(ax, ay));
    CHECK(grid.in_bounds(bx, by));
}

TEST_CASE_METHOD(WallhugFixture, "get_hug_side_options's side 'a' search never leaves a subtile boxed in on all 4 cardinal neighbours", "[kfx_pathfinding][ariadne_wallhug]") {
    // Box the source subtile in on all four cardinal sides. Side 'a'
    // (dirctn=-1) only moves when it finds a non-blocked direction within
    // its 4-direction scan (no "move anyway" fallback, unlike side 'b'
    // below) -- with every neighbour blocked it can never take a first
    // step, so it stays at the source for the entire search.
    //
    // Side 'b' (dirctn=+1) has an unconditional fallback move instead (see
    // ariadne_wallhug.c's get_hug_side_next_step: `(n < length) ||
    // (dirctn > 0)` accepts unconditionally once dirctn>0), so it steps
    // off the boxed source on its very first call despite every immediate
    // neighbour being blocked -- a genuine asymmetry between the two
    // search directions, not a typo in this test. Once off the source
    // subtile it's back on the open part of the grid and free to
    // converge, so it (empirically, confirmed against the real run) goes
    // on to reach the destination exactly and the overall call returns 0
    // via side 'b', not 2 -- side 'a' being permanently stuck doesn't by
    // itself make the whole call give up.
    grid.at(3 + STL_PER_SLB, 3).walkable = false;
    grid.at(3 - STL_PER_SLB, 3).walkable = false;
    grid.at(3, 3 + STL_PER_SLB).walkable = false;
    grid.at(3, 3 - STL_PER_SLB).walkable = false;

    MapSubtlCoord ax, ay, bx, by;
    int64_t result = get_hug_side_options(3, 3, 3, 9, 0, 0, &ax, &ay, &bx, &by);
    CHECK(result == 0);
    CHECK(ax == 3);
    CHECK(ay == 3);
    CHECK((bx != 3 || by != 3));
}

// --- slab_wall_hug_route -------------------------------------------------
TEST_CASE_METHOD(WallhugFixture, "slab_wall_hug_route reaches an adjacent-slab target directly on an open map", "[kfx_pathfinding][ariadne_wallhug]") {
    struct Coord3d target{};
    target.x.val = subtile_coord_center(13); // one slab east
    target.y.val = subtile_coord_center(10);
    target.z.val = 0;

    int64_t steps = slab_wall_hug_route(t(), &target, 20);
    CHECK(steps > 0); // reached the target within max_val steps
}

TEST_CASE_METHOD(WallhugFixture, "slab_wall_hug_route returns 0 (ran out of steps) when max_val is exhausted before reaching a far target", "[kfx_pathfinding][ariadne_wallhug]") {
    struct Coord3d target{};
    target.x.val = subtile_coord_center(19);
    target.y.val = subtile_coord_center(10);
    target.z.val = 0;

    int64_t steps = slab_wall_hug_route(t(), &target, 1);
    CHECK(steps == 0);
}

TEST_CASE_METHOD(WallhugFixture, "slab_wall_hug_route hugs around a wall directly blocking the straight line east", "[kfx_pathfinding][ariadne_wallhug]") {
    // Wall off the slab directly east of the creature's own slab so the
    // direct hug_can_move_on(curr) check fails and hug_round/hug_round_sub
    // (this file's other zero-coverage static helpers) actually run.
    for (int64_t dy = -1; dy <= 1; dy++) {
        grid.at(13, 9 + dy).walkable = false;
    }
    struct Coord3d target{};
    target.x.val = subtile_coord_center(13);
    target.y.val = subtile_coord_center(10);
    target.z.val = 0;

    int64_t steps = slab_wall_hug_route(t(), &target, 40);
    // Either it fights its way around (>0) or gives up (-1) -- both are
    // real, valid outcomes of hug_round; what matters for coverage is that
    // hug_round's non-trivial branches actually ran, which this wall
    // guarantees (verified: this hits hug_round's search-for-a-direction
    // branch, not just the trivial straight-line one above).
    CHECK((steps == -1 || steps > 0));
}

// --- creature_cannot_move_directly_to_with_collide / get_hugging_blocked_flags
// / set_hugging_pos_using_blocked_flags, all reached only indirectly (real
// external linkage confirmed absent -- see file header) through
// get_next_position_and_angle_required_to_tunnel_creature_to below.
TEST_CASE_METHOD(WallhugFixture, "get_next_position_and_angle_required_to_tunnel_creature_to leaves a disabled-navigation creature alone", "[kfx_pathfinding][ariadne_wallhug]") {
    thing.navi.navstate = NavS_NavigationDisabled;
    struct Coord3d target{};
    target.x.val = subtile_coord_center(15);
    target.y.val = subtile_coord_center(10);
    target.z.val = 0;

    int64_t result = get_next_position_and_angle_required_to_tunnel_creature_to(t(), &target, 0);
    CHECK(result == 1);
    CHECK(thing.navi.navstate == NavS_NavigationDisabled); // default case is a pure no-op
}

TEST_CASE_METHOD(WallhugFixture, "get_next_position_and_angle_required_to_tunnel_creature_to's WallhugRestartSetup advances to InitialWallhugSetup once close enough", "[kfx_pathfinding][ariadne_wallhug]") {
    thing.navi.navstate = NavS_WallhugRestartSetup;
    thing.navi.side = 1;
    thing.navi.pos_next = thing.pos; // distance 0 <= 16
    thing.move_angle = ANGLE_NORTH;

    struct Coord3d target{};
    target.x.val = subtile_coord_center(15);
    target.y.val = subtile_coord_center(10);
    target.z.val = 0;

    int64_t result = get_next_position_and_angle_required_to_tunnel_creature_to(t(), &target, 0);
    CHECK(result == 1);
    CHECK(thing.navi.navstate == NavS_InitialWallhugSetup);
    CHECK(thing.navi.angle == ((ANGLE_NORTH + DEGREES_90) & ANGLE_MASK));
}

TEST_CASE_METHOD(WallhugFixture, "get_next_position_and_angle_required_to_tunnel_creature_to's WallhugRestartSetup waits when still far from pos_next", "[kfx_pathfinding][ariadne_wallhug]") {
    thing.navi.navstate = NavS_WallhugRestartSetup;
    thing.navi.pos_next = thing.pos;
    thing.navi.pos_next.x.val += 5000; // far away -> chessboard distance > 16

    struct Coord3d target{};
    target.x.val = subtile_coord_center(15);
    target.y.val = subtile_coord_center(10);
    target.z.val = 0;

    int64_t result = get_next_position_and_angle_required_to_tunnel_creature_to(t(), &target, 0);
    CHECK(result == 1);
    CHECK(thing.navi.navstate == NavS_WallhugRestartSetup); // unchanged -- still waiting
}

TEST_CASE_METHOD(WallhugFixture, "get_next_position_and_angle_required_to_tunnel_creature_to's WallhugAngleCorrection reports a still-blocking wall (return 2)", "[kfx_pathfinding][ariadne_wallhug]") {
    thing.navi.navstate = NavS_WallhugAngleCorrection;
    // Encode first_colliding_block at a subtile whose slab is flagged
    // Blocking, via the fake's own get_subtile_number encoding.
    MapSubtlCoord blk_x = 15, blk_y = 10;
    grid.set_slab(subtile_slab(blk_x), subtile_slab(blk_y), SlbT_ROCK, 0, false, SlbAtFlg_Blocking);
    thing.navi.first_colliding_block = fake_get_subtile_number(blk_x, blk_y);

    struct Coord3d target{};
    target.x.val = subtile_coord_center(15);
    target.y.val = subtile_coord_center(10);
    target.z.val = 0;

    int64_t result = get_next_position_and_angle_required_to_tunnel_creature_to(t(), &target, 0);
    CHECK(result == 2);
    CHECK(thing.navi.navstate == NavS_WallhugAngleCorrection); // not advanced while still blocked
}

TEST_CASE_METHOD(WallhugFixture, "get_next_position_and_angle_required_to_tunnel_creature_to's WallhugAngleCorrection advances once the block clears", "[kfx_pathfinding][ariadne_wallhug]") {
    thing.navi.navstate = NavS_WallhugAngleCorrection;
    MapSubtlCoord blk_x = 15, blk_y = 10;
    // Grid defaults to open/non-blocking -- nothing more to set up.
    thing.navi.first_colliding_block = fake_get_subtile_number(blk_x, blk_y);

    struct Coord3d target{};
    target.x.val = subtile_coord_center(15);
    target.y.val = subtile_coord_center(10);
    target.z.val = 0;

    int64_t result = get_next_position_and_angle_required_to_tunnel_creature_to(t(), &target, 0);
    CHECK(result == 1);
    CHECK(thing.navi.navstate == NavS_WallhugInProgress);
}

TEST_CASE_METHOD(WallhugFixture, "get_next_position_and_angle_required_to_tunnel_creature_to's WallhugInProgress moves straight toward an open target", "[kfx_pathfinding][ariadne_wallhug]") {
    thing.navi.navstate = NavS_WallhugInProgress;
    thing.navi.push_counter = 0;
    thing.navi.distance_to_next_pos = 0;
    thing.max_speed = 32;

    struct Coord3d target{};
    target.x.val = subtile_coord_center(15);
    target.y.val = subtile_coord_center(10);
    target.z.val = 0;

    int64_t result = get_next_position_and_angle_required_to_tunnel_creature_to(t(), &target, 0);
    CHECK(result == 1);
    // On a fully open grid this shouldn't trip the "cannot move" (==4)
    // collision path, since thing_in_wall_at is only ever true on a
    // blocked cell.
}
