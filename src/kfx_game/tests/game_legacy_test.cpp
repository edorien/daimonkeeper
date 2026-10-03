// First kfx_game coverage, per docs/refactor/testing/
// stage-02-testability-and-fakes.md's rollout order: get_gameturn()'s 329
// fan-in (architecture.md §11.2) makes it a high-value, low-risk target --
// a pure state read, widely relied on. Since refactor pass 2 (S10) the
// turn is kfx_sim_state.play_gameturn and get_gameturn() reads it through
// a source pointer main.cpp installs; the test installs the same one.
#include <catch2/catch_test_macros.hpp>

#include "game_legacy.h"
#include "kfx_game_state.h"

#include <cstring>
#include <vector>

namespace {
struct ResetGameState {
    ResetGameState() { std::memset(&kfx_game_state, 0, sizeof(kfx_game_state)); }
};
}

TEST_CASE_METHOD(ResetGameState, "get_gameturn reads kfx_sim_state.play_gameturn once main.cpp's wiring is in place", "[kfx_game][game_legacy]") {
    set_gameturn_source(&kfx_sim_state.play_gameturn); // what wire_ports() does
    kfx_sim_state.play_gameturn = 0;
    CHECK(get_gameturn() == 0);
    kfx_sim_state.play_gameturn = 42;
    CHECK(get_gameturn() == 42);
    kfx_sim_state.play_gameturn++;
    CHECK(get_gameturn() == 43);
    set_gameturn_source(nullptr);
}

// kfx_game_state is exchanged as a raw blob (resync, savegames); the process-local cheat-box pointer lives in
// kfx_game_local, outside it, so an import cannot install the sender's address.
TEST_CASE_METHOD(ResetGameState, "resync_import_game_state leaves this process's cheat-box pointer alone", "[kfx_game][game_legacy]") {
    kfx_sim_state.play_gameturn = 77;
    size_t len = 0;
    const char *exported = resync_export_game_state(&len);
    std::vector<char> blob(exported, exported + len);

    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    kfx_game_local.gui_cheat_box_2 = reinterpret_cast<struct GuiBox *>(0xCCC2);
    REQUIRE(resync_import_game_state(blob.data(), blob.size()));
    CHECK(kfx_sim_state.play_gameturn == 77);
    CHECK(kfx_game_local.gui_cheat_box_2 == reinterpret_cast<struct GuiBox *>(0xCCC2));
}
