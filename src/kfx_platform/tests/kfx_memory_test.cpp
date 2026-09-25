// kfx_platform: kfx_memory.c, KeeperFX's centralised allocator. The build
// never defines KFX_DEBUG_MEMORY (no CMake module sets it), so only the
// release-build thin-wrapper path is compiled/testable here -- the
// KFX_DEBUG_MEMORY guard-zone/per-callsite-accounting path is dead code
// in every configuration this repo currently builds. All pattern A: real
// malloc/free underneath, no fakes needed.
//
// KfxMemInit()/KfxScratch()/KfxScratchReset()/KfxScratchUsed() share a
// single static bump-arena (s_scratch_base/s_scratch_cap/s_scratch_used)
// across the whole process -- the fixture below re-inits it for every
// test case so tests don't see each other's scratch usage.
#include <catch2/catch_test_macros.hpp>

#include "kfx_memory.h"

#include <cstring>

namespace {
struct ScratchFixture {
    ScratchFixture() {
        KfxMemShutdown(); // safe even if never initialized (frees a null pointer)
        KfxMemInit();
    }
    ~ScratchFixture() { KfxMemShutdown(); }
};
}

TEST_CASE("KfxAlloc returns a usable, writable block of the requested size", "[kfx_platform][kfx_memory]") {
    void* p = KfxAlloc(64);
    REQUIRE(p != nullptr);
    std::memset(p, 0xAB, 64);
    CHECK(static_cast<unsigned char*>(p)[0] == 0xAB);
    CHECK(static_cast<unsigned char*>(p)[63] == 0xAB);
    KfxFree(p);
}

TEST_CASE("KfxCalloc zero-initializes the requested block", "[kfx_platform][kfx_memory]") {
    unsigned char* p = static_cast<unsigned char*>(KfxCalloc(16, sizeof(unsigned char)));
    REQUIRE(p != nullptr);
    for (int64_t i = 0; i < 16; i++) {
        CHECK(p[i] == 0);
    }
    KfxFree(p);
}

TEST_CASE("KfxRealloc grows a block while preserving its original contents", "[kfx_platform][kfx_memory]") {
    char* p = static_cast<char*>(KfxAlloc(4));
    std::memcpy(p, "abcd", 4);
    p = static_cast<char*>(KfxRealloc(p, 8));
    REQUIRE(p != nullptr);
    CHECK(std::memcmp(p, "abcd", 4) == 0);
    KfxFree(p);
}

TEST_CASE("KfxRealloc with size 0 frees the pointer and returns NULL", "[kfx_platform][kfx_memory]") {
    void* p = KfxAlloc(8);
    void* result = KfxRealloc(p, 0);
    CHECK(result == nullptr);
}

TEST_CASE("KfxRealloc on a NULL pointer behaves like KfxAlloc", "[kfx_platform][kfx_memory]") {
    void* p = KfxRealloc(nullptr, 32);
    REQUIRE(p != nullptr);
    KfxFree(p);
}

TEST_CASE("KfxFree on a NULL pointer is a no-op", "[kfx_platform][kfx_memory]") {
    KfxFree(nullptr); // must not crash
    SUCCEED();
}

TEST_CASE("KfxStrDup duplicates the string's contents into a new, independent buffer", "[kfx_platform][kfx_memory]") {
    const char* original = "hello world";
    char* dup = KfxStrDup(original);
    REQUIRE(dup != nullptr);
    CHECK(dup != original);
    CHECK(std::strcmp(dup, original) == 0);
    KfxFree(dup);
}

TEST_CASE("KfxStrDup returns NULL for a NULL input", "[kfx_platform][kfx_memory]") {
    CHECK(KfxStrDup(nullptr) == nullptr);
}

TEST_CASE("KfxMemDump/KfxMemValidate are no-ops in the release build (never crash)", "[kfx_platform][kfx_memory]") {
    KfxMemDump();
    KfxMemValidate();
    SUCCEED();
}

TEST_CASE_METHOD(ScratchFixture, "KfxScratch hands out sequential, non-overlapping regions and KfxScratchUsed tracks the total", "[kfx_platform][kfx_memory]") {
    CHECK(KfxScratchUsed() == 0);
    void* a = KfxScratch(16);
    REQUIRE(a != nullptr);
    CHECK(KfxScratchUsed() == 16);
    void* b = KfxScratch(16);
    REQUIRE(b != nullptr);
    CHECK(KfxScratchUsed() == 32);
    CHECK(a != b);
    // Both regions are writable and don't alias each other.
    std::memset(a, 0x11, 16);
    std::memset(b, 0x22, 16);
    CHECK(static_cast<unsigned char*>(a)[15] == 0x11);
    CHECK(static_cast<unsigned char*>(b)[15] == 0x22);
}

TEST_CASE_METHOD(ScratchFixture, "KfxScratch aligns each allocation's size up to 8 bytes", "[kfx_platform][kfx_memory]") {
    KfxScratch(1); // rounds up to 8
    CHECK(KfxScratchUsed() == 8);
    KfxScratch(9); // rounds up to 16
    CHECK(KfxScratchUsed() == 24);
}

TEST_CASE_METHOD(ScratchFixture, "KfxScratchReset rewinds the arena to empty without freeing it", "[kfx_platform][kfx_memory]") {
    KfxScratch(64);
    CHECK(KfxScratchUsed() == 64);
    KfxScratchReset();
    CHECK(KfxScratchUsed() == 0);
    // The arena is still usable after a reset.
    void* p = KfxScratch(8);
    CHECK(p != nullptr);
    CHECK(KfxScratchUsed() == 8);
}
