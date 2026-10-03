// kfx_script: api_log_tail.c -- the get_log_tail line-splitting logic, on in-memory buffers rather than a real log
// file, so the windowing (a tail read that may not start on a line boundary) is testable exactly.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <json.h>
#include <json-dom.h>

#include "api_log_tail.h"

namespace {

std::vector<std::string> lines_of(const char *text, bool whole, int64_t want) {
    VALUE arr;
    value_init_array(&arr);
    api_log_tail_lines(text, strlen(text), whole, want, &arr);
    std::vector<std::string> out;
    for (size_t i = 0; i < value_array_size(&arr); i++) out.push_back(value_string(value_array_get(&arr, i)));
    value_fini(&arr);
    return out;
}

} // namespace

TEST_CASE("the whole file, fewer lines than asked for: every line comes back, oldest first", "[kfx_script][log_tail]") {
    CHECK(lines_of("one\ntwo\nthree\n", true, 10) == std::vector<std::string>{"one", "two", "three"});
}

TEST_CASE("more lines than asked for: only the last N, still oldest first", "[kfx_script][log_tail]") {
    CHECK(lines_of("one\ntwo\nthree\nfour\n", true, 2) == std::vector<std::string>{"three", "four"});
}

TEST_CASE("a trailing newline is not counted as an extra empty line", "[kfx_script][log_tail]") {
    CHECK(lines_of("one\ntwo\n", true, 10) == std::vector<std::string>{"one", "two"});
    CHECK(lines_of("one\ntwo", true, 10) == std::vector<std::string>{"one", "two"}); // no trailing newline at all
}

TEST_CASE("CRLF line endings lose the CR too", "[kfx_script][log_tail]") {
    CHECK(lines_of("one\r\ntwo\r\n", true, 10) == std::vector<std::string>{"one", "two"});
}

TEST_CASE("a windowed (not whole-file) read drops the possibly-partial earliest line", "[kfx_script][log_tail]") {
    // As if the real file were "...prefix-cut-off-mid-lineTWO\nthree\nfour\n" and we only read from some offset.
    CHECK(lines_of("ONE\ntwo\nthree\n", false, 10) == std::vector<std::string>{"two", "three"});
}

TEST_CASE("a windowed read that already has enough full lines still drops the earliest one", "[kfx_script][log_tail]") {
    CHECK(lines_of("ONE\ntwo\nthree\nfour\n", false, 2) == std::vector<std::string>{"three", "four"});
}

TEST_CASE("an empty or all-blank buffer yields nothing", "[kfx_script][log_tail]") {
    CHECK(lines_of("", true, 10).empty());
    CHECK(lines_of("\n\n\n", true, 10).empty());
}

TEST_CASE("want <= 0 yields nothing", "[kfx_script][log_tail]") {
    CHECK(lines_of("one\ntwo\n", true, 0).empty());
}

TEST_CASE("a single line with no newline at all is kept when it is the whole buffer", "[kfx_script][log_tail]") {
    CHECK(lines_of("only line, no newline", true, 5) == std::vector<std::string>{"only line, no newline"});
}

TEST_CASE("a single partial line with no newline is dropped when it is not the whole buffer", "[kfx_script][log_tail]") {
    CHECK(lines_of("mid-line fragment", false, 5).empty());
}
