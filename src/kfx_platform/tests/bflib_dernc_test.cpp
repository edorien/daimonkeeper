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
// The RNC decompression path is covered with a fixture built in the test
// (make_rnc_fixture() below): a single-chunk method-1 stream of 64 literal
// bytes plus one back-reference that repeats them, no compressor needed.
#include <catch2/catch_test_macros.hpp>

#include "bflib_dernc.h"
#include "bflib_fileio.h" // LbFileExists -- LbFileSaveAtomic's own .tmp-cleanup test

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

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
// One file per process: ctest runs each test case as its own process, in
// parallel, so a shared name lets one case delete another's file.
const std::string kTestFileStr = "kfx_platform_utest_dernc_test_" + std::to_string(getpid()) + ".bin";
const char *const kTestFile = kTestFileStr.c_str();

struct ScratchFile {
    ScratchFile() { std::remove(kTestFile); }
    ~ScratchFile() { std::remove(kTestFile); }
};
}

TEST_CASE_METHOD(ScratchFile, "LbFileLengthRnc reports the plain file length for a non-RNC file", "[kfx_platform][bflib_dernc]") {
    const char *payload = "not compressed, just plain bytes";
    LbFileSaveAt(kTestFile, payload, std::strlen(payload));
    CHECK(LbFileLengthRnc(kTestFile) == (int64_t)std::strlen(payload));
}

TEST_CASE("LbFileLengthRnc returns -1 for a nonexistent file", "[kfx_platform][bflib_dernc]") {
    CHECK(LbFileLengthRnc("kfx_platform_utest_dernc_definitely_missing.bin") == -1);
}

TEST_CASE_METHOD(ScratchFile, "LbFileSaveAt/LbFileLoadAt round-trip a plain (uncompressed) buffer", "[kfx_platform][bflib_dernc]") {
    const char *payload = "round trip me";
    int64_t saved = LbFileSaveAt(kTestFile, payload, std::strlen(payload));
    CHECK(saved == (int64_t)std::strlen(payload));

    char read_back[64] = {0};
    int64_t loaded = LbFileLoadAt(kTestFile, read_back);
    CHECK(loaded == (int64_t)std::strlen(payload));
    CHECK(std::strncmp(read_back, payload, std::strlen(payload)) == 0);
}

TEST_CASE_METHOD(ScratchFile, "LbFileLoadAtMax loads a file that fits like LbFileLoadAt", "[kfx_platform][bflib_dernc]") {
    const char *payload = "fits in the buffer";
    LbFileSaveAt(kTestFile, payload, std::strlen(payload));

    char read_back[64] = {0};
    CHECK(LbFileLoadAtMax(kTestFile, read_back, sizeof(read_back)) == (int64_t)std::strlen(payload));
    CHECK(std::strncmp(read_back, payload, std::strlen(payload)) == 0);
}

TEST_CASE_METHOD(ScratchFile, "LbFileLoadAtMax cuts a larger plain file to the buffer and reports its full length", "[kfx_platform][bflib_dernc]") {
    unsigned char payload[64];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (unsigned char)(i + 1);
    LbFileSaveAt(kTestFile, payload, sizeof(payload));

    unsigned char read_back[32];
    std::memset(read_back, 0xEE, sizeof(read_back));
    CHECK(LbFileLoadAtMax(kTestFile, read_back, 16) == (int64_t)sizeof(payload));
    CHECK(std::memcmp(read_back, payload, 16) == 0);
    for (size_t i = 16; i < sizeof(read_back); i++)
        CHECK(read_back[i] == 0xEE);
}

TEST_CASE("LbFileLoadAtMax returns -1 for a nonexistent file", "[kfx_platform][bflib_dernc]") {
    char read_back[16];
    CHECK(LbFileLoadAtMax("kfx_platform_utest_dernc_definitely_missing.bin", read_back, sizeof(read_back)) == -1);
}

namespace {
// Writes an RNC method-1 bit stream the way rnc_unpack() reads it: bits
// LSB-first out of little-endian 16-bit words, with each literal run placed
// in the byte stream just before the first word the decoder hasn't started
// reading yet.
struct RncBitWriter {
    std::vector<bool> bits;
    std::vector<std::pair<size_t, std::vector<unsigned char>>> literals; // (word index, bytes)

    void put(uint32_t value, int count) {
        for (int i = 0; i < count; i++)
            bits.push_back(((value >> i) & 1) != 0);
    }
    // A value through a one-code Huffman table: the 1-bit code, then the
    // value's low bits under its top bit (huf_read()'s value classes).
    void put_huffman_value(uint32_t code, uint32_t value) {
        put(code, 1);
        if (value >= 2) {
            int top = 31 - __builtin_clz(value);
            put(value - (1u << top), top);
        }
    }
    void put_table(const std::vector<int> &leaf_lengths) {
        put((uint32_t)leaf_lengths.size(), 5);
        for (int len : leaf_lengths)
            put((uint32_t)len, 4);
    }
    void put_literals(const std::vector<unsigned char> &bytes) {
        literals.emplace_back((bits.size() - 1) / 16 + 1, bytes);
    }
    std::vector<unsigned char> bytes() const {
        // Two spare words: the decoder loads ahead of the bits it uses.
        size_t words = (bits.size() + 15) / 16 + 2;
        std::vector<unsigned char> out;
        for (size_t w = 0; w < words; w++) {
            for (const auto &run : literals)
                if (run.first == w)
                    out.insert(out.end(), run.second.begin(), run.second.end());
            uint16_t word = 0;
            for (size_t b = 0; b < 16; b++)
                if (w * 16 + b < bits.size() && bits[w * 16 + b])
                    word |= (uint16_t)(1u << b);
            out.push_back((unsigned char)(word & 0xFF));
            out.push_back((unsigned char)(word >> 8));
        }
        return out;
    }
};

void put_be32(std::vector<unsigned char> &out, size_t pos, uint32_t v) {
    out[pos] = (unsigned char)(v >> 24); out[pos + 1] = (unsigned char)(v >> 16);
    out[pos + 2] = (unsigned char)(v >> 8); out[pos + 3] = (unsigned char)v;
}

void put_be16(std::vector<unsigned char> &out, size_t pos, uint16_t v) {
    out[pos] = (unsigned char)(v >> 8); out[pos + 1] = (unsigned char)v;
}

constexpr uint32_t kRncLiteralCount = 64;
constexpr uint32_t kRncUnpackedSize = 4096;

// The unpacked content: 64 distinct-ish bytes, repeated to 4096.
std::vector<unsigned char> rnc_fixture_unpacked() {
    std::vector<unsigned char> data(kRncUnpackedSize);
    for (uint32_t i = 0; i < kRncUnpackedSize; i++)
        data[i] = (unsigned char)(((i % kRncLiteralCount) * 7 + 3) & 0xFF);
    return data;
}

// A complete RNC file (header + packed stream) for rnc_fixture_unpacked(),
// a little over 100 bytes on disk.
std::vector<unsigned char> make_rnc_fixture() {
    std::vector<unsigned char> unpacked = rnc_fixture_unpacked();
    RncBitWriter bw;
    bw.put(0, 2);                                   // lock and key flags, skipped
    bw.put_table({1, 0, 0, 0, 0, 0, 0, 1});          // literal lengths: class 0 -> code 0, class 7 -> code 1
    bw.put_table({0, 0, 0, 0, 0, 0, 1});             // distances: class 6 -> code 0
    bw.put_table({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}); // copy lengths: class 12 -> code 0
    bw.put(2, 16);                                  // two sub-chunks
    bw.put_huffman_value(1, kRncLiteralCount);      // 64 literals...
    bw.put_literals(std::vector<unsigned char>(unpacked.begin(), unpacked.begin() + kRncLiteralCount));
    bw.put_huffman_value(0, kRncLiteralCount - 1);  // ...then copy from 64 back
    bw.put_huffman_value(0, kRncUnpackedSize - kRncLiteralCount - 2); // for the rest
    bw.put_huffman_value(0, 0);                     // last sub-chunk: no literals
    std::vector<unsigned char> packed = bw.bytes();

    std::vector<unsigned char> file(RNC_HEADER_LEN, 0);
    file[0] = 'R'; file[1] = 'N'; file[2] = 'C'; file[3] = 1;
    put_be32(file, 4, kRncUnpackedSize);
    put_be32(file, 8, (uint32_t)packed.size());
    put_be16(file, 12, (uint16_t)rnc_crc(unpacked.data(), unpacked.size()));
    put_be16(file, 14, (uint16_t)rnc_crc(packed.data(), packed.size()));
    file[17] = 1;                                   // chunk count
    file.insert(file.end(), packed.begin(), packed.end());
    return file;
}
}

TEST_CASE_METHOD(ScratchFile, "the hand-built RNC fixture unpacks through LbFileLoadAt", "[kfx_platform][bflib_dernc]") {
    const std::vector<unsigned char> file = make_rnc_fixture();
    REQUIRE(file.size() < 1024);
    LbFileSaveAt(kTestFile, file.data(), file.size());

    CHECK(LbFileLengthRnc(kTestFile) == (int64_t)kRncUnpackedSize);
    std::vector<unsigned char> read_back(kRncUnpackedSize);
    REQUIRE(LbFileLoadAt(kTestFile, read_back.data()) == (int64_t)kRncUnpackedSize);
    CHECK(read_back == rnc_fixture_unpacked());
}

TEST_CASE_METHOD(ScratchFile, "LbFileLoadAtMax unpacks an RNC file that fits", "[kfx_platform][bflib_dernc]") {
    const std::vector<unsigned char> file = make_rnc_fixture();
    LbFileSaveAt(kTestFile, file.data(), file.size());

    std::vector<unsigned char> read_back(kRncUnpackedSize);
    CHECK(LbFileLoadAtMax(kTestFile, read_back.data(), read_back.size()) == (int64_t)kRncUnpackedSize);
    CHECK(read_back == rnc_fixture_unpacked());
}

TEST_CASE_METHOD(ScratchFile, "LbFileLoadAtMax cuts an RNC file that unpacks larger than the buffer, though its packed size fits", "[kfx_platform][bflib_dernc]") {
    const std::vector<unsigned char> file = make_rnc_fixture();
    LbFileSaveAt(kTestFile, file.data(), file.size());
    constexpr size_t kLimit = 1024; // bigger than the packed file, smaller than its contents

    std::vector<unsigned char> read_back(kLimit + 256, 0xEE);
    CHECK(LbFileLoadAtMax(kTestFile, read_back.data(), kLimit) == (int64_t)kRncUnpackedSize);
    const std::vector<unsigned char> expected = rnc_fixture_unpacked();
    CHECK(std::memcmp(read_back.data(), expected.data(), kLimit) == 0);
    for (size_t i = kLimit; i < read_back.size(); i++)
        CHECK(read_back[i] == 0xEE);
}

// Security pass S08: an RNC file's header sizes come from a file that may be hostile. Each of these used to read or
// write outside the buffers (found by AddressSanitizer), or allocate what the header said.
TEST_CASE_METHOD(ScratchFile, "an RNC header whose packed size is larger than the file is refused", "[kfx_platform][bflib_dernc][security]") {
    std::vector<unsigned char> file = make_rnc_fixture();
    put_be32(file, 8, 1u << 29); // far more packed data than the file holds
    LbFileSaveAt(kTestFile, file.data(), file.size());
    std::vector<unsigned char> read_back(kRncUnpackedSize + 64, 0xEE);
    CHECK(LbFileLoadAt(kTestFile, read_back.data()) == -1);
}

TEST_CASE_METHOD(ScratchFile, "an RNC header whose unpacked size is smaller than the packed data is refused, not overrun", "[kfx_platform][bflib_dernc][security]") {
    std::vector<unsigned char> file = make_rnc_fixture();
    put_be32(file, 4, 8); // the buffer a caller makes from LbFileLengthRnc(): 8 bytes
    LbFileSaveAt(kTestFile, file.data(), file.size());
    CHECK(LbFileLengthRnc(kTestFile) == 8);
    std::vector<unsigned char> read_back(8 + 64, 0xEE);
    CHECK(LbFileLoadAt(kTestFile, read_back.data()) == -1);
    for (size_t i = 8; i < read_back.size(); i++)
        CHECK(read_back[i] == 0xEE); // nothing past the 8 bytes the caller has was touched
}

TEST_CASE_METHOD(ScratchFile, "a packed file that is larger than its unpacked size loads (incompressible data)", "[kfx_platform][bflib_dernc]") {
    // the packed size, not the unpacked one, is what has to fit in the file: here a one-byte literal run in 100+ bytes
    const std::vector<unsigned char> file = make_rnc_fixture();
    LbFileSaveAt(kTestFile, file.data(), file.size());
    std::vector<unsigned char> read_back(kRncUnpackedSize);
    CHECK(LbFileLoadAt(kTestFile, read_back.data()) == (int64_t)kRncUnpackedSize);
}

TEST_CASE_METHOD(ScratchFile, "an RNC file shorter than its header is a plain file, not read past its end", "[kfx_platform][bflib_dernc][security]") {
    const unsigned char tiny[8] = { 'R', 'N', 'C', 1, 0, 0, 0, 9 };
    LbFileSaveAt(kTestFile, tiny, sizeof(tiny));
    CHECK(LbFileLengthRnc(kTestFile) == 8);
    unsigned char read_back[8];
    CHECK(LbFileLoadAt(kTestFile, read_back) == 8);
    CHECK(UnpackM1(read_back, 5) == 0); // a buffer shorter than a header
}

TEST_CASE_METHOD(ScratchFile, "an RNC header that claims over a gigabyte unpacked is refused", "[kfx_platform][bflib_dernc][security]") {
    std::vector<unsigned char> file = make_rnc_fixture();
    put_be32(file, 4, 0xF0000000u);
    LbFileSaveAt(kTestFile, file.data(), file.size());
    CHECK(LbFileLengthRnc(kTestFile) == -1);
}

TEST_CASE_METHOD(ScratchFile, "a packed stream that tries to copy past the end of the output is refused", "[kfx_platform][bflib_dernc][security]") {
    // the same stream, told its output is one byte shorter: the last match copy would write one byte beyond it
    std::vector<unsigned char> file = make_rnc_fixture();
    put_be32(file, 4, kRncUnpackedSize - 1);
    LbFileSaveAt(kTestFile, file.data(), file.size());
    std::vector<unsigned char> read_back(kRncUnpackedSize - 1 + 64, 0xEE);
    CHECK(LbFileLoadAt(kTestFile, read_back.data()) < 0);
    for (size_t i = kRncUnpackedSize - 1; i < read_back.size(); i++)
        CHECK(read_back[i] == 0xEE);
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
const std::string kAtomicTestFileStr = "kfx_platform_utest_dernc_atomic_test_" + std::to_string(getpid()) + ".bin";
const char *const kAtomicTestFile = kAtomicTestFileStr.c_str();
const std::string kAtomicTestFileTmpStr = kAtomicTestFileStr + ".tmp";
const char *const kAtomicTestFileTmp = kAtomicTestFileTmpStr.c_str();

struct ScratchAtomicFile {
    ScratchAtomicFile() { std::remove(kAtomicTestFile); std::remove(kAtomicTestFileTmp); }
    ~ScratchAtomicFile() { std::remove(kAtomicTestFile); std::remove(kAtomicTestFileTmp); }
};
}

TEST_CASE_METHOD(ScratchAtomicFile, "LbFileSaveAtomic writes a new file whose content round-trips", "[kfx_platform][bflib_dernc]") {
    const char *payload = "atomic write, first time";
    CHECK(LbFileSaveAtomic(kAtomicTestFile, payload, std::strlen(payload)));

    char read_back[64] = {0};
    int64_t loaded = LbFileLoadAt(kAtomicTestFile, read_back);
    CHECK(loaded == (int64_t)std::strlen(payload));
    CHECK(std::strncmp(read_back, payload, std::strlen(payload)) == 0);
}

TEST_CASE_METHOD(ScratchAtomicFile, "LbFileSaveAtomic replaces an existing file's content", "[kfx_platform][bflib_dernc]") {
    const char *original = "the original content, somewhat longer than the replacement";
    LbFileSaveAt(kAtomicTestFile, original, std::strlen(original));

    const char *replacement = "replaced";
    CHECK(LbFileSaveAtomic(kAtomicTestFile, replacement, std::strlen(replacement)));

    char read_back[128] = {0};
    int64_t loaded = LbFileLoadAt(kAtomicTestFile, read_back);
    CHECK(loaded == (int64_t)std::strlen(replacement));
    CHECK(std::strncmp(read_back, replacement, std::strlen(replacement)) == 0);
}

TEST_CASE_METHOD(ScratchAtomicFile, "LbFileSaveAtomic leaves no .tmp sibling behind after a successful save", "[kfx_platform][bflib_dernc]") {
    const char *payload = "no leftovers";
    REQUIRE(LbFileSaveAtomic(kAtomicTestFile, payload, std::strlen(payload)));
    CHECK(LbFileExists(kAtomicTestFileTmp) == 0);
    CHECK(LbFileExists(kAtomicTestFile) != 0);
}
