// kfx_platform: bflib_fileio.c, the thin fopen/fread/fwrite/fseek wrapper
// every other library's file I/O goes through. Unlike the fixture-file
// pattern kfx_config's value_util_test.cpp uses (a checked-in, read-only
// TOML file plus a configure_file()-baked absolute path), these tests
// need a *writable* scratch file -- so they create/delete their own
// temp file directly in the test binary's CWD rather than adding a new
// CMake fixture mechanism, cleaning up in the fixture's constructor (in
// case a prior crashed run left it behind) and destructor alike.
// KFX_BUILD_TESTS is native-Linux-only (docs/Architecture/testing-
// harness.md §2), so a bare POSIX filename is fine here.
//
// A bare filename with no directory component for most of the tests below
// (KFX_BUILD_TESTS is native-Linux-only, docs/Architecture/testing-
// harness.md §2, so a bare POSIX filename is fine).
//
// docs/refactor/editor/phase3/00-slice1-native-save.md hit, and fixed,
// the absolute-path bug this comment used to just document: an fname
// starting with '/' put the *leading* slash first in create_directory_
// for_file()'s own strchr() scan, so it called mkdir("") for that empty
// first component -- always ENOENT, aborting the whole open before
// fopen() was ever reached. Every production LbFileOpen(...,
// Lb_FILE_MODE_NEW) caller in this codebase happens to use relative
// paths (prepare_file_fmtpath()'s own campaign-relative resolution), so
// it stayed latent until a map-content writer needed to target an
// arbitrary absolute scratch/test directory. See the "absolute path"
// test below for the regression coverage this fix earned.
//
// LbFileMakeFullPath() and the case-insensitive-fallback path inside
// LbFileOpen/LbFileLength/LbFileDelete (find_case_insensitive_file(),
// Linux/non-Windows only) still aren't attempted here: the latter would
// need two same-named-but-cased files on disk to actually exercise the
// fallback branch, more fixture setup than the straight-line open/read/
// write/seek/eof/length/delete lifecycle below.
#include <catch2/catch_test_macros.hpp>

#include "bflib_fileio.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <cstring>
#include <unistd.h>

namespace {
// One file per process: ctest runs each test case as its own process, in
// parallel, so a shared name lets one case delete another's file.
const std::string kTestFileStr = "kfx_platform_utest_fileio_test_" + std::to_string(getpid()) + ".bin";
const char *const kTestFile = kTestFileStr.c_str();

struct ScratchFile {
    ScratchFile() { std::remove(kTestFile); }
    ~ScratchFile() { std::remove(kTestFile); }
};
}

TEST_CASE_METHOD(ScratchFile, "LbFileExists is false for a file that hasn't been created", "[kfx_platform][bflib_fileio]") {
    CHECK(LbFileExists(kTestFile) == 0);
}

TEST_CASE_METHOD(ScratchFile, "LbFileOpen in READ_ONLY mode fails when the file doesn't exist", "[kfx_platform][bflib_fileio]") {
    CHECK(LbFileOpen(kTestFile, Lb_FILE_MODE_READ_ONLY) == nullptr);
}

TEST_CASE_METHOD(ScratchFile, "LbFileOpen in NEW mode creates the file, and a full write/read/seek/close round-trip works", "[kfx_platform][bflib_fileio]") {
    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    REQUIRE(h != nullptr);
    const char *payload = "hello, kfx";
    int64_t written = LbFileWrite(h, payload, std::strlen(payload));
    CHECK(written == (int64_t)std::strlen(payload));
    CHECK(LbFileClose(h) == 1);

    CHECK(LbFileExists(kTestFile) != 0);
    CHECK(LbFileLength(kTestFile) == (int64_t)std::strlen(payload));

    TbFileHandle rh = LbFileOpen(kTestFile, Lb_FILE_MODE_READ_ONLY);
    REQUIRE(rh != nullptr);
    char buffer[32] = {0};
    int64_t read_bytes = LbFileRead(rh, buffer, sizeof(buffer) - 1);
    CHECK(read_bytes == (int64_t)std::strlen(payload));
    CHECK(std::strcmp(buffer, payload) == 0);
    CHECK(LbFileClose(rh) == 1);
}

TEST_CASE_METHOD(ScratchFile, "LbFileSeek/LbFilePosition/LbFileEof track the file position through a read", "[kfx_platform][bflib_fileio]") {
    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    REQUIRE(h != nullptr);
    LbFileWrite(h, "0123456789", 10);
    LbFileClose(h);

    TbFileHandle rh = LbFileOpen(kTestFile, Lb_FILE_MODE_READ_ONLY);
    REQUIRE(rh != nullptr);
    CHECK(LbFilePosition(rh) == 0);
    CHECK_FALSE(LbFileEof(rh));

    CHECK(LbFileSeek(rh, 5, Lb_FILE_SEEK_BEGINNING) == 0);
    CHECK(LbFilePosition(rh) == 5);

    char buffer[8] = {0};
    LbFileRead(rh, buffer, 5); // reads "56789", lands exactly at EOF
    CHECK(LbFileEof(rh));

    LbFileClose(rh);
}

TEST_CASE_METHOD(ScratchFile, "LbFileLengthHandle reports the file's total size without disturbing the current position", "[kfx_platform][bflib_fileio]") {
    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    REQUIRE(h != nullptr);
    LbFileWrite(h, "0123456789", 10);
    LbFileSeek(h, 3, Lb_FILE_SEEK_BEGINNING);
    CHECK(LbFileLengthHandle(h) == 10);
    CHECK(LbFilePosition(h) == 3); // restored after probing the end
    LbFileClose(h);
}

TEST_CASE_METHOD(ScratchFile, "LbFileFlush succeeds on a valid handle", "[kfx_platform][bflib_fileio]") {
    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    REQUIRE(h != nullptr);
    LbFileWrite(h, "x", 1);
    CHECK(LbFileFlush(h) != 0);
    LbFileClose(h);
}

TEST_CASE_METHOD(ScratchFile, "LbFileOpen in OLD mode opens an existing file for read+write", "[kfx_platform][bflib_fileio]") {
    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    LbFileWrite(h, "abc", 3);
    LbFileClose(h);

    TbFileHandle oh = LbFileOpen(kTestFile, Lb_FILE_MODE_OLD);
    REQUIRE(oh != nullptr);
    LbFileSeek(oh, 0, Lb_FILE_SEEK_END);
    int64_t written = LbFileWrite(oh, "def", 3);
    CHECK(written == 3);
    LbFileClose(oh);
    CHECK(LbFileLength(kTestFile) == 6);
}

TEST_CASE_METHOD(ScratchFile, "LbFileDelete removes an existing file and reports failure for a missing one", "[kfx_platform][bflib_fileio]") {
    TbFileHandle h = LbFileOpen(kTestFile, Lb_FILE_MODE_NEW);
    LbFileClose(h);
    CHECK(LbFileExists(kTestFile) != 0);
    CHECK(LbFileDelete(kTestFile) == 1);
    CHECK(LbFileExists(kTestFile) == 0);
    CHECK(LbFileDelete(kTestFile) == -1); // already gone
}

TEST_CASE("LbFileLength returns -1 for a nonexistent file", "[kfx_platform][bflib_fileio]") {
    CHECK(LbFileLength("/tmp/kfx_platform_utest_definitely_missing_file.bin") == -1);
}

TEST_CASE("LbDirectoryCurrent fills the buffer with an absolute path", "[kfx_platform][bflib_fileio]") {
    char buf[1024];
    CHECK(LbDirectoryCurrent(buf, sizeof(buf)) == 1);
    CHECK(buf[0] == '/');
}

namespace {
// Regression coverage for the absolute-path bug this file's own header
// comment used to just document (now fixed in create_directory_for_file()).
// A multi-level throwaway tree under a unique mkdtemp() root, so the test
// also exercises the "create every missing intermediate directory" half
// of create_directory_for_file(), not just "don't choke on a leading '/'".
struct ScratchAbsoluteTree {
    char root[64];
    std::string nested_file;
    ScratchAbsoluteTree() {
        std::strcpy(root, "/tmp/kfx_platform_utest_absdir_XXXXXX");
        REQUIRE(mkdtemp(root) != nullptr);
        nested_file = std::string(root) + "/a/b/c/leaf.bin";
    }
    ~ScratchAbsoluteTree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec); // best-effort cleanup, not asserted
    }
};
}

TEST_CASE_METHOD(ScratchAbsoluteTree, "LbFileOpen in NEW mode creates every missing intermediate directory for an absolute path", "[kfx_platform][bflib_fileio]") {
    TbFileHandle h = LbFileOpen(nested_file.c_str(), Lb_FILE_MODE_NEW);
    REQUIRE(h != nullptr);
    const char *payload = "abs path works";
    CHECK(LbFileWrite(h, payload, std::strlen(payload)) == (int64_t)std::strlen(payload));
    CHECK(LbFileClose(h) == 1);
    CHECK(LbFileExists(nested_file.c_str()) != 0);
    CHECK(LbFileLength(nested_file.c_str()) == (int64_t)std::strlen(payload));
}
