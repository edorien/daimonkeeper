// kfx_frontend: kfx_frontend_state.c -- the raw-blob save/load/reset
// wrappers tabled in UiPort so kfx_game doesn't need to reach
// up into this header directly. All fully testable: reset_frontend_state/
// get_frontend_state_size are pure, and save_frontend_state/
// load_frontend_state are thin LbFileWrite/LbFileRead wrappers -- reuses
// kfx_platform's bflib_fileio_test.cpp real-scratch-file discipline (a
// bare relative filename, not an absolute /tmp/... path -- see that
// file's own header comment on LbFileOpen's leading-slash bug).
#include <catch2/catch_test_macros.hpp>

#include "kfx_frontend_state.h"
#include "bflib_fileio.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <unistd.h>

TEST_CASE("get_frontend_state_size reports the real struct size", "[kfx_frontend][kfx_frontend_state]") {
    CHECK(get_frontend_state_size() == sizeof(struct KfxFrontendState));
    CHECK(get_frontend_state_size() > 0);
}

TEST_CASE("reset_frontend_state zeroes the whole global struct", "[kfx_frontend][kfx_frontend_state]") {
    kfx_frontend_state.flash_button_index = 42;
    kfx_frontend_state.last_mouse_x = 100;
    kfx_frontend_state.last_mouse_y = 200;

    reset_frontend_state();

    CHECK(kfx_frontend_state.flash_button_index == 0);
    CHECK(kfx_frontend_state.last_mouse_x == 0);
    CHECK(kfx_frontend_state.last_mouse_y == 0);
}

namespace {
// One file per process: ctest runs each test case as its own process, in
// parallel, so a shared name lets one case delete another's file.
const std::string kTestFileStr = "kfx_frontend_utest_state_test_" + std::to_string(getpid()) + ".bin";
const char *const kTestFile = kTestFileStr.c_str();

struct ScratchFile {
    ScratchFile() { std::remove(kTestFile); }
    ~ScratchFile() { std::remove(kTestFile); }
};
}

TEST_CASE_METHOD(ScratchFile, "save_frontend_state/load_frontend_state round-trip through a real file", "[kfx_frontend][kfx_frontend_state]") {
    reset_frontend_state();
    kfx_frontend_state.flash_button_index = 7;
    kfx_frontend_state.flash_button_time = 3.5;
    kfx_frontend_state.last_mouse_x = 111;
    kfx_frontend_state.last_mouse_y = 222;

    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    REQUIRE(h != nullptr);
    CHECK(save_frontend_state(h));
    LbFileClose(h);

    reset_frontend_state(); // clear in-memory state so the load below proves the round trip
    CHECK(kfx_frontend_state.flash_button_index == 0);

    TbFileHandle rh = LbFileOpen(kTestFile, Lb_FILE_MODE_READ_ONLY);
    REQUIRE(rh != nullptr);
    CHECK(load_frontend_state(rh));
    LbFileClose(rh);

    CHECK(kfx_frontend_state.flash_button_index == 7);
    CHECK(kfx_frontend_state.flash_button_time == 3.5);
    CHECK(kfx_frontend_state.last_mouse_x == 111);
    CHECK(kfx_frontend_state.last_mouse_y == 222);
}

TEST_CASE_METHOD(ScratchFile, "load_frontend_state fails when the file is shorter than the struct", "[kfx_frontend][kfx_frontend_state]") {
    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    REQUIRE(h != nullptr);
    char one_byte = 0;
    LbFileWrite(h, &one_byte, 1); // far short of sizeof(struct KfxFrontendState)
    LbFileClose(h);

    TbFileHandle rh = LbFileOpen(kTestFile, Lb_FILE_MODE_READ_ONLY);
    REQUIRE(rh != nullptr);
    CHECK_FALSE(load_frontend_state(rh));
    LbFileClose(rh);
}

// Process-local pointers (cheat-menu boxes) live in kfx_frontend_local, outside the raw blob that is saved,
// loaded and network-resynced, so importing a blob written by another process cannot install foreign addresses.
TEST_CASE_METHOD(ScratchFile, "load_frontend_state leaves this process's GUI-box pointers alone", "[kfx_frontend][kfx_frontend_state]") {
    reset_frontend_state();
    kfx_frontend_state.flash_button_index = 9;
    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    REQUIRE(h != nullptr);
    REQUIRE(save_frontend_state(h));
    LbFileClose(h);

    reset_frontend_state();
    kfx_frontend_local.gui_cheat_box_3 = reinterpret_cast<struct GuiBox *>(0xAAA3);
    TbFileHandle rh = LbFileOpen(kTestFile, Lb_FILE_MODE_READ_ONLY);
    REQUIRE(rh != nullptr);
    REQUIRE(load_frontend_state(rh));
    LbFileClose(rh);

    CHECK(kfx_frontend_state.flash_button_index == 9);
    CHECK(kfx_frontend_local.gui_cheat_box_3 == reinterpret_cast<struct GuiBox *>(0xAAA3));
    reset_frontend_state();
}

TEST_CASE("resync_import_frontend_state leaves this process's GUI-box pointers alone", "[kfx_frontend][kfx_frontend_state]") {
    reset_frontend_state();
    kfx_frontend_state.last_mouse_x = 321;
    size_t len = 0;
    const char *host_blob = resync_export_frontend_state(&len);
    REQUIRE(len == sizeof(struct KfxFrontendState));
    std::vector<char> copy(host_blob, host_blob + len);

    reset_frontend_state();
    kfx_frontend_local.gui_cheat_box_1 = reinterpret_cast<struct GuiBox *>(0xBBB1);
    REQUIRE(resync_import_frontend_state(copy.data(), copy.size()));
    CHECK(kfx_frontend_state.last_mouse_x == 321);
    CHECK(kfx_frontend_local.gui_cheat_box_1 == reinterpret_cast<struct GuiBox *>(0xBBB1));
    reset_frontend_state();
}
