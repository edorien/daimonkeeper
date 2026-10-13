// Security pass S06 (docs/security-pass/README.md): the navigation mesh in a saved game or a resync is checked before
// it is used (ariadne_saved_state.h). The check tries the buffer on the live tables and puts the live mesh back.
#include <catch2/catch_test_macros.hpp>

#include "ariadne_saved_state.h"
#include "ariadne_tringls.h"
#include "kfx_pathfinding_state.h"

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace {

/** The offset in the saved-state buffer of the first thing after the pathfinding state: the triangles. */
size_t triangles_at() { return sizeof(kfx_pathfinding_state); }
size_t after_triangles() { return triangles_at() + sizeof(Triangles); }

std::vector<unsigned char> current_state()
{
    std::vector<unsigned char> buf(ariadne_saved_state_size());
    ariadne_saved_state_write(buf.data());
    return buf;
}

template <typename T> void poke(std::vector<unsigned char> &buf, size_t at, T value)
{
    std::memcpy(buf.data() + at, &value, sizeof(value));
}

/** An empty mesh as the game starts one: no triangle in use and the free list empty (-1). The test process's own
 *  globals have free_Triangles at 0, which names triangle 0 -- a list that loops through itself. */
std::vector<unsigned char> empty_mesh()
{
    auto buf = current_state();
    poke<int64_t>(buf, after_triangles() + 16, -1); // free_Triangles
    return buf;
}

std::string check(const std::vector<unsigned char> &buf)
{
    char why[256] = "";
    return ariadne_saved_state_check(buf.data(), why, sizeof(why)) ? std::string() : std::string(why);
}

} // namespace

TEST_CASE("the mesh the game writes is accepted", "[kfx_pathfinding][ariadne_state_check]") {
    auto buf = empty_mesh();
    CHECK(check(buf).empty());
}

TEST_CASE("counts and free-list heads past their tables are refused", "[kfx_pathfinding][ariadne_state_check]") {
    auto buf = empty_mesh();
    poke<int64_t>(buf, after_triangles(), TRIANLGLES_COUNT + 1); // count_Triangles
    CHECK(check(buf).find("count_Triangles") != std::string::npos);
    buf = empty_mesh();
    poke<int64_t>(buf, after_triangles() + 8, -5); // ix_Triangles
    CHECK(check(buf).find("ix_Triangles") != std::string::npos);
    buf = empty_mesh();
    poke<int64_t>(buf, after_triangles() + 16, TRIANLGLES_COUNT); // free_Triangles
    CHECK(check(buf).find("free_Triangles") != std::string::npos);
}

TEST_CASE("a triangle that names a point or a neighbour that doesn't exist is refused", "[kfx_pathfinding][ariadne_state_check]") {
    auto buf = empty_mesh();
    poke<int64_t>(buf, after_triangles() + 8, 1); // ix_Triangles = 1: triangle 0 is in use
    CHECK(check(buf).empty());
    poke<int64_t>(buf, triangles_at() + offsetof(struct Triangle, points), 1 << 20);
    CHECK(check(buf).find("Triangles.points") != std::string::npos);
    poke<int64_t>(buf, triangles_at() + offsetof(struct Triangle, points), 0);
    poke<int64_t>(buf, triangles_at() + offsetof(struct Triangle, tags) + 8, TRIANLGLES_COUNT);
    CHECK(check(buf).find("Triangles.tags") != std::string::npos);
}

TEST_CASE("a free list that loops is refused", "[kfx_pathfinding][ariadne_state_check]") {
    auto buf = empty_mesh();
    poke<int64_t>(buf, after_triangles() + 8, 2);   // two triangles in use
    poke<int64_t>(buf, after_triangles() + 16, 0);  // free list starts at 0
    poke<int64_t>(buf, triangles_at() + offsetof(struct Triangle, tags), 1);                       // 0 -> 1
    poke<int64_t>(buf, triangles_at() + sizeof(struct Triangle) + offsetof(struct Triangle, tags), 0); // 1 -> 0
    CHECK(check(buf).find("never ends") != std::string::npos);
}

TEST_CASE("checking leaves the live mesh exactly as it was", "[kfx_pathfinding][ariadne_state_check]") {
    const auto before = empty_mesh();
    auto bad = before;
    poke<int64_t>(bad, after_triangles(), -1);
    CHECK_FALSE(check(bad).empty());
    const auto live = current_state();
    CHECK_FALSE(check(bad).empty());
    CHECK(current_state() == live);                  // a refusal
    CHECK(check(before).empty());
    CHECK(current_state() == live);                  // and an acceptance
}
