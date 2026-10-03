// kfx_platform: spritesheet.cpp. create_spritesheet/free_spritesheet/
// get_sprite/add_sprite/num_sprites are all pure pattern-A (a bare
// TbSpriteSheet, no file I/O). load_spritesheet() itself is a real
// integration test: it writes its own index+data files (LbFileSaveAt,
// reusing bflib_fileio_test.cpp's real-scratch-file discipline) matching
// spritesheet.cpp's private `sprite_entry` index-record layout
// (#pragma pack(1): uint32_t offset, uint8_t width, uint8_t height --
// the non-SPRITE_FORMAT_V2 shape, since that macro isn't defined
// anywhere in this build) rather than faking the format, then loads them
// back through the real function and checks the decoded sprites.
#include <catch2/catch_test_macros.hpp>

#include "bflib_sprite.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h" // LbFileSaveAt

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {
struct SheetFixture {
    TbSpriteSheet *sheet = create_spritesheet();
    ~SheetFixture() { free_spritesheet(&sheet); }
};

#pragma pack(1)
struct IndexEntry {
    uint32_t offset;
    uint8_t width;
    uint8_t height;
};
#pragma pack()

// One file per process: ctest runs each test case as its own process, in
// parallel, so a shared name lets one case delete another's file.
const std::string kIndexFileStr = "kfx_platform_utest_spritesheet_index_" + std::to_string(getpid()) + ".dat";
const char *const kIndexFile = kIndexFileStr.c_str();
const std::string kDataFileStr = "kfx_platform_utest_spritesheet_data_" + std::to_string(getpid()) + ".dat";
const char *const kDataFile = kDataFileStr.c_str();

struct SpriteFilesFixture {
    SpriteFilesFixture() {
        std::remove(kIndexFile);
        std::remove(kDataFile);
    }
    ~SpriteFilesFixture() {
        std::remove(kIndexFile);
        std::remove(kDataFile);
    }
};
}

TEST_CASE_METHOD(SheetFixture, "create_spritesheet returns a fresh, empty sheet", "[kfx_platform][spritesheet]") {
    REQUIRE(sheet != nullptr);
    CHECK(num_sprites(sheet) == 0);
}

TEST_CASE("num_sprites on a NULL sheet is 0", "[kfx_platform][spritesheet]") {
    CHECK(num_sprites(nullptr) == 0);
}

TEST_CASE("get_sprite on a NULL sheet returns NULL", "[kfx_platform][spritesheet]") {
    CHECK(get_sprite(nullptr, 0) == nullptr);
}

TEST_CASE_METHOD(SheetFixture, "get_sprite returns NULL for an out-of-range index", "[kfx_platform][spritesheet]") {
    CHECK(get_sprite(sheet, 0) == nullptr); // empty sheet: even index 0 is out of range
}

TEST_CASE_METHOD(SheetFixture, "add_sprite appends a sprite with the given dimensions and data, retrievable via get_sprite", "[kfx_platform][spritesheet]") {
    unsigned char data[3] = {0xAA, 0xBB, 0xCC};
    CHECK(add_sprite(sheet, 3, 1, sizeof(data), data));
    CHECK(num_sprites(sheet) == 1);

    const TbSprite *spr = get_sprite(sheet, 0);
    REQUIRE(spr != nullptr);
    CHECK(spr->SWidth == 3);
    CHECK(spr->SHeight == 1);
    CHECK(std::memcmp(spr->Data, data, sizeof(data)) == 0);
}

TEST_CASE_METHOD(SheetFixture, "add_sprite accumulates multiple sprites in order", "[kfx_platform][spritesheet]") {
    unsigned char a[1] = {1};
    unsigned char b[2] = {2, 3};
    add_sprite(sheet, 1, 1, sizeof(a), a);
    add_sprite(sheet, 2, 1, sizeof(b), b);
    CHECK(num_sprites(sheet) == 2);
    CHECK(get_sprite(sheet, 0)->SWidth == 1);
    CHECK(get_sprite(sheet, 1)->SWidth == 2);
}

TEST_CASE("free_spritesheet nulls the caller's pointer", "[kfx_platform][spritesheet]") {
    TbSpriteSheet *sheet = create_spritesheet();
    REQUIRE(sheet != nullptr);
    free_spritesheet(&sheet);
    CHECK(sheet == nullptr);
}

TEST_CASE("free_spritesheet on a NULL sheet-pointer-pointer is a safe no-op", "[kfx_platform][spritesheet]") {
    free_spritesheet(nullptr); // must not crash
    SUCCEED();
}

TEST_CASE_METHOD(SpriteFilesFixture, "load_spritesheet reads a real index+data file pair and decodes each sprite's bounds and bytes", "[kfx_platform][spritesheet]") {
    IndexEntry entries[2] = {
        {0, 3, 1}, // sprite 0: offset 0, 3x1, "AAA"
        {3, 2, 1}, // sprite 1: offset 3, 2x1, "BB"
    };
    LbFileSaveAt(kIndexFile, entries, sizeof(entries));
    LbFileSaveAt(kDataFile, "AAABB", 5);

    TbSpriteSheet *sheet = load_spritesheet(kDataFile, kIndexFile);
    REQUIRE(sheet != nullptr);
    CHECK(num_sprites(sheet) == 2);

    const TbSprite *spr0 = get_sprite(sheet, 0);
    REQUIRE(spr0 != nullptr);
    CHECK(spr0->SWidth == 3);
    CHECK(spr0->SHeight == 1);
    CHECK(std::memcmp(spr0->Data, "AAA", 3) == 0);

    const TbSprite *spr1 = get_sprite(sheet, 1);
    REQUIRE(spr1 != nullptr);
    CHECK(spr1->SWidth == 2);
    CHECK(spr1->SHeight == 1);
    CHECK(std::memcmp(spr1->Data, "BB", 2) == 0);

    free_spritesheet(&sheet);
}

TEST_CASE_METHOD(SpriteFilesFixture, "load_spritesheet returns NULL when the index file doesn't exist", "[kfx_platform][spritesheet]") {
    LbFileSaveAt(kDataFile, "AAABB", 5);
    CHECK(load_spritesheet(kDataFile, kIndexFile) == nullptr);
}

TEST_CASE_METHOD(SpriteFilesFixture, "load_spritesheet returns NULL when the data file doesn't exist", "[kfx_platform][spritesheet]") {
    IndexEntry entries[1] = {{0, 3, 1}};
    LbFileSaveAt(kIndexFile, entries, sizeof(entries));
    CHECK(load_spritesheet(kDataFile, kIndexFile) == nullptr);
}
