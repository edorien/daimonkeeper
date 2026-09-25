// kfx_platform: bflib_text.c's codepage/UTF-8 conversion helpers. All
// pure given an explicit input buffer + size (no locale/OS text APIs on
// the non-CJK path, which is the one exercised here) -- Lang_Japanese/
// Lang_ChineseInt/Lang_ChineseTra/Lang_Korean route through iconv (or
// Win32 MultiByteToWideChar) instead of the static internal_codepage_map
// table, a real OS/library text-conversion dependency not attempted
// here.
#include <catch2/catch_test_macros.hpp>

#include "bflib_text.h"
#include "globals.h" // Lang_English/Lang_Japanese/...

#include <cstring>

TEST_CASE("encode_utf8_codepoint passes ASCII codepoints through as a single byte", "[kfx_platform][bflib_text]") {
    char dst[8];
    CHECK(encode_utf8_codepoint('A', dst, sizeof(dst)) == 1);
    CHECK(dst[0] == 'A');
}

TEST_CASE("encode_utf8_codepoint encodes a 2-byte codepoint (U+00C7, Ç) correctly", "[kfx_platform][bflib_text]") {
    unsigned char dst[8];
    size_t n = encode_utf8_codepoint(0x00C7, reinterpret_cast<char*>(dst), sizeof(dst));
    CHECK(n == 2);
    CHECK(dst[0] == 0xC3);
    CHECK(dst[1] == 0x87);
}

TEST_CASE("encode_utf8_codepoint encodes a 3-byte codepoint (U+20AC, Euro sign) correctly", "[kfx_platform][bflib_text]") {
    unsigned char dst[8];
    size_t n = encode_utf8_codepoint(0x20AC, reinterpret_cast<char*>(dst), sizeof(dst));
    CHECK(n == 3);
    CHECK(dst[0] == 0xE2);
    CHECK(dst[1] == 0x82);
    CHECK(dst[2] == 0xAC);
}

TEST_CASE("encode_utf8_codepoint encodes a 4-byte codepoint (U+1F600, an emoji) correctly", "[kfx_platform][bflib_text]") {
    unsigned char dst[8];
    size_t n = encode_utf8_codepoint(0x1F600, reinterpret_cast<char*>(dst), sizeof(dst));
    CHECK(n == 4);
    CHECK(dst[0] == 0xF0);
    CHECK(dst[1] == 0x9F);
    CHECK(dst[2] == 0x98);
    CHECK(dst[3] == 0x80);
}

TEST_CASE("encode_utf8_codepoint returns 0 for a codepoint past the Unicode range", "[kfx_platform][bflib_text]") {
    char dst[8];
    CHECK(encode_utf8_codepoint(0x110000, dst, sizeof(dst)) == 0); // one past 0x10FFFF
}

TEST_CASE("encode_utf8_codepoint returns 0 when dst_size is 0", "[kfx_platform][bflib_text]") {
    CHECK(encode_utf8_codepoint('A', nullptr, 0) == 0);
}

TEST_CASE("encode_utf8_codepoint returns 0 when dst_size is too small for the required encoding length", "[kfx_platform][bflib_text]") {
    char dst[1];
    CHECK(encode_utf8_codepoint(0x00C7, dst, 1) == 0); // needs 2 bytes, only 1 available
}

TEST_CASE("convert_codepage_to_utf8_buffer passes ASCII text through unchanged", "[kfx_platform][bflib_text]") {
    char dst[32];
    size_t n = convert_codepage_to_utf8_buffer("Hello", 5, dst, sizeof(dst), Lang_English);
    CHECK(n == 5);
    CHECK(std::memcmp(dst, "Hello", 5) == 0);
    CHECK(dst[5] == '\0');
}

TEST_CASE("convert_codepage_to_utf8_buffer maps a codepage byte through internal_codepage_map to real UTF-8", "[kfx_platform][bflib_text]") {
    unsigned char src[1] = {0x80}; // maps to U+00C7 (Ç) in the internal codepage table
    unsigned char dst[8];
    size_t n = convert_codepage_to_utf8_buffer(reinterpret_cast<const char*>(src), 1, reinterpret_cast<char*>(dst), sizeof(dst), Lang_English);
    CHECK(n == 2);
    CHECK(dst[0] == 0xC3);
    CHECK(dst[1] == 0x87);
}

TEST_CASE("convert_codepage_to_utf8_buffer falls back to '?' for a byte with no codepage mapping", "[kfx_platform][bflib_text]") {
    unsigned char src[1] = {0xB8}; // one of the table's unmapped (0) gap entries
    char dst[8];
    size_t n = convert_codepage_to_utf8_buffer(reinterpret_cast<const char*>(src), 1, dst, sizeof(dst), Lang_English);
    CHECK(n == 1);
    CHECK(dst[0] == '?');
}

TEST_CASE("convert_codepage_to_utf8_buffer returns 0 for a NULL src, NULL dst, or zero size", "[kfx_platform][bflib_text]") {
    char dst[8];
    CHECK(convert_codepage_to_utf8_buffer(nullptr, 5, dst, sizeof(dst), Lang_English) == 0);
    CHECK(convert_codepage_to_utf8_buffer("abc", 3, nullptr, 8, Lang_English) == 0);
    CHECK(convert_codepage_to_utf8_buffer("abc", 0, dst, sizeof(dst), Lang_English) == 0);
    CHECK(convert_codepage_to_utf8_buffer("abc", 3, dst, 0, Lang_English) == 0);
}

TEST_CASE("convert_codepage_to_utf8_buffer truncates and null-terminates when the output buffer is too small", "[kfx_platform][bflib_text]") {
    char dst[3];
    size_t n = convert_codepage_to_utf8_buffer("Hello", 5, dst, sizeof(dst), Lang_English);
    CHECK(n < 5);
    CHECK(dst[n] == '\0');
}

TEST_CASE("read_utf_8_codepoint_f decodes a 1-byte ASCII sequence", "[kfx_platform][bflib_text]") {
    size_t seq_len;
    uint64_t cp = read_utf_8_codepoint("A", &seq_len);
    CHECK(cp == 'A');
    CHECK(seq_len == 1);
}

TEST_CASE("read_utf_8_codepoint_f decodes a 2-byte sequence (U+00C7, Ç)", "[kfx_platform][bflib_text]") {
    const char text[] = "\xC3\x87";
    size_t seq_len;
    uint64_t cp = read_utf_8_codepoint(text, &seq_len);
    CHECK(cp == 0x00C7);
    CHECK(seq_len == 2);
}

TEST_CASE("read_utf_8_codepoint_f decodes a 3-byte sequence (U+20AC, Euro sign)", "[kfx_platform][bflib_text]") {
    const char text[] = "\xE2\x82\xAC";
    size_t seq_len;
    uint64_t cp = read_utf_8_codepoint(text, &seq_len);
    CHECK(cp == 0x20AC);
    CHECK(seq_len == 3);
}

TEST_CASE("read_utf_8_codepoint_f decodes a 4-byte sequence (U+1F600, an emoji)", "[kfx_platform][bflib_text]") {
    const char text[] = "\xF0\x9F\x98\x80";
    size_t seq_len;
    uint64_t cp = read_utf_8_codepoint(text, &seq_len);
    CHECK(cp == 0x1F600);
    CHECK(seq_len == 4);
}

TEST_CASE("read_utf_8_codepoint_f falls back to '?' (seq_len 1) for a malformed lead byte with a broken continuation", "[kfx_platform][bflib_text]") {
    const char text[] = "\xC0\x00"; // 2-byte lead, but the next byte isn't a valid continuation byte
    size_t seq_len;
    uint64_t cp = read_utf_8_codepoint(text, &seq_len);
    CHECK(cp == '?');
    CHECK(seq_len == 1);
}

TEST_CASE("read_utf_8_codepoint_f falls back to '?' for a stray continuation byte used as a lead byte", "[kfx_platform][bflib_text]") {
    const char text[] = "\x80\x00";
    size_t seq_len;
    uint64_t cp = read_utf_8_codepoint(text, &seq_len);
    CHECK(cp == '?');
    CHECK(seq_len == 1);
}
