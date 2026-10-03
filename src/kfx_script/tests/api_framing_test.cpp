// kfx_script: finding whole JSON messages in an API client's byte stream (api_framing.h). One message can arrive over
// several reads and one read can hold several; braces inside strings (a plan that mentions "{", an agent memory that
// is itself JSON in a string) must not end a message early.
#include <catch2/catch_test_macros.hpp>

#include "api_framing.h"

#include <cstring>
#include <string>

namespace {
std::string next(const std::string &buf, size_t *consumed = nullptr, bool *complete = nullptr)
{
    size_t s = 0, e = 0;
    const int ok = api_frame_next(buf.data(), buf.size(), &s, &e);
    if (complete) *complete = (ok != 0);
    if (consumed) *consumed = ok ? e : s;
    return ok ? buf.substr(s, e - s) : std::string();
}
}

TEST_CASE("api framing: one object, and several in one read", "[api_framing]")
{
    size_t used = 0;
    CHECK(next("{\"a\":1}\n", &used) == "{\"a\":1}");
    CHECK(used == 7);
    std::string two = "{\"a\":1}\n{\"b\":{\"c\":2}}\n";
    CHECK(next(two, &used) == "{\"a\":1}");
    CHECK(next(two.substr(used)) == "{\"b\":{\"c\":2}}");
}

TEST_CASE("api framing: an unfinished object waits for more bytes", "[api_framing]")
{
    bool complete = true;
    size_t keep_from = 0;
    CHECK(next("junk\n{\"a\":{\"b\":", &keep_from, &complete).empty());
    CHECK_FALSE(complete);
    CHECK(keep_from == 5);        // the junk before the object can go
    CHECK(next("\n\n", &keep_from, &complete).empty());
    CHECK_FALSE(complete);
    CHECK(keep_from == 2);        // nothing but newlines: all of it can go
}

TEST_CASE("api framing: braces and quotes inside strings do not count", "[api_framing]")
{
    const std::string msg = "{\"data\":\"{\\\"plan\\\":\\\"} dig {\\\"}\",\"x\":\"a\\\\\"}";
    CHECK(next(msg + "{\"next\":1}") == msg);
    bool complete = true;
    next("{\"data\":\"}\"", nullptr, &complete);
    CHECK_FALSE(complete);        // the string's brace did not close it
}

TEST_CASE("api framing: a message far bigger than one read", "[api_framing]")
{
    std::string big = "{\"action\":\"set_agent_memory\",\"data\":\"" + std::string(70000, 'x') + "\"}";
    bool complete = false;
    CHECK(next(big.substr(0, 4095), nullptr, &complete).empty());
    CHECK_FALSE(complete);
    CHECK(next(big) == big);
}
