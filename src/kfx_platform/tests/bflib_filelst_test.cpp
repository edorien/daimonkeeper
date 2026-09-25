// kfx_platform: bflib_filelst.c, the TbLoadFiles[]-array data loader.
// LbDataFree()'s two "don't touch anything" sentinel FName prefixes
// ('!' and the literal "*TEXTURE_PAGE") and LbDataLoad()'s '*'-prefixed
// wildcard branch (SLength-only calloc, no real file touched) are both
// fully pattern-A testable. LbDataLoad()'s real-file branch is only
// exercised for the "file doesn't exist" early return (-101) here,
// reusing bflib_fileio_test.cpp's real-scratch-file discipline for the
// nonexistent-file case specifically (no file created); a full real-file
// load round-trip through LbDataLoad would need a LoadFilesGetSizeFunc/
// LoadFilesUnpackFunc pair too and isn't attempted.
#include <catch2/catch_test_macros.hpp>

#include "bflib_filelst.h"

#include <cstdlib>
#include <cstring>

namespace {
struct ModifyFilenameFixture {
    ModifyFilenameFixture() { LbDataLoadSetModifyFilenameFunction(defaultModifyDataLoadFilename); }
    ~ModifyFilenameFixture() { LbDataLoadSetModifyFilenameFunction(defaultModifyDataLoadFilename); }
};
}

TEST_CASE("LbDataFree on a NULL load_file returns 0", "[kfx_platform][bflib_filelst]") {
    CHECK(LbDataFree(nullptr) == 0);
}

TEST_CASE("LbDataFree frees Start and nulls both Start and SEnd for a normal entry", "[kfx_platform][bflib_filelst]") {
    struct TbLoadFiles lf{};
    std::strcpy(lf.FName, "somefile.dat");
    unsigned char *block = static_cast<unsigned char*>(std::malloc(16));
    unsigned char *start_ptr = block;
    unsigned char *end_ptr = block + 16;
    lf.Start = &start_ptr;
    lf.SEnd = &end_ptr;
    CHECK(LbDataFree(&lf) == 1);
    CHECK(start_ptr == nullptr);
    CHECK(end_ptr == nullptr);
}

TEST_CASE("LbDataFree leaves Start untouched for a '!'-prefixed (already-allocated) entry", "[kfx_platform][bflib_filelst]") {
    struct TbLoadFiles lf{};
    std::strcpy(lf.FName, "!static_buffer");
    unsigned char sentinel[4];
    unsigned char *start_ptr = sentinel;
    lf.Start = &start_ptr;
    lf.SEnd = nullptr;
    CHECK(LbDataFree(&lf) == 1);
    CHECK(start_ptr == sentinel); // untouched, not freed
}

TEST_CASE("LbDataFree leaves Start untouched for the literal *TEXTURE_PAGE marker", "[kfx_platform][bflib_filelst]") {
    struct TbLoadFiles lf{};
    std::strcpy(lf.FName, "*TEXTURE_PAGE");
    unsigned char sentinel[4];
    unsigned char *start_ptr = sentinel;
    lf.Start = &start_ptr;
    CHECK(LbDataFree(&lf) == 1);
    CHECK(start_ptr == sentinel);
}

TEST_CASE("LbDataFreeAll frees every entry up to the Start==NULL sentinel", "[kfx_platform][bflib_filelst]") {
    unsigned char *a_ptr = static_cast<unsigned char*>(std::malloc(4));
    unsigned char *b_ptr = static_cast<unsigned char*>(std::malloc(4));

    struct TbLoadFiles files[3] = {};
    std::strcpy(files[0].FName, "a.dat");
    files[0].Start = &a_ptr;
    std::strcpy(files[1].FName, "b.dat");
    files[1].Start = &b_ptr;
    files[2].Start = nullptr; // sentinel: LbDataFreeAll stops here

    LbDataFreeAll(files);
    CHECK(a_ptr == nullptr);
    CHECK(b_ptr == nullptr);
}

TEST_CASE_METHOD(ModifyFilenameFixture, "LbDataLoad on a '*'-prefixed entry calloc's SLength bytes without touching the filesystem", "[kfx_platform][bflib_filelst]") {
    struct TbLoadFiles lf{};
    std::strcpy(lf.FName, "*");
    lf.SLength = 32;
    unsigned char *start_ptr = nullptr;
    unsigned char *end_ptr = nullptr;
    lf.Start = &start_ptr;
    lf.SEnd = &end_ptr;

    CHECK(LbDataLoad(&lf, nullptr, nullptr) == 1);
    REQUIRE(start_ptr != nullptr);
    CHECK(end_ptr == start_ptr + 32);
    for (int64_t i = 0; i < 32; i++) {
        CHECK(start_ptr[i] == 0); // calloc, not malloc -- zero-initialized
    }
    std::free(start_ptr);
}

TEST_CASE_METHOD(ModifyFilenameFixture, "LbDataLoad returns -101 for a real-file entry whose file doesn't exist", "[kfx_platform][bflib_filelst]") {
    struct TbLoadFiles lf{};
    std::strcpy(lf.FName, "kfx_platform_utest_filelst_definitely_missing.dat");
    unsigned char *start_ptr = nullptr;
    lf.Start = &start_ptr;
    CHECK(LbDataLoad(&lf, nullptr, nullptr) == -101);
}

TEST_CASE_METHOD(ModifyFilenameFixture, "LbDataLoadSetModifyFilenameFunction installs the hook and echoes the same function pointer back", "[kfx_platform][bflib_filelst]") {
    // Found by testing, not assumed from the name: despite reading like a
    // "set and return the old one" setter, it just echoes newfunc straight
    // back -- there's no previous-value tracking at all.
    static const char *rewritten = "rewritten.dat";
    ModifyDataLoadFnameFunc *fake = [](const char *) -> const char * { return rewritten; };
    ModifyDataLoadFnameFunc *result = LbDataLoadSetModifyFilenameFunction(fake);
    CHECK(result("anything.dat") == rewritten);
    CHECK(modify_data_load_filename_function("anything.dat") == rewritten);
}

TEST_CASE("defaultModifyDataLoadFilename passes its input straight through", "[kfx_platform][bflib_filelst]") {
    const char *input = "foo.dat";
    CHECK(defaultModifyDataLoadFilename(input) == input); // same pointer, not just equal content
}
