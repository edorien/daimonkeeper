// First kfx_game coverage, per docs/refactor/testing/
// stage-02-testability-and-fakes.md's rollout order: get_gameturn()'s 329
// fan-in (architecture.md §11.2) makes it a high-value, low-risk target --
// a pure state read, widely relied on. Its real implementation now lives
// here as game_legacy_get_gameturn() (renamed by the
// check-layering-symbol-level-blind-spot.md fix; kfx_platform's
// get_gameturn() is a thin wrapper around a registered provider function,
// not tested directly here since kfx_platform_utest doesn't wire that
// provider -- see that library's own default-stub behavior).
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

TEST_CASE_METHOD(ResetGameState, "game_legacy_get_gameturn reads kfx_game_state.play_gameturn", "[kfx_game][game_legacy]") {
    kfx_game_state.play_gameturn = 0;
    CHECK(game_legacy_get_gameturn() == 0);

    kfx_game_state.play_gameturn = 42;
    CHECK(game_legacy_get_gameturn() == 42);
}

TEST_CASE_METHOD(ResetGameState, "game_legacy_get_gameturn reflects turn advancement", "[kfx_game][game_legacy]") {
    kfx_game_state.play_gameturn = 100;
    for (int i = 0; i < 5; i++) {
        kfx_game_state.play_gameturn++;
    }
    CHECK(game_legacy_get_gameturn() == 105);
}

// kfx_game_state is exchanged as a raw blob (resync, savegames) but gui_cheat_box_2 points into this
// process's static gui_boxes[]: an import must keep the local pointer, not adopt the sender's address.
TEST_CASE_METHOD(ResetGameState, "resync_import_game_state keeps this process's cheat-box pointer", "[kfx_game][game_legacy][lp64]") {
    kfx_game_state.play_gameturn = 77;
    kfx_game_state.gui_cheat_box_2 = reinterpret_cast<struct GuiBox *>(0x2222); // the host's address
    size_t len = 0;
    const char *exported = resync_export_game_state(&len);
    std::vector<char> blob(exported, exported + len);

    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    kfx_game_state.gui_cheat_box_2 = reinterpret_cast<struct GuiBox *>(0xCCC2); // ours
    REQUIRE(resync_import_game_state(blob.data(), blob.size()));
    CHECK(kfx_game_state.play_gameturn == 77);
    CHECK(kfx_game_state.gui_cheat_box_2 == reinterpret_cast<struct GuiBox *>(0xCCC2));
}
