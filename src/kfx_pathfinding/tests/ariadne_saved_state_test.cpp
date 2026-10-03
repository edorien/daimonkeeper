// kfx_pathfinding: ariadne_saved_state.c -- Ariadne's navigation mesh, saved and resynced with the game (refactor
// pass 4, P4-F7). The mesh is updated incrementally as the map changes; one built again from the map is a
// different mesh, so a load or a resync restores the saved one. The fixture triangulates
// pathfinding_fake_world.h's grid as ariadne_update_test.cpp's does.
#include <catch2/catch_test_macros.hpp>

#include "ariadne.h" // nav_map_initialised, LastTriangulatedMap
#include "ariadne_saved_state.h"
#include "ariadne_tringls.h"
#include "ariadne_update.h"
#include "kfx_pathfinding_state.h"
#include "pathfinding_fake_world.h"
#include "state_versions.h"

#include <cstring>
#include <vector>

using namespace pf_fake;

namespace {
struct MeshFixture : GridWorldFixture {
    static constexpr int64_t kLogicalSize = 8;
    MeshFixture() {
        grid.size_x = kLogicalSize;
        grid.size_y = kLogicalSize;
        for (int64_t y = 0; y < kLogicalSize; y++) {
            for (int64_t x = 0; x < kLogicalSize; x++) {
                grid.at(x, y).floor_filled_subtiles = 1;
                grid.at(x, y).unsafe = false;
                grid.at(x, y).map_flags = 0;
                grid.at(x, y).walkable = true;
            }
        }
        ariadne_set_navigation_map_size(kLogicalSize + 1, kLogicalSize + 1);
        ariadne_set_map_dimensions(kLogicalSize, kLogicalSize, g_map_size_z);
    }
    void raise_floor(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t height) {
        for (int64_t y = y0; y <= y1; y++)
            for (int64_t x = x0; x <= x1; x++)
                grid.at(x, y).floor_filled_subtiles = height;
    }
};

std::vector<unsigned char> saved_state() {
    std::vector<unsigned char> buf(ariadne_saved_state_size());
    ariadne_saved_state_write(buf.data());
    return buf;
}
} // namespace

TEST_CASE("the saved navigation mesh is the size state_versions.h records", "[kfx_pathfinding][ariadne_saved_state]") {
    // A part added to or removed from the block changes its size: bump KFX_ARIADNE_STATE_VER and the size.
    CHECK(ariadne_saved_state_size() == KFX_ARIADNE_STATE_SIZE);
}

TEST_CASE_METHOD(MeshFixture, "a restored mesh is the saved one, ready to use without building it again", "[kfx_pathfinding][ariadne_saved_state]") {
    REQUIRE(init_navigation() == 1);
    raise_floor(3, 3, 5, 5, 4);
    REQUIRE(update_navigation_triangulation(3, 3, 5, 5));
    const std::vector<unsigned char> saved = saved_state();

    // the game carries on: more of the map changes, then the saved game is loaded
    raise_floor(1, 1, 2, 6, 3);
    REQUIRE(update_navigation_triangulation(1, 1, 2, 6));
    REQUIRE(saved_state() != saved);
    nav_map_initialised = 0;
    LastTriangulatedMap = nullptr;

    ariadne_saved_state_read(saved.data());
    CHECK(saved_state() == saved);
    CHECK(nav_map_initialised);
    CHECK(LastTriangulatedMap == kfx_pathfinding_state.navigation_map);
}

TEST_CASE_METHOD(MeshFixture, "why it is saved: a mesh built from the map is not the one updates made", "[kfx_pathfinding][ariadne_saved_state]") {
    REQUIRE(init_navigation() == 1);
    raise_floor(3, 3, 5, 5, 4);
    REQUIRE(update_navigation_triangulation(3, 3, 5, 5));
    raise_floor(2, 5, 6, 6, 2);
    REQUIRE(update_navigation_triangulation(2, 5, 6, 6));
    const int64_t updated_count = count_Triangles;
    std::vector<struct Triangle> updated(Triangles, Triangles + ix_Triangles);

    REQUIRE(init_navigation() == 1); // what a load did before: the same map, triangulated whole
    const bool same = (count_Triangles == updated_count) && (static_cast<size_t>(ix_Triangles) == updated.size())
        && (std::memcmp(Triangles, updated.data(), updated.size() * sizeof(struct Triangle)) == 0);
    CHECK_FALSE(same);
}
