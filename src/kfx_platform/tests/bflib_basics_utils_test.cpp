// kfx_platform: bflib_basics.c's pure string/number utility functions --
// get_rid, llong, lword, saturate_set_signed, str_append, str_appendf,
// make_lowercase, make_uppercase, natoi. Split out of bflib_basics_test.cpp
// (which covers get_gameturn/saturate_set_unsigned, both pattern-B on a
// registered provider) since none of these need any fake/provider setup
// at all -- every one is a bare pure function.
//
// get_rid's "RANDOM" branch is deliberately not tested: it reads the
// global rand() stream, so its return value can't be pinned down without
// either seeding rand() (fragile across libc versions) or changing the
// function to take a seed/RNG parameter (out of scope for a test-only
// change).
#include <inttypes.h>
#include <catch2/catch_test_macros.hpp>

#include "bflib_basics.h"

#include <cstring>

TEST_CASE("get_rid finds a case-insensitive match by name and returns its num", "[kfx_platform][bflib_basics]") {
    const struct NamedCommand table[] = {
        {"Foo", 1},
        {"Bar", 2},
        {NULL, 0},
    };
    CHECK(get_rid(table, "bar") == 2);
    CHECK(get_rid(table, "FOO") == 1);
}

TEST_CASE("get_rid returns -1 for a name not present in the table", "[kfx_platform][bflib_basics]") {
    const struct NamedCommand table[] = {
        {"Foo", 1},
        {NULL, 0},
    };
    CHECK(get_rid(table, "nope") == -1);
}

TEST_CASE("get_rid returns -1 for a NULL table or NULL name", "[kfx_platform][bflib_basics]") {
    const struct NamedCommand table[] = {
        {"Foo", 1},
        {NULL, 0},
    };
    CHECK(get_rid(nullptr, "Foo") == -1);
    CHECK(get_rid(table, nullptr) == -1);
}

TEST_CASE("llong reads a little-endian 32-bit value", "[kfx_platform][bflib_basics]") {
    unsigned char bytes[4] = {0x78, 0x56, 0x34, 0x12};
    CHECK(llong(bytes) == 0x12345678u);
}

TEST_CASE("lword reads a little-endian 16-bit value", "[kfx_platform][bflib_basics]") {
    unsigned char bytes[2] = {0xCD, 0xAB};
    CHECK(lword(bytes) == 0xABCDu);
}

TEST_CASE("saturate_set_signed passes through a value that fits in nbits", "[kfx_platform][bflib_basics]") {
    CHECK(saturate_set_signed(10, 8) == 10);   // fits comfortably in 8 bits signed
    CHECK(saturate_set_signed(-10, 8) == -10);
}

TEST_CASE("saturate_set_signed clamps to the max positive value once val reaches or exceeds it", "[kfx_platform][bflib_basics]") {
    // 8 bits: maximum_value = (1 << 7) - 1 = 127
    CHECK(saturate_set_signed(127, 8) == 127);
    CHECK(saturate_set_signed(128, 8) == 127);
    CHECK(saturate_set_signed(99999, 8) == 127);
}

TEST_CASE("saturate_set_signed clamps to the negated max once val reaches or drops below it", "[kfx_platform][bflib_basics]") {
    CHECK(saturate_set_signed(-127, 8) == -127);
    CHECK(saturate_set_signed(-128, 8) == -127);
    CHECK(saturate_set_signed(-99999, 8) == -127);
}

TEST_CASE("str_append appends within the buffer's remaining capacity and returns the new length", "[kfx_platform][bflib_basics]") {
    char buffer[32] = "abc";
    int64_t result = str_append(buffer, sizeof(buffer), "def");
    CHECK(std::strcmp(buffer, "abcdef") == 0);
    CHECK(result == 6);
}

TEST_CASE("str_append is a no-op and returns the current length once the buffer is already full", "[kfx_platform][bflib_basics]") {
    char buffer[4] = "abc"; // size(3) - strlen(3) = 0, not > 0
    int64_t result = str_append(buffer, 3, "xyz");
    CHECK(std::strcmp(buffer, "abc") == 0); // untouched
    CHECK(result == 3);
}

TEST_CASE("str_appendf formats and appends within the buffer's remaining capacity", "[kfx_platform][bflib_basics]") {
    char buffer[32] = "n=";
    int64_t result = str_appendf(buffer, sizeof(buffer), "%" PRId64, (int64_t)(42));
    CHECK(std::strcmp(buffer, "n=42") == 0);
    CHECK(result == 4);
}

TEST_CASE("str_appendf is a no-op and returns the current length once the buffer is already full", "[kfx_platform][bflib_basics]") {
    char buffer[4] = "abc";
    int64_t result = str_appendf(buffer, 3, "%" PRId64, (int64_t)(99)); // size(3) - strlen(3) = 0
    CHECK(std::strcmp(buffer, "abc") == 0);
    CHECK(result == 3);
}

TEST_CASE("make_lowercase lowercases every character in place", "[kfx_platform][bflib_basics]") {
    char buffer[] = "Hello World 123";
    make_lowercase(buffer);
    CHECK(std::strcmp(buffer, "hello world 123") == 0);
}

TEST_CASE("make_uppercase uppercases every character in place", "[kfx_platform][bflib_basics]") {
    char buffer[] = "Hello World 123";
    make_uppercase(buffer);
    CHECK(std::strcmp(buffer, "HELLO WORLD 123") == 0);
}

TEST_CASE("natoi parses a purely-numeric prefix of the given length", "[kfx_platform][bflib_basics]") {
    CHECK(natoi("12345", 5) == 12345);
}

TEST_CASE("natoi stops at the first non-digit and returns the value accumulated so far", "[kfx_platform][bflib_basics]") {
    CHECK(natoi("12a45", 5) == 12);
}

TEST_CASE("natoi returns -1 when the very first character isn't a digit", "[kfx_platform][bflib_basics]") {
    CHECK(natoi("abc", 3) == -1);
}

TEST_CASE("natoi returns -1 for a zero-length input", "[kfx_platform][bflib_basics]") {
    CHECK(natoi("", 0) == -1);
}
