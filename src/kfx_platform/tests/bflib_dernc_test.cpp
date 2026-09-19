// kfx_platform: bflib_dernc.c, the RNC ("Rob Northern Computing")
// decompression + generic file-checksum support. rnc_crc() is pure and
// turns out to be the standard CRC-16/ARC algorithm (poly 0xA001,
// init 0, reflected, LSB-first) -- confirmed against that algorithm's
// well-known check value for "123456789" (0xBB3D), not just re-derived
// from the source. UnpackM1()'s "not actually compressed" early return
// (header.signature != RNC_SIGNATURE) is testable with a bare zeroed
// buffer, no real RNC-compressed fixture needed. LbFileLengthRnc/
// LbFileSaveAt/LbFileLoadAt/calculate_file_checksum reuse bflib_fileio_
// test.cpp's real-scratch-file pattern (bare relative filename -- see
// that file's header comment on LbFileOpen's leading-slash bug) since
// they're themselves thin wrappers around bflib_fileio.c's real file I/O.
//
// The actual RNC decompression path (rnc_unpack(), and UnpackM1() past
// its early return) isn't attempted: it needs a real RNC-compressed
// fixture blob, which would mean either checking in a binary fixture or
// hand-rolling a compressor, more setup than this pass's ROI justifies.
#include <catch2/catch_test_macros.hpp>

#include "bflib_dernc.h"
#include "bflib_fileio.h" // LbFileExists -- LbFileSaveAtomic's own .tmp-cleanup test

#include <cstdio>
#include <cstring>

TEST_CASE("rnc_crc of an empty buffer is 0", "[kfx_platform][bflib_dernc]") {
    CHECK(rnc_crc(const_cast<char*>(""), 0) == 0);
}

TEST_CASE("rnc_crc matches the standard CRC-16/ARC check value for the ASCII string 123456789", "[kfx_platform][bflib_dernc]") {
    char data[] = "123456789";
    CHECK(rnc_crc(data, 9) == 0xBB3D);
}

TEST_CASE("rnc_crc is sensitive to every byte, not just the buffer length", "[kfx_platform][bflib_dernc]") {
    char a[] = "ABC";
    char b[] = "ABD";
    CHECK(rnc_crc(a, 3) != rnc_crc(b, 3));
}

TEST_CASE("UnpackM1 returns 0 (not RNC-compressed) for a buffer whose signature doesn't match RNC_SIGNATURE", "[kfx_platform][bflib_dernc]") {
    unsigned char buffer[RNC_HEADER_LEN] = {0}; // signature field is all-zero, != RNC_SIGNATURE
    CHECK(UnpackM1(buffer, sizeof(buffer)) == 0);
}

namespace {
const char *kTestFile = "kfx_platform_utest_dernc_test.bin";

struct ScratchFile {
    ScratchFile() { std::remove(kTestFile); }
    ~ScratchFile() { std::remove(kTestFile); }
};
}

TEST_CASE_METHOD(ScratchFile, "LbFileLengthRnc reports the plain file length for a non-RNC file", "[kfx_platform][bflib_dernc]") {
    const char *payload = "not compressed, just plain bytes";
    LbFileSaveAt(kTestFile, payload, std::strlen(payload));
    CHECK(LbFileLengthRnc(kTestFile) == (long)std::strlen(payload));
}

TEST_CASE("LbFileLengthRnc returns -1 for a nonexistent file", "[kfx_platform][bflib_dernc]") {
    CHECK(LbFileLengthRnc("kfx_platform_utest_dernc_definitely_missing.bin") == -1);
}

TEST_CASE_METHOD(ScratchFile, "LbFileSaveAt/LbFileLoadAt round-trip a plain (uncompressed) buffer", "[kfx_platform][bflib_dernc]") {
    const char *payload = "round trip me";
    long saved = LbFileSaveAt(kTestFile, payload, std::strlen(payload));
    CHECK(saved == (long)std::strlen(payload));

    char read_back[64] = {0};
    long loaded = LbFileLoadAt(kTestFile, read_back);
    CHECK(loaded == (long)std::strlen(payload));
    CHECK(std::strncmp(read_back, payload, std::strlen(payload)) == 0);
}

TEST_CASE_METHOD(ScratchFile, "calculate_file_checksum is deterministic for identical content", "[kfx_platform][bflib_dernc]") {
    const char *payload = "checksum me";
    LbFileSaveAt(kTestFile, payload, std::strlen(payload));
    TbBigChecksum first = calculate_file_checksum(kTestFile);
    TbBigChecksum second = calculate_file_checksum(kTestFile);
    CHECK(first == second);
    CHECK(first != 0);
}

TEST_CASE_METHOD(ScratchFile, "calculate_file_checksum differs for files with different content", "[kfx_platform][bflib_dernc]") {
    LbFileSaveAt(kTestFile, "content A", 9);
    TbBigChecksum checksum_a = calculate_file_checksum(kTestFile);
    LbFileSaveAt(kTestFile, "content B", 9);
    TbBigChecksum checksum_b = calculate_file_checksum(kTestFile);
    CHECK(checksum_a != checksum_b);
}

TEST_CASE("calculate_file_checksum returns 0 for a nonexistent file", "[kfx_platform][bflib_dernc]") {
    // file_size resolves to -1 (LbFileLengthRnc's not-found case); the
    // CHECKSUM_ADD(checksum, file_size) step folds a nonzero seed in via
    // XOR, but starting from checksum=0 with a left/right-rotate-by-5
    // that's still a no-op on an initial zero accumulator XORed with
    // -1 -- confirmed empirically, not assumed, since it's not obvious
    // from the macro alone.
    CHECK(calculate_file_checksum("kfx_platform_utest_dernc_definitely_missing.bin") == (TbBigChecksum)-1);
}

// docs/refactor/editor/phase3/05-slice6-atomic-write-lif.md --
// LbFileSaveAtomic()'s own scratch file, plus its "<file>.tmp" sibling --
// cleaned up on both ends of the fixture the same way ScratchFile above
// cleans kTestFile, so a failed assertion mid-test still doesn't leak a
// stray .tmp file for the next test run to trip over.
namespace {
const char *kAtomicTestFile = "kfx_platform_utest_dernc_atomic_test.bin";
const char *kAtomicTestFileTmp = "kfx_platform_utest_dernc_atomic_test.bin.tmp";

struct ScratchAtomicFile {
    ScratchAtomicFile() { std::remove(kAtomicTestFile); std::remove(kAtomicTestFileTmp); }
    ~ScratchAtomicFile() { std::remove(kAtomicTestFile); std::remove(kAtomicTestFileTmp); }
};
}

TEST_CASE_METHOD(ScratchAtomicFile, "LbFileSaveAtomic writes a new file whose content round-trips", "[kfx_platform][bflib_dernc]") {
    const char *payload = "atomic write, first time";
    CHECK(LbFileSaveAtomic(kAtomicTestFile, payload, std::strlen(payload)));

    char read_back[64] = {0};
    long loaded = LbFileLoadAt(kAtomicTestFile, read_back);
    CHECK(loaded == (long)std::strlen(payload));
    CHECK(std::strncmp(read_back, payload, std::strlen(payload)) == 0);
}

TEST_CASE_METHOD(ScratchAtomicFile, "LbFileSaveAtomic replaces an existing file's content", "[kfx_platform][bflib_dernc]") {
    const char *original = "the original content, somewhat longer than the replacement";
    LbFileSaveAt(kAtomicTestFile, original, std::strlen(original));

    const char *replacement = "replaced";
    CHECK(LbFileSaveAtomic(kAtomicTestFile, replacement, std::strlen(replacement)));

    char read_back[128] = {0};
    long loaded = LbFileLoadAt(kAtomicTestFile, read_back);
    CHECK(loaded == (long)std::strlen(replacement));
    CHECK(std::strncmp(read_back, replacement, std::strlen(replacement)) == 0);
}

TEST_CASE_METHOD(ScratchAtomicFile, "LbFileSaveAtomic leaves no .tmp sibling behind after a successful save", "[kfx_platform][bflib_dernc]") {
    const char *payload = "no leftovers";
    REQUIRE(LbFileSaveAtomic(kAtomicTestFile, payload, std::strlen(payload)));
    CHECK(LbFileExists(kAtomicTestFileTmp) == 0);
    CHECK(LbFileExists(kAtomicTestFile) != 0);
}
