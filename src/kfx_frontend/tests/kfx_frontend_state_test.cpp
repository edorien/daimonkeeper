// kfx_frontend: kfx_frontend_state.c -- the raw-blob save/load/reset
// wrappers registered on GameCallbacks so kfx_game doesn't need to reach
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
const char *kTestFile = "kfx_frontend_utest_state_test.bin";

struct ScratchFile {
    ScratchFile() { std::remove(kTestFile); }
    ~ScratchFile() { std::remove(kTestFile); }
};
}

TEST_CASE_METHOD(ScratchFile, "save_frontend_state/load_frontend_state round-trip through a real file", "[kfx_frontend][kfx_frontend_state]") {
    reset_frontend_state();
    kfx_frontend_state.flash_button_index = 7;
    kfx_frontend_state.flash_button_time = 3.5f;
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
    CHECK(kfx_frontend_state.flash_button_time == 3.5f);
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

// The blob holds pointers into the *writing* process (cheat-menu boxes in the static gui_boxes[] array).
// Importing one written by another run / a multiplayer host must not adopt them: the next
// gui_box_is_not_valid() would dereference a foreign address.
TEST_CASE_METHOD(ScratchFile, "load_frontend_state keeps this process's GUI-box pointers", "[kfx_frontend][kfx_frontend_state][lp64]") {
    reset_frontend_state();
    kfx_frontend_state.flash_button_index = 9;
    kfx_frontend_state.gui_cheat_box_1 = reinterpret_cast<struct GuiBox *>(0x1111);
    kfx_frontend_state.gui_cheat_box_3 = reinterpret_cast<struct GuiBox *>(0x3333);
    kfx_frontend_state.gui_cheat_box_4 = reinterpret_cast<struct GuiBox *>(0x4444);
    kfx_frontend_state.level_names_data = reinterpret_cast<char *>(0x5555);
    kfx_frontend_state.end_level_names_data = reinterpret_cast<char *>(0x6666);

    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    REQUIRE(h != nullptr);
    REQUIRE(save_frontend_state(h));
    LbFileClose(h);

    // "Another run": different live pointers, different data.
    reset_frontend_state();
    kfx_frontend_state.gui_cheat_box_3 = reinterpret_cast<struct GuiBox *>(0xAAA3);
    kfx_frontend_state.level_names_data = reinterpret_cast<char *>(0xAAA5);

    TbFileHandle rh = LbFileOpen(kTestFile, Lb_FILE_MODE_READ_ONLY);
    REQUIRE(rh != nullptr);
    REQUIRE(load_frontend_state(rh));
    LbFileClose(rh);

    CHECK(kfx_frontend_state.flash_button_index == 9); // ordinary data still comes from the file
    CHECK(kfx_frontend_state.gui_cheat_box_1 == nullptr);
    CHECK(kfx_frontend_state.gui_cheat_box_3 == reinterpret_cast<struct GuiBox *>(0xAAA3));
    CHECK(kfx_frontend_state.gui_cheat_box_4 == nullptr);
    CHECK(kfx_frontend_state.level_names_data == reinterpret_cast<char *>(0xAAA5));
    CHECK(kfx_frontend_state.end_level_names_data == nullptr);
    reset_frontend_state();
}

TEST_CASE("resync_import_frontend_state keeps this process's GUI-box pointers", "[kfx_frontend][kfx_frontend_state][lp64]") {
    reset_frontend_state();
    kfx_frontend_state.last_mouse_x = 321;
    kfx_frontend_state.gui_cheat_box_1 = reinterpret_cast<struct GuiBox *>(0x1111); // the host's address
    size_t len = 0;
    const char *host_blob = resync_export_frontend_state(&len);
    REQUIRE(len == sizeof(struct KfxFrontendState));
    std::vector<char> copy(host_blob, host_blob + len);

    reset_frontend_state();
    kfx_frontend_state.gui_cheat_box_1 = reinterpret_cast<struct GuiBox *>(0xBBB1); // ours
    REQUIRE(resync_import_frontend_state(copy.data(), copy.size()));
    CHECK(kfx_frontend_state.last_mouse_x == 321);
    CHECK(kfx_frontend_state.gui_cheat_box_1 == reinterpret_cast<struct GuiBox *>(0xBBB1));
    reset_frontend_state();
}
