// kfx_platform: port_check.h's completeness check, and every kfx_platform
// default callback table passing it.
// See docs/refactor-pass2/stage-01-callback-hygiene.md.
#include <catch2/catch_test_macros.hpp>

#include "port_check.h"
#include "ports/file_path_port.h"
#include "ports/sound_host_port.h"
#include "ports/input_focus_port.h"
#include "ports/display_host_port.h"

namespace {
struct FakeTable {
    void (*a)(void);
    int64_t (*b)(int64_t);
    void (*c)(void);
};
void fake_void(void) {}
int64_t fake_long(int64_t v) { return v; }
}

TEST_CASE("kfx_table_complete accepts a table with every slot set", "[kfx_platform][ports]") {
    const FakeTable table = { .a = fake_void, .b = fake_long, .c = fake_void };
    CHECK(KFX_TABLE_SLOTS(FakeTable) == 3);
    CHECK(KFX_TABLE_COMPLETE(&table, FakeTable));
}

TEST_CASE("kfx_table_complete rejects a table with a slot left out", "[kfx_platform][ports]") {
    const FakeTable table = { .a = fake_void, .b = nullptr, .c = fake_void };
    CHECK_FALSE(KFX_TABLE_COMPLETE(&table, FakeTable));
}

TEST_CASE("kfx_table_complete rejects a missing table", "[kfx_platform][ports]") {
    CHECK_FALSE(kfx_table_complete(nullptr, 3, "FakeTable"));
}

TEST_CASE("every kfx_platform port's default table has all its slots set", "[kfx_platform][ports]") {
    // set_*(nullptr) reinstalls the default table.
    set_file_path_port(nullptr);
    CHECK(KFX_TABLE_COMPLETE(file_path_port, struct FilePathPort));
    set_sound_host_port(nullptr);
    CHECK(KFX_TABLE_COMPLETE(sound_host_port, struct SoundHostPort));
    set_input_focus_port(nullptr);
    CHECK(KFX_TABLE_COMPLETE(input_focus_port, struct InputFocusPort));
    set_display_host_port(nullptr);
    CHECK(KFX_TABLE_COMPLETE(display_host_port, struct DisplayHostPort));
}
